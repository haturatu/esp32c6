#pragma once

class WebServer;

class SystemApi {
 public:
  explicit SystemApi(WebServer &server);
  void begin();

 private:
  void handleHealth();
  void handleInfo();
  WebServer &server_;
};
