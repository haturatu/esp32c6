#pragma once

#include <Arduino.h>
#include <ir_Daikin.h>

#include "../../api/Json.h"

class IrSender;

class DaikinAircon {
 public:
  explicit DaikinAircon(IrSender &ir);

  void begin();
  bool sendOff();
  bool applyPatch(const HomeJson::Object &object, String &errorCode,
                  String &errorMessage);
  String stateJson() const;

 private:
  static const char *modeName(uint8_t mode);
  IrSender &ir_;
  IRDaikinESP &ac_;
  bool power_;
  uint8_t mode_;
  uint8_t temperature_;
  uint8_t fan_;
  bool stateKnown_;
};
