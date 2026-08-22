#pragma once

#include <Arduino.h>

#include "CeilingLightCodes.h"

class IrSender;

class CeilingLight {
 public:
  explicit CeilingLight(IrSender &ir);

  bool send(LightCommand command);
  static bool parseCommand(const String &name, LightCommand &command);
  static const char *commandName(LightCommand command);
  static const char *commandCode(LightCommand command);

 private:
  IrSender &ir_;
};
