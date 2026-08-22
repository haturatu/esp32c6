#include "DaikinAircon.h"

namespace {
constexpr uint8_t kDefaultTemperature = 26;
}

DaikinAircon::DaikinAircon(const uint8_t txPin)
    : ac_(txPin),
      power_(true),
      mode_(kDaikinCool),
      temperature_(kDefaultTemperature),
      fan_(kDaikinFanAuto) {}

void DaikinAircon::begin() {
  ac_.begin();
  ac_.on();
  ac_.setMode(mode_);
  ac_.setTemp(temperature_);
  ac_.setFan(fan_);
}

bool DaikinAircon::sendOff() {
  ac_.off();
  ac_.send();
  power_ = false;
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

bool DaikinAircon::applySimplePatch(const String &body) {
  bool changed = false;
  if (body.indexOf("\"power\":false") >= 0) {
    ac_.off();
    power_ = false;
    changed = true;
  } else if (body.indexOf("\"power\":true") >= 0) {
    ac_.on();
    power_ = true;
    changed = true;
  }

  const int modeKey = body.indexOf("\"mode\":\"");
  if (modeKey >= 0) {
    const int start = modeKey + 9;
    const int end = body.indexOf('"', start);
    if (end < 0) return false;
    const String mode = body.substring(start, end);
    if (mode == "auto") mode_ = kDaikinAuto;
    else if (mode == "cool") mode_ = kDaikinCool;
    else if (mode == "heat") mode_ = kDaikinHeat;
    else if (mode == "dry") mode_ = kDaikinDry;
    else if (mode == "fan") mode_ = kDaikinFan;
    else return false;
    ac_.on();
    ac_.setMode(mode_);
    power_ = true;
    changed = true;
  }

  const int tempKey = body.indexOf("\"temperature\":");
  if (tempKey >= 0) {
    int value = body.substring(tempKey + 14).toInt();
    if (value < kDaikinMinTemp || value > kDaikinMaxTemp) return false;
    temperature_ = value;
    ac_.setTemp(temperature_);
    changed = true;
  }

  if (!changed) return false;
  ac_.send();
  return true;
}
