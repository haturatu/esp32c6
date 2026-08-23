#include "AirconApi.h"

#include <WebServer.h>

#include "../devices/daikin/DaikinAircon.h"
#include "../ir/IrSender.h"
#include "ApiResponse.h"

namespace {
constexpr size_t kAirconMaxBodyLength = 2048;
}

AirconApi::AirconApi(WebServer &server, DaikinAircon &aircon)
    : server_(server), aircon_(aircon) {}

void AirconApi::begin() {
  server_.on("/api/v1/ac/state", HTTP_GET, [this]() { handleStateGet(); });
  server_.on("/api/v1/ac/state", HTTP_PATCH,
             [this]() { handleStatePatch(); });
  server_.on("/api/v1/ac/state", HTTP_POST,
             [this]() { handleStatePatch(); });
  server_.on("/api/v1/aircon/state", HTTP_GET,
             [this]() { handleStateGet(); });
  server_.on("/api/v1/aircon/state", HTTP_PATCH,
             [this]() { handleStatePatch(); });
  server_.on("/api/v1/aircon/state", HTTP_POST,
             [this]() { handleStatePatch(); });
  server_.on("/api/v1/ac/off", HTTP_POST, [this]() { handleOff(); });
  server_.on("/api/v1/aircon/off", HTTP_POST, [this]() { handleOff(); });

  server_.on("/api/v1/ir/received", HTTP_GET,
             [this]() { handleReceived(); });
  server_.on("/api/v1/ir/replay", HTTP_POST, [this]() { handleReplay(); });
  server_.on("/api/v1/ir/diagnostics/send", HTTP_POST,
             [this]() { handleDiagnostics(); });
}

void AirconApi::handleStateGet() {
  server_.send(200, "application/json", aircon_.stateJson());
}

void AirconApi::handleStatePatch() { handlePatchBody(); }

void AirconApi::handleOff() {
  const IrSendResult result = aircon_.sendOff();
  if (result != IrSendResult::Ok) {
    sendTransmissionError(result);
    return;
  }
  String response = "{\"ok\":true,\"device\":\"aircon\",\"command\":\"off\",\"state\":";
  response += aircon_.stateJson();
  response += "}";
  server_.send(200, "application/json", response);
}

void AirconApi::handleReceived() {
  server_.send(200, "application/json", aircon_.receivedJson());
}

void AirconApi::handleReplay() {
  const IrSendResult result = aircon_.replay();
  if (result == IrSendResult::NotConfigured) {
    HomeApi::sendJsonError(server_, 409, "no_received_ir_state",
                           "No DAIKIN IR frame has been received");
    return;
  }
  if (result != IrSendResult::Ok) {
    sendTransmissionError(result);
    return;
  }
  server_.send(200, "application/json",
               "{\"ok\":true,\"ir\":{\"transmitted\":true,\"acknowledged\":false}}");
}

void AirconApi::handleDiagnostics() {
  HomeApi::sendJsonError(server_, 404, "not_found",
                         "IR diagnostics are disabled");
}

void AirconApi::handlePatchBody() {
  if (!server_.hasArg("plain")) {
    HomeApi::sendJsonError(server_, 400, "invalid_json",
                           "request body is required");
    return;
  }
  const String body = server_.arg("plain");
  if (body.length() > kAirconMaxBodyLength) {
    HomeApi::sendJsonError(server_, 413, "request_too_large",
                           "request body must be 2048 bytes or less");
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
  const IrSendResult result =
      aircon_.applyPatch(object, errorCode, errorMessage);
  if (result != IrSendResult::Ok) {
    if (!errorCode.isEmpty()) {
      HomeApi::sendJsonError(server_, 422, errorCode.c_str(), errorMessage);
    } else {
      sendTransmissionError(result);
    }
    return;
  }
  String response = "{\"ok\":true,\"state\":";
  response += aircon_.stateJson();
  response += "}";
  server_.send(200, "application/json", response);
}

void AirconApi::sendTransmissionError(const IrSendResult result) {
  switch (result) {
    case IrSendResult::NotInitialized:
      HomeApi::sendJsonError(server_, 500, "ir_sender_not_initialized",
                             "IR sender has not been initialized");
      return;
    case IrSendResult::RateLimited:
      HomeApi::sendJsonError(server_, 429, "ir_rate_limited",
                             "IR transmission rate limit exceeded");
      return;
    case IrSendResult::InvalidCode:
      HomeApi::sendJsonError(server_, 500, "invalid_ir_code",
                             "IR code is invalid");
      return;
    case IrSendResult::SendFailed:
      HomeApi::sendJsonError(server_, 500, "ir_send_failed",
                             "IR transmission failed");
      return;
    case IrSendResult::NotConfigured:
      HomeApi::sendJsonError(server_, 409, "no_received_ir_state",
                             "No DAIKIN IR frame has been received");
      return;
    case IrSendResult::Ok:
      return;
  }
}
