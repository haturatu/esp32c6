#pragma once

class DaikinAircon;
class WebServer;
enum class IrSendResult : unsigned char;

class AirconApi {
 public:
  AirconApi(WebServer &server, DaikinAircon &aircon);
  void begin();

 private:
  void handleStateGet();
  void handleStatePatch();
  void handleOff();
  void handleReceived();
  void handleReplay();
  void handleDiagnostics();
  void handlePatchBody();
  void sendTransmissionError(IrSendResult result);

  WebServer &server_;
  DaikinAircon &aircon_;
};
