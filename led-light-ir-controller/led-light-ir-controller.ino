#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>

constexpr uint16_t kIrLedPin = 4;
constexpr uint16_t kIrReceiverPin = 5;
constexpr uint16_t kNecBits = 32;
constexpr uint16_t kCaptureBufferSize = 1024;
constexpr uint16_t kCaptureTimeoutMs = 15;
constexpr size_t kMaxRequestBody = 256;
constexpr uint32_t kMinimumIrSendIntervalMs = 100;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint16_t kNecRepeatCount = 0;

struct LightCommand {
  const char *name;
  uint32_t code;
};

// Captured from the LED light remote. All commands use NEC 32-bit framing.
constexpr LightCommand kLightCommands[] = {
    {"on", 0x807F00FF},
    {"off", 0x807F807F},
    {"full", 0x807F609F},
    {"brighter", 0x807FA05F},
    {"dimmer", 0x807F20DF},
    {"cooler", 0x807F40BF},
    {"warmer", 0x807F50AF},
    {"toggle", 0x807FC03F},
    {"night_light", 0x807FD02F},
    {"cancel", 0x807FE01F},
    {"timer_15m", 0x807F22DD},
    {"timer_30m", 0x807FFF00},
};
constexpr size_t kLightCommandCount =
    sizeof(kLightCommands) / sizeof(kLightCommands[0]);

IRsend irsend(kIrLedPin);
IRrecv irrecv(kIrReceiverPin, kCaptureBufferSize, kCaptureTimeoutMs, true);
WebServer server(80);
Preferences preferences;
decode_results results;

String wifiSsid;
String wifiPassword;
String serialLine;
String lastCommand;
uint32_t lastCode = 0;
uint32_t lastTransmittedAtMs = 0;
uint32_t lastIrSendAtMs = 0;
bool hasSentIr = false;

void sendJson(const int statusCode, const String &body) {
  server.send(statusCode, "application/json", body);
}

void sendError(const int statusCode, const char *code, const char *message) {
  String body = "{\"error\":{\"code\":\"";
  body += code;
  body += "\",\"message\":\"";
  body += message;
  body += "\"}}";
  sendJson(statusCode, body);
}

String codeAsJson(const uint32_t code) {
  String value = "0x";
  String hex = String(code, HEX);
  hex.toUpperCase();
  while (hex.length() < 8) hex = "0" + hex;
  value += hex;
  return value;
}

const LightCommand *findCommand(const String &name) {
  for (size_t i = 0; i < kLightCommandCount; i++) {
    if (name == kLightCommands[i].name) return &kLightCommands[i];
  }
  return nullptr;
}

String stateJson() {
  String body = "{\"last_command\":";
  if (lastCommand.isEmpty()) {
    body += "null";
  } else {
    body += "\"";
    body += lastCommand;
    body += "\"";
  }
  body += ",\"last_code\":";
  if (hasSentIr) {
    body += "\"";
    body += codeAsJson(lastCode);
    body += "\"";
  } else {
    body += "null";
  }
  body += ",\"protocol\":\"NEC\",\"bits\":32,\"last_transmitted_at_ms\":";
  body += String(lastTransmittedAtMs);
  body += ",\"state_source\":\"";
  body += hasSentIr ? "transmitted" : "initial";
  body += "\"}";
  return body;
}

void printCommandList() {
  Serial.println(F("[INFO] commands:"));
  for (size_t i = 0; i < kLightCommandCount; i++) {
    Serial.print(F("[INFO]   "));
    Serial.print(kLightCommands[i].name);
    Serial.print(F(" -> "));
    Serial.println(codeAsJson(kLightCommands[i].code));
  }
  Serial.println(F("[INFO] serial: send <command>, status, help"));
}

bool sendLightCommand(const String &name, String &errorCode,
                      String &errorMessage) {
  const LightCommand *command = findCommand(name);
  if (command == nullptr) {
    errorCode = "invalid_command";
    errorMessage = "command is not supported";
    return false;
  }

  const uint32_t now = millis();
  if (hasSentIr && now - lastIrSendAtMs < kMinimumIrSendIntervalMs) {
    errorCode = "ir_rate_limited";
    errorMessage = "IR commands must be at least 100ms apart";
    return false;
  }

  irsend.sendNEC(command->code, kNecBits, kNecRepeatCount);
  digitalWrite(kIrLedPin, LOW);
  lastCommand = command->name;
  lastCode = command->code;
  lastTransmittedAtMs = now;
  lastIrSendAtMs = now;
  hasSentIr = true;

  Serial.print(F("[INFO] IR sent command="));
  Serial.print(command->name);
  Serial.print(F(" code="));
  Serial.println(codeAsJson(command->code));
  return true;
}

