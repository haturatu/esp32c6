#include "LightApi.h"

#include <WebServer.h>

#include "../devices/light/CeilingLight.h"
#include "../ir/IrSender.h"
#include "ApiResponse.h"

namespace {
constexpr size_t kLightMaxBodyLength = 256;
}  // namespace

LightApi::LightApi(WebServer &server, CeilingLight &light)
    : server_(server), light_(light) {}

void LightApi::begin() {
  server_.on("/api/v1/light/command", HTTP_POST,
             [this]() { handleCommandRequest(); });

  const char *const names[] = {
      "on",         "off",       "full",       "brighter",
      "dimmer",     "cooler",    "warmer",     "toggle",
      "night-light", "cancel",    "timer-15m",  "timer-30m",
  };
  const char *const internalNames[] = {
      "on",       "off",      "full",      "brighter",
      "dimmer",   "cooler",   "warmer",    "toggle",
      "night_light", "cancel", "timer_15m", "timer_30m",
  };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    String path = "/api/v1/light/commands/";
    path += names[i];
    const String command = internalNames[i];
    server_.on(path, HTTP_POST,
               [this, command]() { handleNamedCommand(command); });
  }
}

bool LightApi::parseCommandBody(const String &body, String &command) {
  HomeJson::Object object;
  String parseError;
  if (!object.parse(body, parseError) || object.size() != 1 ||
      object.keyAt(0) != "command" || !object.getString("command", command)) {
    return false;
  }
  command.toLowerCase();
  return !command.isEmpty();
}

void LightApi::handleCommandRequest() {
  if (!server_.hasArg("plain")) {
    HomeApi::sendJsonError(server_, 400, "invalid_json", "request body is required");
    return;
  }
  const String body = server_.arg("plain");
  if (body.length() > kLightMaxBodyLength) {
    HomeApi::sendJsonError(server_, 413, "request_too_large", "request body is too large");
    return;
  }
  String command;
  if (!parseCommandBody(body, command)) {
    HomeApi::sendJsonError(server_, 400, "invalid_json", "body must contain command");
    return;
  }
  sendCommand(command);
}

void LightApi::handleNamedCommand(const String &name) { sendCommand(name); }

void LightApi::sendCommand(const String &name) {
  LightCommand command;
  if (!CeilingLight::parseCommand(name, command)) {
    HomeApi::sendJsonError(server_, 422, "invalid_command", "light command is not supported");
    return;
  }
  const IrSendResult result = light_.send(command);
  if (result != IrSendResult::Ok) {
    switch (result) {
      case IrSendResult::NotConfigured:
        HomeApi::sendJsonError(server_, 501, "ir_code_not_configured",
                               "IR code has not been captured yet");
        return;
      case IrSendResult::NotInitialized:
        HomeApi::sendJsonError(server_, 500, "ir_sender_not_initialized",
                               "IR sender has not been initialized");
        return;
      case IrSendResult::InvalidCode:
        HomeApi::sendJsonError(server_, 500, "invalid_ir_code", "IR code is invalid");
        return;
      case IrSendResult::SendFailed:
        HomeApi::sendJsonError(server_, 500, "ir_send_failed", "IR transmission failed");
        return;
      case IrSendResult::Ok: break;
    }
    return;
  }

  const IrCode &code = LightCodes::forCommand(command);
  String body = "{\"ok\":true,\"device\":\"light\",\"command\":\"";
  body += CeilingLight::commandName(command);
  body += "\",\"code\":\"";
  body += CeilingLight::codeString(code);
  body += "\",\"ir\":{\"transmitted\":true,\"acknowledged\":false}}";
  server_.send(200, "application/json", body);
}
