#include "LightApi.h"

#include <WebServer.h>

#include "../devices/light/CeilingLight.h"

namespace {
constexpr size_t kMaxBodyLength = 256;

void sendError(WebServer &server, const int status, const char *code,
               const char *message) {
  String body = "{\"ok\":false,\"error\":{\"code\":\"";
  body += code;
  body += "\",\"message\":\"";
  body += message;
  body += "\"}}";
  server.send(status, "application/json", body);
}
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
  const int keyStart = body.indexOf("\"command\"");
  if (keyStart < 0) return false;
  const int colon = body.indexOf(':', keyStart + 9);
  if (colon < 0) return false;
  const int start = body.indexOf('"', colon + 1);
  if (start < 0) return false;
  const int end = body.indexOf('"', start + 1);
  if (end < 0) return false;
  command = body.substring(start + 1, end);
  command.toLowerCase();
  return !command.isEmpty();
}

void LightApi::handleCommandRequest() {
  if (!server_.hasArg("plain")) {
    sendError(server_, 400, "invalid_json", "request body is required");
    return;
  }
  const String body = server_.arg("plain");
  if (body.length() > kMaxBodyLength) {
    sendError(server_, 413, "request_too_large", "request body is too large");
    return;
  }
  String command;
  if (!parseCommandBody(body, command)) {
    sendError(server_, 400, "invalid_json", "body must contain command");
    return;
  }
  sendCommand(command);
}

void LightApi::handleNamedCommand(const String &name) { sendCommand(name); }

void LightApi::sendCommand(const String &name) {
  LightCommand command;
  if (!CeilingLight::parseCommand(name, command)) {
    sendError(server_, 422, "invalid_command", "light command is not supported");
    return;
  }
  if (!light_.send(command)) {
    sendError(server_, 501, "ir_code_not_configured",
              "IR code has not been captured yet");
    return;
  }

  String body = "{\"ok\":true,\"device\":\"light\",\"command\":\"";
  body += CeilingLight::commandName(command);
  body += "\",\"code\":\"";
  body += CeilingLight::commandCode(command);
  body += "\",\"ir\":{\"transmitted\":true,\"acknowledged\":false}}";
  server_.send(200, "application/json", body);
}
