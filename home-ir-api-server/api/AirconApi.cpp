#include "AirconApi.h"

#include <WebServer.h>

#include "../devices/daikin/DaikinAircon.h"
#include "ApiResponse.h"

namespace {
constexpr size_t kAirconMaxBodyLength = 2048;
}  // namespace

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
    HomeApi::sendJsonError(server_, 500, "ir_send_failed", "IR transmission failed");
    return;
  }
  server_.send(200, "application/json", "{\"ok\":true,\"device\":\"aircon\",\"command\":\"off\"}");
}

void AirconApi::handleStatePost() {
  if (!server_.hasArg("plain")) {
    HomeApi::sendJsonError(server_, 400, "invalid_json", "request body is required");
    return;
  }
  const String body = server_.arg("plain");
  if (body.length() > kAirconMaxBodyLength) {
    HomeApi::sendJsonError(server_, 413, "request_too_large", "request body is too large");
    return;
  }
  HomeJson::Object object;
  String parseError;
  if (!object.parse(body, parseError)) {
    HomeApi::sendJsonError(server_, 400, "invalid_json", parseError);
    return;
  }
  String errorCode;
  String errorMessage;
  if (!aircon_.applyPatch(object, errorCode, errorMessage)) {
    HomeApi::sendJsonError(server_, 422, errorCode.c_str(), errorMessage);
    return;
  }
  server_.send(200, "application/json", aircon_.stateJson());
}
