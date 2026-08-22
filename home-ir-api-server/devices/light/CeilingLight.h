#pragma once

#include <Arduino.h>

#include "CeilingLightCodes.h"

class IrSender;
enum class IrSendResult : uint8_t;

class CeilingLight {
 public:
  explicit CeilingLight(IrSender &ir);

  IrSendResult send(LightCommand command);
  static bool parseCommand(const String &name, LightCommand &command);
  static const char *commandName(LightCommand command);
  static String codeString(const IrCode &code);

 private:
  IrSender &ir_;
};
