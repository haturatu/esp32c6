#pragma once

#include <Arduino.h>

#include "CeilingLightCodes.h"

class IrSender;
enum class IrSendResult : uint8_t;

enum class LightStateSource : uint8_t {
  Initial,
  Transmitted,
  Received,
};

class CeilingLight {
 public:
  explicit CeilingLight(IrSender &ir);

  IrSendResult send(LightCommand command);
  bool handleReceived(uint64_t data, uint16_t bits, uint32_t receivedAtMs);
  String stateJson() const;
  static bool parseCommand(const String &name, LightCommand &command);
  static const char *commandName(LightCommand command);
  static String codeString(const IrCode &code);

 private:
  static const char *stateSourceName(LightStateSource source);

  IrSender &ir_;
  bool hasLastCommand_;
  LightCommand lastCommand_;
  uint64_t lastCode_;
  uint16_t lastBits_;
  uint32_t lastTransmittedAtMs_;
  LightStateSource stateSource_;
};
