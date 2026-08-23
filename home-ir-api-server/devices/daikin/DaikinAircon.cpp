#include "DaikinAircon.h"

#include <string.h>
#include <time.h>

namespace {
constexpr uint8_t kDefaultTemperature = 26;
constexpr uint8_t kArc446A3SleepByte = 29;
constexpr uint8_t kArc446A3SleepMask = 0x04;
constexpr uint8_t kArc446A3HealthByte = 29;
constexpr uint8_t kArc446A3HealthMask = 0x08;
constexpr uint8_t kArc446A3CleanByte = 6;
constexpr uint8_t kArc446A3CleanMask = 0x08;
constexpr uint8_t kDaikinSpecialTemperatureByte = 22;
constexpr uint16_t kMinutesPerDay = 24 * 60;
constexpr uint16_t kMaxTimerMinutes = 12 * 60;
}  // namespace

DaikinAircon::DaikinAircon(IrSender &ir)
    : ir_(ir),
      ac_(ir.daikin()),
      power_(true),
      mode_(kDaikinCool),
      normalTemperature_(kDefaultTemperature),
      fan_(kDaikinFanAuto),
      stateSource_(DaikinStateSource::Initial),
      hasReceivedState_(false),
      lastReceivedState_{},
      lastReceivedAtMs_(0),
      timerClockKnown_(false),
      timerClockBaseMinutes_(0),
      timerClockBaseAtMs_(0) {}

void DaikinAircon::begin() {
  ac_.on();
  ac_.setMode(kDaikinCool);
  ac_.setTemp(normalTemperature_);
  ac_.setFan(kDaikinFanAuto);
  ac_.setSwingVertical(false);
  ac_.setComfort(false);
  ac_.setQuiet(false);
  ac_.setMold(false);
  ac_.disableOnTimer();
  ac_.disableOffTimer();
  setArcSleep(false);
  setArcHealth(false);
  setArcClean(false);
  power_ = true;
  mode_ = kDaikinCool;
  fan_ = kDaikinFanAuto;
  stateSource_ = DaikinStateSource::Initial;
}

IrSendResult DaikinAircon::sendOff() {
  uint8_t previousState[kDaikinStateLength];
  memcpy(previousState, ac_.getRaw(), kDaikinStateLength);
  const DaikinStateSource previousSource = stateSource_;

  ac_.off();
  const IrSendResult result = sendCurrentState();
  if (result != IrSendResult::Ok) {
    ac_.setRaw(previousState, kDaikinStateLength);
    stateSource_ = previousSource;
    return result;
  }
  power_ = false;
  return result;
}

const char *DaikinAircon::modeName(const uint8_t mode) {
  switch (mode) {
    case kDaikinAuto: return "auto";
    case kDaikinCool: return "cool";
    case kDaikinHeat: return "heat";
    case kDaikinDry: return "dry";
    case kDaikinFan: return "fan";
    default: return "unknown";
  }
}

const char *DaikinAircon::stateSourceName(const DaikinStateSource source) {
  switch (source) {
    case DaikinStateSource::Initial: return "initial";
    case DaikinStateSource::Transmitted: return "transmitted";
    case DaikinStateSource::Received: return "received";
  }
  return "initial";
}

String DaikinAircon::fanName(const uint8_t fan) {
  if (fan == kDaikinFanAuto) return "auto";
  if (fan == kDaikinFanQuiet) return "quiet";
  return String(fan);
}

void DaikinAircon::setArcSleep(const bool enabled) {
  uint8_t *raw = ac_.getRaw();
  if (enabled) raw[kArc446A3SleepByte] |= kArc446A3SleepMask;
  else raw[kArc446A3SleepByte] &= ~kArc446A3SleepMask;
}

void DaikinAircon::setArcHealth(const bool enabled) {
  uint8_t *raw = ac_.getRaw();
  if (enabled) raw[kArc446A3HealthByte] |= kArc446A3HealthMask;
  else raw[kArc446A3HealthByte] &= ~kArc446A3HealthMask;
}

