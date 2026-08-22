#include "CeilingLight.h"

#include "../../ir/IrSender.h"

CeilingLight::CeilingLight(IrSender &ir) : ir_(ir) {}

IrSendResult CeilingLight::send(const LightCommand command) {
  const IrCode &code = LightCodes::forCommand(command);
  if (!code.configured) return IrSendResult::NotConfigured;
  if (code.protocol == IrProtocol::Nec) {
    return ir_.sendNec(code.data, code.bits, 0);
  }
  if (code.protocol == IrProtocol::Raw) {
    return ir_.sendRaw(code.timings, code.length, code.frequency);
  }
  return IrSendResult::InvalidCode;
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

String CeilingLight::codeString(const IrCode &code) {
  if (!code.configured) return "null";
  String output = "0x";
  String hex = String(static_cast<uint32_t>(code.data), HEX);
  hex.toUpperCase();
  while (hex.length() < 8) hex = "0" + hex;
  output += hex;
  return output;
}
