#include "CeilingLight.h"

#include "../../ir/IrSender.h"

CeilingLight::CeilingLight(IrSender &ir)
    : ir_(ir),
      hasLastCommand_(false),
      lastCommand_(LightCommand::PowerOn),
      lastCode_(0),
      lastBits_(32),
      lastTransmittedAtMs_(0),
      stateSource_(LightStateSource::Initial) {}

IrSendResult CeilingLight::send(const LightCommand command) {
  const IrCode &code = LightCodes::forCommand(command);
  if (!code.configured) return IrSendResult::NotConfigured;

  IrSendResult result = IrSendResult::InvalidCode;
  if (code.protocol == IrProtocol::Nec) {
    result = ir_.sendNec(code.data, code.bits, 0);
  } else if (code.protocol == IrProtocol::Raw) {
    result = ir_.sendRaw(code.timings, code.length, code.frequency);
  }
  if (result == IrSendResult::Ok) {
    hasLastCommand_ = true;
    lastCommand_ = command;
    lastCode_ = code.data;
    lastBits_ = code.bits;
    lastTransmittedAtMs_ = millis();
    stateSource_ = LightStateSource::Transmitted;
  }
  return result;
}

bool CeilingLight::handleReceived(const uint64_t data, const uint16_t bits,
                                  const uint32_t receivedAtMs) {
  if (bits != 32) return false;
  const LightCommand commands[] = {
      LightCommand::PowerOn,    LightCommand::PowerOff,
      LightCommand::Full,       LightCommand::Brighter,
      LightCommand::Dimmer,     LightCommand::Cooler,
      LightCommand::Warmer,     LightCommand::Toggle,
      LightCommand::NightLight, LightCommand::Cancel,
      LightCommand::Timer15Min, LightCommand::Timer30Min,
  };
  for (const LightCommand command : commands) {
    const IrCode &code = LightCodes::forCommand(command);
    if (code.protocol == IrProtocol::Nec && code.bits == bits &&
        code.data == data) {
      hasLastCommand_ = true;
      lastCommand_ = command;
      lastCode_ = data;
      lastBits_ = bits;
      lastTransmittedAtMs_ = receivedAtMs;
      stateSource_ = LightStateSource::Received;
      return true;
    }
  }
  return false;
}

bool CeilingLight::parseCommand(const String &name, LightCommand &command) {
  if (name == "on") command = LightCommand::PowerOn;
  else if (name == "off") command = LightCommand::PowerOff;
  else if (name == "full") command = LightCommand::Full;
  else if (name == "brighter") command = LightCommand::Brighter;
  else if (name == "dimmer") command = LightCommand::Dimmer;
  else if (name == "cooler") command = LightCommand::Cooler;
  else if (name == "warmer") command = LightCommand::Warmer;
  else if (name == "toggle") command = LightCommand::Toggle;
  else if (name == "night_light") command = LightCommand::NightLight;
  else if (name == "cancel") command = LightCommand::Cancel;
  else if (name == "timer_15m") command = LightCommand::Timer15Min;
  else if (name == "timer_30m") command = LightCommand::Timer30Min;
  else return false;
  return true;
}

const char *CeilingLight::commandName(const LightCommand command) {
  switch (command) {
    case LightCommand::PowerOn: return "on";
    case LightCommand::PowerOff: return "off";
    case LightCommand::Full: return "full";
    case LightCommand::Brighter: return "brighter";
    case LightCommand::Dimmer: return "dimmer";
    case LightCommand::Cooler: return "cooler";
    case LightCommand::Warmer: return "warmer";
    case LightCommand::Toggle: return "toggle";
    case LightCommand::NightLight: return "night_light";
    case LightCommand::Cancel: return "cancel";
    case LightCommand::Timer15Min: return "timer_15m";
    case LightCommand::Timer30Min: return "timer_30m";
  }
  return "unknown";
}

const char *CeilingLight::stateSourceName(const LightStateSource source) {
  switch (source) {
    case LightStateSource::Initial: return "initial";
    case LightStateSource::Transmitted: return "transmitted";
    case LightStateSource::Received: return "received";
  }
  return "initial";
}

String CeilingLight::codeString(const IrCode &code) {
  if (!code.configured) return "null";
  String output = "0x";
  String hex = String(static_cast<uint32_t>(code.data), HEX);
  hex.toUpperCase();
  while (hex.length() < 8) hex = "0" + hex;
  output += hex;
  return output;
}

String CeilingLight::stateJson() const {
  String json = "{\"last_command\":";
  if (hasLastCommand_) {
    json += "\"";
    json += commandName(lastCommand_);
    json += "\"";
  } else {
    json += "null";
  }
  json += ",\"last_code\":";
  if (hasLastCommand_) {
    json += "\"";
    String hex = String(static_cast<uint32_t>(lastCode_), HEX);
    hex.toUpperCase();
    while (hex.length() < 8) hex = "0" + hex;
    json += "0x";
    json += hex;
    json += "\"";
  } else {
    json += "null";
  }
  json += ",\"protocol\":\"NEC\",\"bits\":";
  json += String(lastBits_);
  json += ",\"last_transmitted_at_ms\":";
  json += String(lastTransmittedAtMs_);
  json += ",\"state_source\":\"";
  json += stateSourceName(stateSource_);
  json += "\"}";
  return json;
}