void DaikinAircon::setArcClean(const bool enabled) {
  uint8_t *raw = ac_.getRaw();
  if (enabled) raw[kArc446A3CleanByte] |= kArc446A3CleanMask;
  else raw[kArc446A3CleanByte] &= ~kArc446A3CleanMask;
}

bool DaikinAircon::arcSleep() const {
  return (ac_.getRaw()[kArc446A3SleepByte] & kArc446A3SleepMask) != 0;
}

bool DaikinAircon::arcHealth() const {
  return (ac_.getRaw()[kArc446A3HealthByte] & kArc446A3HealthMask) != 0;
}

bool DaikinAircon::arcClean() const {
  return (ac_.getRaw()[kArc446A3CleanByte] & kArc446A3CleanMask) != 0;
}

void DaikinAircon::syncTimerClock(const uint16_t currentMinutes) {
  if (currentMinutes >= kMinutesPerDay) return;
  timerClockKnown_ = true;
  timerClockBaseMinutes_ = currentMinutes;
  timerClockBaseAtMs_ = millis();
}

uint16_t DaikinAircon::timerClockMinutes() const {
  if (timerClockKnown_) {
    const uint32_t elapsedMinutes =
        (millis() - timerClockBaseAtMs_) / 60000UL;
    return static_cast<uint16_t>(
        (timerClockBaseMinutes_ + elapsedMinutes) % kMinutesPerDay);
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 10) && timeinfo.tm_hour >= 0 &&
      timeinfo.tm_hour <= 23 && timeinfo.tm_min >= 0 &&
      timeinfo.tm_min <= 59) {
    return static_cast<uint16_t>(timeinfo.tm_hour * 60 + timeinfo.tm_min);
  }

  const uint16_t remoteMinutes = ac_.getCurrentTime();
  return remoteMinutes < kMinutesPerDay ? remoteMinutes : 0;
}

uint16_t DaikinAircon::timerTargetFromDuration(
    const uint16_t currentMinutes, const uint16_t durationMinutes) const {
  return static_cast<uint16_t>((currentMinutes + durationMinutes) %
                               kMinutesPerDay);
}

uint16_t DaikinAircon::timerDurationFromTarget(
    const bool enabled, const uint16_t targetMinutes) const {
  if (!enabled) return 0;
  const uint16_t currentMinutes = timerClockMinutes();
  return static_cast<uint16_t>((targetMinutes + kMinutesPerDay -
                                currentMinutes) % kMinutesPerDay);
}

void DaikinAircon::syncNormalTemperatureFromAc() {
  if (ac_.getMode() == kDaikinDry || ac_.getMode() == kDaikinAuto) return;
  const int temperature = static_cast<int>(ac_.getTemp());
  if (temperature >= kDaikinMinTemp && temperature <= kDaikinMaxTemp) {
    normalTemperature_ = static_cast<uint8_t>(temperature);
  }
}

bool DaikinAircon::parseTimer(
    const String &json, bool &hasOn, bool &onEnabled, uint16_t &onMinutes,
    bool &hasOff, bool &offEnabled, uint16_t &offMinutes, String &errorCode,
    String &errorMessage) const {
  HomeJson::Object timer;
  String parseError;
  if (!timer.parse(json, parseError) || timer.size() == 0) {
    errorCode = "invalid_timer";
    errorMessage = "timer must contain on and/or off";
    return false;
  }
  for (size_t i = 0; i < timer.size(); i++) {
    const String &key = timer.keyAt(i);
    if (key != "on" && key != "off") {
      errorCode = "invalid_timer";
      errorMessage = "timer only supports on and off";
      return false;
    }

    String stringValue;
    bool booleanValue = false;
    String objectValue;
    int32_t value = 0;
    if (timer.getString(key.c_str(), stringValue) ||
        timer.getBoolean(key.c_str(), booleanValue)) {
      errorCode = "invalid_timer";
      errorMessage = "timer values must be null or integer minutes";
      return false;
    }
    if (timer.getObjectJson(key.c_str(), objectValue)) {
      errorCode = "invalid_timer";
      errorMessage = "timer values must be null or integer minutes";
      return false;
    }
    const bool isInteger = timer.getInteger(key.c_str(), value);
    if (isInteger && (value < 0 || value > kMaxTimerMinutes)) {
      errorCode = "invalid_timer";
      errorMessage = "timer must be between 0 and 720 minutes";
      return false;
    }
    if (!isInteger && !timer.has(key.c_str())) {
      errorCode = "invalid_timer";
      errorMessage = "timer values must be null or integer minutes";
      return false;
    }

    const bool enabled = isInteger;
    if (key == "on") {
      hasOn = true;
      onEnabled = enabled;
      onMinutes = static_cast<uint16_t>(value);
    } else {
      hasOff = true;
      offEnabled = enabled;
      offMinutes = static_cast<uint16_t>(value);
    }
  }
  return true;
}

