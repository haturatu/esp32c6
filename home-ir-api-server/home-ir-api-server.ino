#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include <IRrecv.h>
#include <IRremoteESP8266.h>

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
DaikinAircon aircon(irSender);
CeilingLight light(irSender);
LightApi lightApi(server, light);
AirconApi airconApi(server, aircon);
SystemApi systemApi(server);
IRrecv irReceiver(HOME_IR_RX_GPIO, 1024, 50, true);
decode_results irResults;

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

void printIrSendResult(const IrSendResult result) {
  switch (result) {
    case IrSendResult::Ok:
      Serial.println(F("[INFO] IR command sent"));
      return;
    case IrSendResult::RateLimited:
      Serial.println(F("[WARN] IR rate limited; wait at least 100ms"));
      return;
    case IrSendResult::NotInitialized:
      Serial.println(F("[WARN] IR sender is not initialized"));
      return;
    case IrSendResult::NotConfigured:
      Serial.println(F("[WARN] IR code is not configured"));
      return;
    case IrSendResult::InvalidCode:
      Serial.println(F("[WARN] IR code is invalid"));
      return;
    case IrSendResult::SendFailed:
      Serial.println(F("[WARN] IR transmission failed"));
      return;
  }
}

void printSerialHelp() {
  Serial.println(F("[INFO] send <on|off|full|brighter|dimmer|cooler|warmer|toggle|night_light|cancel|timer_15m|timer_30m>"));
  Serial.println(F("[INFO] light <same commands as send>"));
  Serial.println(F("[INFO] status"));
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
    printSerialHelp();
    return;
  }
  if (line == "status") {
    Serial.print(F("[INFO] light state: "));
    Serial.println(light.stateJson());
    Serial.print(F("[INFO] aircon state: "));
    Serial.println(aircon.stateJson());
    return;
  }

  String commandName;
  if (line.startsWith("send ")) commandName = line.substring(5);
  else if (line.startsWith("light ")) commandName = line.substring(6);
  if (!commandName.isEmpty()) {
    LightCommand command;
    if (!CeilingLight::parseCommand(commandName, command)) {
      Serial.println(F("[WARN] unknown light command"));
      return;
    }
    const IrSendResult result = light.send(command);
    printIrSendResult(result);
    if (result == IrSendResult::Ok) {
      Serial.print(F("[INFO] light command: "));
      Serial.println(CeilingLight::commandName(command));
    }
    return;
  }
  Serial.println(F("[WARN] unknown command; type help"));
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

void receiveIrFrame() {
  if (!irReceiver.decode(&irResults)) return;

  if (irResults.decode_type == decode_type_t::DAIKIN &&
      irResults.bits == kDaikinBits && !irResults.repeat) {
    if (aircon.handleReceived(irResults.state, kDaikinStateLength, millis())) {
      Serial.println(F("[INFO] DAIKIN 280-bit state received from VS1838B"));
    }
  } else if (irResults.decode_type == decode_type_t::NEC &&
             irResults.bits == 32 && !irResults.repeat) {
    const uint32_t code = static_cast<uint32_t>(irResults.value);
    if (light.handleReceived(code, 32, millis())) {
      Serial.print(F("[DEBUG] NEC light received code=0x"));
      Serial.println(code, HEX);
    } else {
      Serial.print(F("[DEBUG] NEC received code=0x"));
      Serial.println(code, HEX);
    }
  }
  irReceiver.resume();
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

  configTime(9 * 60 * 60, 0, "time.google.com", "time.cloudflare.com");
  Serial.println(F("[INFO] NTP configured: time.google.com, time.cloudflare.com"));
  Serial.print(F("[INFO] Wi-Fi connected; IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

bool isKnownApiPath(const String &uri) {
  if (uri == "/api/v1/health" || uri == "/api/v1/system/health" ||
      uri == "/api/v1/system/info" || uri == "/api/v1/ac/state" ||
      uri == "/api/v1/aircon/state" || uri == "/api/v1/ac/off" ||
      uri == "/api/v1/aircon/off" || uri == "/api/v1/ir/received" ||
      uri == "/api/v1/ir/replay" ||
      uri == "/api/v1/ir/diagnostics/send" ||
      uri == "/api/v1/light/state" || uri == "/api/v1/light/command") {
    return true;
  }
  return uri.startsWith("/api/v1/light/commands/");
}

void handleNotFound() {
  if (isKnownApiPath(server.uri())) {
    server.send(405, "application/json",
                "{\"error\":{\"code\":\"method_not_allowed\",\"message\":\"HTTP method is not supported for this endpoint\"}}");
  } else {
    server.send(404, "application/json",
                "{\"error\":{\"code\":\"not_found\",\"message\":\"endpoint does not exist\"}}");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  serialLine.reserve(512);

  irSender.begin();
  irReceiver.enableIRIn();
  aircon.begin();

  Serial.println(F("[INFO] home IR API server"));
  Serial.println(F("[INFO] TX GPIO4 / RX GPIO5 / DAIKIN 280-bit + NEC 32-bit"));
  Serial.println(F("[INFO] LAN only / Basic auth disabled"));
  loadWifiCredentials();
  if (connectWifi()) {
    lightApi.begin();
    airconApi.begin();
    systemApi.begin();
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println(F("[INFO] HTTP API listening on port 80"));
  }
  printSerialHelp();
}

void loop() {
  processSerial();
  receiveIrFrame();
  if (WiFi.status() == WL_CONNECTED) server.handleClient();
}
