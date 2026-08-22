#include "AirconApi.h"

#include <WebServer.h>

#include "../devices/daikin/DaikinAircon.h"

AirconApi::AirconApi(WebServer &server, DaikinAircon &aircon)
    : server_(server), aircon_(aircon) {}

void AirconApi::begin() {
  server_.on("/api/v1/aircon/state", HTTP_GET,
             [this]() { handleState(); });
  server_.on("/api/v1/aircon/state", HTTP_POST,
             [this]() { handleStatePost(); });
  server_.on("/api/v1/aircon/off", HTTP_POST, [this]() { handleOff(); });
}

void AirconApi::handleState() {
  server_.send(200, "application/json", aircon_.stateJson());
}

void AirconApi::handleOff() {
  if (!aircon_.sendOff()) {
    server_.send(500, "application/json",
                 "{\"ok\":false,\"error\":{\"code\":\"ir_send_failed\"}}");
    return;
  }
  server_.send(200, "application/json", "{\"ok\":true,\"device\":\"aircon\",\"command\":\"off\"}");
}

void AirconApi::handleStatePost() {
  if (!server_.hasArg("plain")) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":{\"code\":\"invalid_json\"}}");
    return;
  }
  if (!aircon_.applySimplePatch(server_.arg("plain"))) {
    server_.send(422, "application/json", "{\"ok\":false,\"error\":{\"code\":\"invalid_state\"}}");
    return;
  }
  server_.send(200, "application/json", aircon_.stateJson());
}