IrSendResult DaikinAircon::sendCurrentState() {
  const IrSendResult result = ir_.sendDaikin(0);
  if (result == IrSendResult::Ok) stateSource_ = DaikinStateSource::Transmitted;
  return result;
}

IrSendResult DaikinAircon::applyPatch(const HomeJson::Object &object,
                                      String &errorCode,
                                      String &errorMessage) {
  if (object.size() == 0) {
    errorCode = "empty_patch";
    errorMessage = "at least one field is required";
    return IrSendResult::InvalidCode;
  }
  for (size_t i = 0; i < object.size(); i++) {
    const String &key = object.keyAt(i);
    if (key != "power" && key != "mode" && key != "temperature" &&
        key != "dry_offset" && key != "auto_offset" && key != "fan" &&
        key != "swing" && key != "sleep" && key != "health" &&
        key != "comfort" && key != "clean" && key != "quiet" &&
        key != "timer") {
      errorCode = "unknown_field";
      errorMessage = "aircon field is not supported";
      return IrSendResult::InvalidCode;
    }
  }

  bool nextPower = power_;
  uint8_t nextMode = mode_;
  uint8_t nextFan = fan_;
  int32_t temperature = 0;
  int32_t dryOffset = 0;
  int32_t autoOffset = 0;
  bool nextSwing = ac_.getSwingVertical();
  bool nextSleep = arcSleep();
  bool nextHealth = arcHealth();
  bool nextComfort = ac_.getComfort();
  bool nextClean = arcClean();
  bool nextQuiet = ac_.getQuiet();
  const bool hasTemperature = object.has("temperature");
  const bool hasDryOffset = object.has("dry_offset");
  const bool hasAutoOffset = object.has("auto_offset");
  const bool hasFan = object.has("fan");

  bool value = false;
  if (object.has("power")) {
    if (!object.getBoolean("power", value)) {
      errorCode = "invalid_power";
      errorMessage = "power must be a boolean";
      return IrSendResult::InvalidCode;
    }
    nextPower = value;
  }

  String mode;
  if (object.has("mode")) {
    if (!object.getString("mode", mode)) {
      errorCode = "invalid_mode";
      errorMessage = "mode must be a string";
      return IrSendResult::InvalidCode;
    }
    if (mode == "auto") nextMode = kDaikinAuto;
    else if (mode == "cool") nextMode = kDaikinCool;
    else if (mode == "heat") nextMode = kDaikinHeat;
    else if (mode == "dry") nextMode = kDaikinDry;
    else if (mode == "fan") nextMode = kDaikinFan;
    else {
      errorCode = "invalid_mode";
      errorMessage = "mode must be auto, cool, heat, dry, or fan";
      return IrSendResult::InvalidCode;
    }
    if (!object.has("power")) nextPower = true;
  }

  if (hasTemperature &&
      (!object.getInteger("temperature", temperature) ||
       temperature < kDaikinMinTemp || temperature > kDaikinMaxTemp)) {
    errorCode = "invalid_temperature";
    errorMessage = "temperature must be between 10 and 32";
    return IrSendResult::InvalidCode;
  }
  if (hasDryOffset &&
      (!object.getInteger("dry_offset", dryOffset) || dryOffset < -2 ||
       dryOffset > 2)) {
    errorCode = "invalid_dry_offset";
    errorMessage = "dry_offset must be between -2 and 2";
    return IrSendResult::InvalidCode;
  }
  if (hasAutoOffset &&
      (!object.getInteger("auto_offset", autoOffset) || autoOffset < -5 ||
       autoOffset > 5)) {
    errorCode = "invalid_auto_offset";
    errorMessage = "auto_offset must be between -5 and 5";
    return IrSendResult::InvalidCode;
  }

  if (hasFan) {
    String fan;
    if (object.getString("fan", fan)) {
      if (fan == "auto") nextFan = kDaikinFanAuto;
      else if (fan == "quiet") nextFan = kDaikinFanQuiet;
      else if (fan == "1") nextFan = 1;
      else if (fan == "2") nextFan = 2;
      else if (fan == "3") nextFan = 3;
      else if (fan == "4") nextFan = 4;
      else if (fan == "5") nextFan = 5;
      else {
        errorCode = "invalid_fan";
        errorMessage = "fan must be auto, quiet, or 1 through 5";
        return IrSendResult::InvalidCode;
      }
    } else {
      int32_t numericFan = 0;
      if (!object.getInteger("fan", numericFan) || numericFan < 1 ||
          numericFan > 5) {
        errorCode = "invalid_fan";
        errorMessage = "fan must be auto, quiet, or 1 through 5";
        return IrSendResult::InvalidCode;
      }
      nextFan = static_cast<uint8_t>(numericFan);
    }
  }

  auto parseBooleanField = [&](const char *key, bool &target,
                               const char *fieldName) -> bool {
    if (!object.has(key)) return true;
    if (!object.getBoolean(key, target)) {
      errorCode = String("invalid_") + fieldName;
      errorMessage = String(fieldName) + " must be a boolean";
      return false;
    }
    return true;
  };
  if (!parseBooleanField("swing", nextSwing, "swing") ||
      !parseBooleanField("sleep", nextSleep, "sleep") ||
      !parseBooleanField("health", nextHealth, "health") ||
      !parseBooleanField("comfort", nextComfort, "comfort") ||
      !parseBooleanField("clean", nextClean, "clean") ||
      !parseBooleanField("quiet", nextQuiet, "quiet")) {
    return IrSendResult::InvalidCode;
  }

  bool hasTimerOn = false;
  bool timerOnEnabled = false;
  uint16_t timerOn = 0;
  bool hasTimerOff = false;
  bool timerOffEnabled = false;
  uint16_t timerOff = 0;
  if (object.has("timer")) {
    String timerJson;
    if (!object.getObjectJson("timer", timerJson) ||
        !parseTimer(timerJson, hasTimerOn, timerOnEnabled, timerOn,
                    hasTimerOff, timerOffEnabled, timerOff, errorCode,
                    errorMessage)) {
      if (errorCode.isEmpty()) {
        errorCode = "invalid_timer";
        errorMessage = "timer must be an object";
      }
      return IrSendResult::InvalidCode;
    }
  }

  const bool requestedDry = nextMode == kDaikinDry;
  const bool requestedAuto = nextMode == kDaikinAuto;
  if (hasTemperature && (requestedDry || requestedAuto)) {
    errorCode = requestedDry ? "invalid_dry_temperature"
                             : "invalid_auto_temperature";
    errorMessage = requestedDry
                       ? "temperature is not available in dry mode; use dry_offset"
                       : "temperature is not available in auto mode; use auto_offset";
    return IrSendResult::InvalidCode;
  }
  if (hasDryOffset && !requestedDry) {
    errorCode = "invalid_dry_offset";
    errorMessage = "dry_offset is only available in dry mode";
    return IrSendResult::InvalidCode;
  }
  if (hasAutoOffset && !requestedAuto) {
    errorCode = "invalid_auto_offset";
    errorMessage = "auto_offset is only available in auto mode";
    return IrSendResult::InvalidCode;
  }
  if (hasFan && (nextHealth || nextComfort)) {
    errorCode = "fan_locked_by_feature";
    errorMessage = "fan cannot be changed while health or comfort is enabled";
    return IrSendResult::InvalidCode;
  }

  uint8_t previousState[kDaikinStateLength];
  memcpy(previousState, ac_.getRaw(), kDaikinStateLength);
  const uint8_t previousNormalTemperature = normalTemperature_;
  const DaikinStateSource previousSource = stateSource_;
  const uint8_t previousMode = ac_.getMode();
  const uint16_t currentMinutes = timerClockMinutes();
  syncTimerClock(currentMinutes);
  ac_.setCurrentTime(currentMinutes);

  if (object.has("mode")) {
    ac_.on();
    ac_.setMode(nextMode);
  }
  if (object.has("power")) ac_.setPower(nextPower);
  if (!requestedDry && !requestedAuto && !hasTemperature) {
    ac_.setTemp(normalTemperature_);
  }
  if (hasTemperature) {
    ac_.setTemp(static_cast<float>(temperature));
    normalTemperature_ = static_cast<uint8_t>(temperature);
  }
  if (requestedAuto) {
    if (hasAutoOffset) {
      const uint8_t rawValues[] = {0xD6, 0xD8, 0xDA, 0xDC, 0xDE,
                                   0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCA};
      ac_.getRaw()[kDaikinSpecialTemperatureByte] =
          rawValues[static_cast<size_t>(autoOffset + 5)];
    } else if (object.has("mode") && previousMode != kDaikinAuto) {
      ac_.getRaw()[kDaikinSpecialTemperatureByte] = 0xC0;
    }
  } else if (requestedDry) {
    if (hasDryOffset) {
      const uint8_t rawValues[] = {0xDC, 0xDE, 0xC0, 0xC2, 0xC4};
      ac_.getRaw()[kDaikinSpecialTemperatureByte] =
          rawValues[static_cast<size_t>(dryOffset + 2)];
    } else if (object.has("mode") && previousMode != kDaikinDry) {
      ac_.getRaw()[kDaikinSpecialTemperatureByte] = 0xC0;
    }
  }
  if (hasFan) ac_.setFan(nextFan);
  if (object.has("swing")) ac_.setSwingVertical(nextSwing);
  if (object.has("sleep")) setArcSleep(nextSleep);
  if (object.has("health")) setArcHealth(nextHealth);
  if (object.has("comfort")) ac_.setComfort(nextComfort);
  if (object.has("clean")) setArcClean(nextClean);
  if (object.has("quiet")) ac_.setQuiet(nextQuiet);
  if (hasTimerOn) {
    if (timerOnEnabled) {
      ac_.enableOnTimer(timerTargetFromDuration(currentMinutes, timerOn));
    } else {
      ac_.disableOnTimer();
    }
  }
  if (hasTimerOff) {
    if (timerOffEnabled) {
      ac_.enableOffTimer(timerTargetFromDuration(currentMinutes, timerOff));
    } else {
      ac_.disableOffTimer();
    }
  }

  const IrSendResult result = sendCurrentState();
  if (result != IrSendResult::Ok) {
    ac_.setRaw(previousState, kDaikinStateLength);
    normalTemperature_ = previousNormalTemperature;
    stateSource_ = previousSource;
    return result;
  }
  power_ = ac_.getPower();
  mode_ = ac_.getMode();
  fan_ = ac_.getFan();
  return result;
}

