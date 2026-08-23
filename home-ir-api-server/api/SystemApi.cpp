#include "SystemApi.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../config.h"

SystemApi::SystemApi(WebServer &server) : server_(server) {}

void SystemApi::begin() {
  server_.on("/api/v1/health", HTTP_GET, [this]() { handleHealth(); });
  server_.on("/api/v1/system/health", HTTP_GET,
             [this]() { handleHealth(); });
  server_.on("/api/v1/system/info", HTTP_GET, [this]() { handleInfo(); });
}

void SystemApi::handleHealth() { server_.send(200, "application/json", "{\"status\":\"ok\"}"); }

void SystemApi::handleInfo() {
  String body =
      "{\"device\":\"esp32c6\",\"model\":\"home-ir-api-server\","
      "\"api_version\":\"v1\",\"ir\":{\"tx_gpio\":";
  body += String(HOME_IR_TX_GPIO);
  body += ",\"rx_gpio\":";
  body += String(HOME_IR_RX_GPIO);
  body += ",\"protocols\":{\"light\":{\"protocol\":\"NEC\",\"bits\":32},";
  body += "\"aircon\":{\"protocol\":\"DAIKIN\",\"bits\":280}}},";
  body += "\"wifi\":{\"rssi\":";
  body += String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  body += "},\"uptime_seconds\":";
  body += String(millis() / 1000);
  body += ",\"free_heap\":";
  body += String(ESP.getFreeHeap());
  body += "}";
  server_.send(200, "application/json", body);
}
