#pragma once

class DaikinAircon;
class WebServer;

class AirconApi {
 public:
  AirconApi(WebServer &server, DaikinAircon &aircon);
  void begin();

 private:
  void handleState();
  void handleOff();
  void handleStatePost();
  WebServer &server_;
  DaikinAircon &aircon_;
};