bool DaikinAircon::handleReceived(const uint8_t *state, const size_t length,
                                  const uint32_t receivedAtMs) {
  if (state == nullptr || length < kDaikinStateLength) return false;
  memcpy(lastReceivedState_, state, kDaikinStateLength);
  ac_.setRaw(lastReceivedState_, kDaikinStateLength);
  syncNormalTemperatureFromAc();
  syncTimerClock(ac_.getCurrentTime());
  power_ = ac_.getPower();
  mode_ = ac_.getMode();
  fan_ = ac_.getFan();
  hasReceivedState_ = true;
  lastReceivedAtMs_ = receivedAtMs;
  stateSource_ = DaikinStateSource::Received;
  return true;
}

IrSendResult DaikinAircon::replay() {
  if (!hasReceivedState_) return IrSendResult::NotConfigured;
  uint8_t previousState[kDaikinStateLength];
  memcpy(previousState, ac_.getRaw(), kDaikinStateLength);
  const uint8_t previousNormalTemperature = normalTemperature_;
  const DaikinStateSource previousSource = stateSource_;

  ac_.setRaw(lastReceivedState_, kDaikinStateLength);
  syncNormalTemperatureFromAc();
  syncTimerClock(ac_.getCurrentTime());
  const IrSendResult result = sendCurrentState();
  if (result != IrSendResult::Ok) {
    ac_.setRaw(previousState, kDaikinStateLength);
    normalTemperature_ = previousNormalTemperature;
    stateSource_ = previousSource;
  }
  return result;
}