bool parseCommandBody(const String &body, String &command) {
  const int keyStart = body.indexOf("\"command\"");
  if (keyStart < 0) return false;
  const int colon = body.indexOf(':', keyStart + 9);
  if (colon < 0) return false;
  const int quoteStart = body.indexOf('"', colon + 1);
  if (quoteStart < 0) return false;
  const int quoteEnd = body.indexOf('"', quoteStart + 1);
  if (quoteEnd < 0) return false;
  command = body.substring(quoteStart + 1, quoteEnd);
  command.toLowerCase();
  return !command.isEmpty();
}

void handleCommandRequest() {
  if (!server.hasArg("plain")) {
    sendError(400, "invalid_json", "request body is required");
    return;
  }
  const String body = server.arg("plain");
  if (body.length() > kMaxRequestBody) {
    sendError(413, "request_too_large", "request body must be 256 bytes or less");
    return;
  }

  String command;
  if (!parseCommandBody(body, command)) {
    sendError(400, "invalid_json", "body must contain a command string");
    return;
  }

  String errorCode;
  String errorMessage;
  if (!sendLightCommand(command, errorCode, errorMessage)) {
    sendError(errorCode == "invalid_command" ? 422 : 429, errorCode.c_str(),
              errorMessage.c_str());
    return;
  }

  String response = "{\"ok\":true,\"command\":\"";
  response += command;
  response += "\",\"code\":\"";
  response += codeAsJson(lastCode);
  response += "\",\"ir\":{\"transmitted\":true,\"acknowledged\":false}}";
  sendJson(200, response);
}

void handleLightState() { sendJson(200, stateJson()); }

void handleHealth() { sendJson(200, "{\"status\":\"ok\"}"); }

void handleInfo() {
  String response =
      "{\"device\":\"esp32c6\",\"model\":\"led-light-ir-controller\","
      "\"api_version\":\"v1\",\"ir\":{\"protocol\":\"NEC\","
      "\"bits\":32,\"tx_gpio\":4,\"rx_gpio\":5},\"wifi\":{\"rssi\":";
  response += String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  response += "},\"uptime_seconds\":";
  response += String(millis() / 1000);
  response += ",\"free_heap\":";
  response += String(ESP.getFreeHeap());
  response += "}";
  sendJson(200, response);
}

bool isKnownApiPath(const String &uri) {
  return uri == "/api/v1/light/state" || uri == "/api/v1/light/command" ||
         uri == "/api/v1/system/health" || uri == "/api/v1/system/info";
}

void handleNotFound() {
  if (isKnownApiPath(server.uri())) {
    sendError(405, "method_not_allowed", "HTTP method is not supported for this endpoint");
  } else {
    sendError(404, "not_found", "endpoint does not exist");
  }
}

void setupHttpRoutes() {
  server.on("/api/v1/light/state", HTTP_GET, handleLightState);
  server.on("/api/v1/light/command", HTTP_POST, handleCommandRequest);
  server.on("/api/v1/system/health", HTTP_GET, handleHealth);
  server.on("/api/v1/system/info", HTTP_GET, handleInfo);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("[INFO] HTTP API listening on port 80"));
}

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
  if (encoded.length() == 0 || encoded.length() % 2 != 0) return false;
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
    printCommandList();
    return;
  }
  if (line == "status") {
    Serial.print(F("[INFO] state: "));
    Serial.println(stateJson());
    return;
  }
  if (line.startsWith("send ")) line = line.substring(5);

  String errorCode;
  String errorMessage;
  if (!sendLightCommand(line, errorCode, errorMessage)) {
    Serial.print(F("[WARN] "));
    Serial.print(errorCode);
    Serial.print(F(": "));
    Serial.println(errorMessage);
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

void receiveIrFrame() {
  if (!irrecv.decode(&results)) return;
  if (results.decode_type == decode_type_t::NEC && results.bits == kNecBits &&
      !results.repeat) {
    Serial.print(F("[DEBUG] NEC received code="));
    Serial.println(codeAsJson(static_cast<uint32_t>(results.value)));
  }
  irrecv.resume();
}

bool connectWifi() {
  if (wifiSsid.isEmpty() || wifiPassword.isEmpty()) {
    Serial.println(F("[WARN] Wi-Fi credentials are not provisioned"));
    Serial.println(F("[INFO] run provision.py with --ssid and --password"));
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("led-light-ir-controller");
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  Serial.println(F("[INFO] connecting to provisioned Wi-Fi"));
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < kWifiConnectTimeoutMs) {
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  serialLine.reserve(512);

  irsend.begin();
  digitalWrite(kIrLedPin, LOW);
  irrecv.enableIRIn();

  Serial.println(F("[INFO] LED light remote HTTP API"));
  Serial.println(F("[INFO] NEC 32-bit / TX GPIO4 / RX GPIO5"));
  Serial.println(F("[INFO] Authentication disabled; LAN/VPN use only"));
  printCommandList();

  loadWifiCredentials();
  if (connectWifi()) setupHttpRoutes();
}

void loop() {
  processSerial();
  receiveIrFrame();
  if (WiFi.status() == WL_CONNECTED) server.handleClient();
}
