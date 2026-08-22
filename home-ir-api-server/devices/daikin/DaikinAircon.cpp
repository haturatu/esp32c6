#include "DaikinAircon.h"

#include "../../ir/IrSender.h"

namespace {
constexpr uint8_t kDefaultTemperature = 26;
}

DaikinAircon::DaikinAircon(IrSender &ir)
    : ir_(ir),
      ac_(ir.daikin()),
      power_(true),
      mode_(kDaikinCool),
      temperature_(kDefaultTemperature),
      fan_(kDaikinFanAuto),
      stateKnown_(false) {}

void DaikinAircon::begin() {
  ac_.on();
  ac_.setMode(mode_);
  ac_.setTemp(temperature_);
  ac_.setFan(fan_);
}

bool DaikinAircon::sendOff() {
  ac_.off();
  ac_.send();
  power_ = false;
  stateKnown_ = true;
  return true;
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

String DaikinAircon::stateJson() const {
  if (!stateKnown_) {
    return "{\"state\":null,\"state_source\":\"unknown\"}";
  }
  String body = "{\"power\":";
  body += power_ ? "true" : "false";
  body += ",\"mode\":\"";
  body += modeName(mode_);
  body += "\",\"temperature\":";
  body += String(temperature_);
  body += ",\"fan\":\"";
  body += fan_ == kDaikinFanAuto ? "auto" : String(fan_);
  body += "\",\"state_source\":\"transmitted\"}";
  return body;
}

bool DaikinAircon::applyPatch(const HomeJson::Object &object,
                              String &errorCode, String &errorMessage) {
  for (size_t i = 0; i < object.size(); i++) {
    const String &key = object.keyAt(i);
    if (key != "power" && key != "mode" && key != "temperature" &&
        key != "fan") {
      errorCode = "unknown_field";
      errorMessage = "aircon field is not supported";
      return false;
    }
  }
  if (object.size() == 0) {
    errorCode = "empty_patch";
    errorMessage = "at least one field is required";
    return false;
  }

  bool nextPower = power_;
  uint8_t nextMode = mode_;
  uint8_t nextTemperature = temperature_;
  uint8_t nextFan = fan_;
  bool value = false;
  if (object.has("power")) {
    if (!object.getBoolean("power", value)) {
      errorCode = "invalid_power";
      errorMessage = "power must be a boolean";
      return false;
    }
    nextPower = value;
  }

  String mode;
  if (object.has("mode")) {
    if (!object.getString("mode", mode)) {
      errorCode = "invalid_mode";
      errorMessage = "mode must be a string";
      return false;
    }
    if (mode == "auto") nextMode = kDaikinAuto;
    else if (mode == "cool") nextMode = kDaikinCool;
    else if (mode == "heat") nextMode = kDaikinHeat;
    else if (mode == "dry") nextMode = kDaikinDry;
    else if (mode == "fan") nextMode = kDaikinFan;
    else {
      errorCode = "invalid_mode";
      errorMessage = "mode must be auto, cool, heat, dry, or fan";
      return false;
    }
    nextPower = true;
  }

  int32_t temperature = 0;
  if (object.has("temperature")) {
    if (!object.getInteger("temperature", temperature) ||
        temperature < kDaikinMinTemp || temperature > kDaikinMaxTemp) {
      errorCode = "invalid_temperature";
      errorMessage = "temperature must be between 10 and 32";
      return false;
    }
    nextTemperature = static_cast<uint8_t>(temperature);
  }

  if (object.has("fan")) {
    String fan;
    if (object.getString("fan", fan)) {
      if (fan == "auto") nextFan = kDaikinFanAuto;
      else if (fan == "quiet") nextFan = kDaikinFanQuiet;
      else {
        if (fan == "1") nextFan = 1;
        else if (fan == "2") nextFan = 2;
        else if (fan == "3") nextFan = 3;
        else if (fan == "4") nextFan = 4;
        else if (fan == "5") nextFan = 5;
        else {
          errorCode = "invalid_fan";
          errorMessage = "fan must be auto, quiet, or 1 through 5";
          return false;
        }
      }
    } else {
      int32_t speed = 0;
      if (!object.getInteger("fan", speed) || speed < kDaikinFanMin ||
          speed > kDaikinFanMax) {
        errorCode = "invalid_fan";
        errorMessage = "fan must be auto, quiet, or 1 through 5";
        return false;
      }
      nextFan = speed;
    }
  }

  if (nextPower) ac_.on();
  else ac_.off();
  ac_.setMode(nextMode);
  ac_.setTemp(nextTemperature);
  ac_.setFan(nextFan);
  ac_.send();
  power_ = nextPower;
  mode_ = nextMode;
  temperature_ = nextTemperature;
  fan_ = nextFan;
  stateKnown_ = true;
  return true;
}
