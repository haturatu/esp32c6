#pragma once

#include <Arduino.h>

class CeilingLight;
class WebServer;

class LightApi {
 public:
  LightApi(WebServer &server, CeilingLight &light);
  void begin();

 private:
  void handleCommandRequest();
  void handleNamedCommand(const String &name);
  void sendCommand(const String &name);
  static bool parseCommandBody(const String &body, String &command);

  WebServer &server_;
  CeilingLight &light_;
};
