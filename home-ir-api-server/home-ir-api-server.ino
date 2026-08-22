#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "api/AirconApi.h"
#include "api/LightApi.h"
#include "api/SystemApi.h"
#include "devices/daikin/DaikinAircon.h"
#include "devices/light/CeilingLight.h"
#include "ir/IrSender.h"

WebServer server(HOME_IR_HTTP_PORT);
Preferences preferences;
IrSender irSender(HOME_IR_TX_GPIO);
DaikinAircon aircon(HOME_IR_TX_GPIO);
CeilingLight light(irSender);
LightApi lightApi(server, light);
AirconApi airconApi(server, aircon);
SystemApi systemApi(server);

String wifiSsid;
String wifiPassword;
String serialLine;

void saveWifiCredentials(const String &ssid, const String &password) {
  preferences.begin("secrets", false);
  preferences.putString("ssid", ssid);
  preferences.putString("wifi_pass", password);
  preferences.end();
}

void loadWifiCredentials() {
  preferences.begin("secrets", true);
  wifiSsid = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("wifi_pass", "");
  preferences.end();
}

int hexNibble(const char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

bool decodeHex(const String &encoded, String &decoded) {
  if (encoded.isEmpty() || encoded.length() % 2 != 0) return false;
  decoded = "";
  decoded.reserve(encoded.length() / 2);
  for (size_t i = 0; i < encoded.length(); i += 2) {
    const int high = hexNibble(encoded[i]);
    const int low = hexNibble(encoded[i + 1]);
    if (high < 0 || low < 0) return false;
    decoded += static_cast<char>((high << 4) | low);
  }
  return true;
}

void handleProvisionLine(const String &line) {
  if (!line.startsWith("PROVISION1 ")) return;
  const int separator = line.indexOf(' ', 11);
  if (separator < 0) {
    Serial.println(F("[WARN] provisioning format: PROVISION1 <hex_ssid> <hex_password>"));
    return;
  }
  String ssid;
  String password;
  if (!decodeHex(line.substring(11, separator), ssid) ||
      !decodeHex(line.substring(separator + 1), password) || ssid.isEmpty() ||
      password.isEmpty()) {
    Serial.println(F("[WARN] provisioning rejected"));
    return;
  }
  saveWifiCredentials(ssid, password);
  Serial.println(F("[INFO] PROVISION_OK; restarting"));
  delay(300);
  ESP.restart();
}

void processSerialLine(String line) {
  line.trim();
  if (line.isEmpty()) return;
  if (line.startsWith("PROVISION1 ")) {
    handleProvisionLine(line);
    return;
  }
  line.toLowerCase();
  if (line == "help") {
    Serial.println(F("[INFO] light <on|off|full|brighter|dimmer|cooler|warmer|toggle|night_light|cancel|timer_15m|timer_30m>"));
    Serial.println(F("[INFO] status"));
  } else if (line == "status") {
    Serial.print(F("[INFO] light API: http://"));
    Serial.print(WiFi.localIP());
    Serial.println(F("/api/v1/light/command"));
  } else if (line.startsWith("light ")) {
    LightCommand command;
    const String name = line.substring(6);
    if (!CeilingLight::parseCommand(name, command) || !light.send(command)) {
      Serial.println(F("[WARN] light command failed"));
    } else {
      Serial.print(F("[INFO] light command sent: "));
      Serial.println(CeilingLight::commandName(command));
    }
  } else {
    Serial.println(F("[WARN] unknown command; type help"));
  }
}

void processSerial() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') continue;
    if (value == '\n') {
      processSerialLine(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 512) {
      serialLine += value;
    }
  }
}

bool connectWifi() {
  if (wifiSsid.isEmpty() || wifiPassword.isEmpty()) {
    Serial.println(F("[WARN] Wi-Fi credentials are not provisioned"));
    Serial.println(F("[INFO] run provision.py with --ssid and --password"));
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOME_IR_HOSTNAME);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  Serial.println(F("[INFO] connecting to provisioned Wi-Fi"));
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WARN] Wi-Fi connection failed; HTTP server is not started"));
    return false;
  }
  Serial.print(F("[INFO] Wi-Fi connected; IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

void handleNotFound() {
  server.send(404, "application/json",
              "{\"ok\":false,\"error\":{\"code\":\"not_found\"}}");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  serialLine.reserve(512);

  irSender.begin();
  aircon.begin();

  Serial.println(F("[INFO] home IR API server"));
  Serial.println(F("[INFO] TX GPIO4 / RX GPIO5 / LAN only / Basic auth disabled"));
  loadWifiCredentials();
  if (connectWifi()) {
    lightApi.begin();
    airconApi.begin();
    systemApi.begin();
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println(F("[INFO] HTTP API listening on port 80"));
  }
}

void loop() {
  processSerial();
  if (WiFi.status() == WL_CONNECTED) server.handleClient();
}