String DaikinAircon::stateJson() const {
  const uint8_t rawTemperature = ac_.getRaw()[kDaikinSpecialTemperatureByte];
  String json;
  json.reserve(620);
  json += "{\"power\":";
  json += ac_.getPower() ? "true" : "false";
  json += ",\"mode\":\"";
  json += modeName(ac_.getMode());
  json += "\",\"temperature\":";
  if (ac_.getMode() == kDaikinDry || ac_.getMode() == kDaikinAuto) {
    json += "null";
  } else {
    json += String(static_cast<int>(ac_.getTemp()));
  }
  json += ",\"dry_offset\":";
  if (ac_.getMode() == kDaikinDry) {
    switch (rawTemperature) {
      case 0xDC: json += "-2"; break;
      case 0xDE: json += "-1"; break;
      case 0xC0: json += "0"; break;
      case 0xC2: json += "1"; break;
      case 0xC4: json += "2"; break;
      default: json += "null"; break;
    }
  } else {
    json += "null";
  }
  json += ",\"auto_offset\":";
  if (ac_.getMode() == kDaikinAuto) {
    switch (rawTemperature) {
      case 0xD6: json += "-5"; break;
      case 0xD8: json += "-4"; break;
      case 0xDA: json += "-3"; break;
      case 0xDC: json += "-2"; break;
      case 0xDE: json += "-1"; break;
      case 0xC0: json += "0"; break;
      case 0xC2: json += "1"; break;
      case 0xC4: json += "2"; break;
      case 0xC6: json += "3"; break;
      case 0xC8: json += "4"; break;
      case 0xCA: json += "5"; break;
      default: json += "null"; break;
    }
  } else {
    json += "null";
  }
  json += ",\"fan\":\"";
  json += fanName(ac_.getFan());
  json += "\",\"swing\":";
  json += ac_.getSwingVertical() ? "true" : "false";
  json += ",\"sleep\":";
  json += arcSleep() ? "true" : "false";
  json += ",\"health\":";
  json += arcHealth() ? "true" : "false";
  json += ",\"comfort\":";
  json += ac_.getComfort() ? "true" : "false";
  json += ",\"clean\":";
  json += arcClean() ? "true" : "false";
  json += ",\"quiet\":";
  json += ac_.getQuiet() ? "true" : "false";
  json += ",\"timer\":{\"on\":";
  if (ac_.getOnTimerEnabled()) {
    json += String(timerDurationFromTarget(true, ac_.getOnTime()));
  } else {
    json += "null";
  }
  json += ",\"off\":";
  if (ac_.getOffTimerEnabled()) {
    json += String(timerDurationFromTarget(true, ac_.getOffTime()));
  } else {
    json += "null";
  }
  json += "},\"state_source\":\"";
  json += stateSourceName(stateSource_);
  json += "\"}";
  return json;
}

String DaikinAircon::receivedJson() const {
  if (!hasReceivedState_) return "{\"received\":false}";
  String json =
      "{\"received\":true,\"protocol\":\"DAIKIN\",\"bits\":280,\"received_at_ms\":";
  json += String(lastReceivedAtMs_);
  json += ",\"state_loaded\":true}";
  return json;
}
