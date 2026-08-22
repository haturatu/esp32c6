#pragma once

#include <Arduino.h>
#include <ir_Daikin.h>

class DaikinAircon {
 public:
  explicit DaikinAircon(uint8_t txPin);

  void begin();
  bool sendOff();
  bool applySimplePatch(const String &body);
  String stateJson() const;

 private:
  static const char *modeName(uint8_t mode);
  IRDaikinESP ac_;
  bool power_;
  uint8_t mode_;
  uint8_t temperature_;
  uint8_t fan_;
};
