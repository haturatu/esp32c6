#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "Json.h"

namespace HomeApi {

inline void sendJsonError(WebServer &server, const int status, const char *code,
                          const String &message) {
  server.send(status, "application/json", HomeJson::errorJson(code, message));
}

}  // namespace HomeApi
