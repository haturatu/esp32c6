#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>

#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <ir_Daikin.h>

constexpr uint16_t kIrLedPin = 4;
constexpr uint16_t kIrReceiverPin = 5;
constexpr uint8_t kDefaultTemperature = 26;
constexpr uint8_t kArc446A3SleepByte = 29;
constexpr uint8_t kArc446A3SleepMask = 0x04;
constexpr uint8_t kArc446A3HealthByte = 29;
constexpr uint8_t kArc446A3HealthMask = 0x08;
constexpr uint8_t kArc446A3CleanByte = 6;
constexpr uint8_t kArc446A3CleanMask = 0x08;
constexpr size_t kMaxRequestBody = 2048;
constexpr uint32_t kMinimumIrSendIntervalMs = 100;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr bool kEnableIrDiagnostics = false;

const char *const kApiPrefix = "/api/v1";

IRDaikinESP ac(kIrLedPin);
IRDaikinESP acInverted(kIrLedPin, true);
IRrecv irrecv(kIrReceiverPin, 1024, 50, true);
WebServer server(80);
Preferences preferences;
decode_results results;

enum class StateSource : uint8_t { Initial, Transmitted, Received };

struct AcPatch {
  bool hasPower = false;
  bool power = false;
  bool hasMode = false;
  uint8_t mode = kDaikinAuto;
  bool hasTemperature = false;
  int temperature = 0;
  bool hasFan = false;
  uint8_t fan = kDaikinFanAuto;
  bool hasSwing = false;
  bool swing = false;
  bool hasSleep = false;
  bool sleep = false;
  bool hasHealth = false;
  bool health = false;
  bool hasComfort = false;
  bool comfort = false;
  bool hasClean = false;
  bool clean = false;
  bool hasQuiet = false;
  bool quiet = false;
  bool hasTimerOn = false;
  bool timerOnEnabled = false;
  uint16_t timerOn = 0;
  bool hasTimerOff = false;
  bool timerOffEnabled = false;
  uint16_t timerOff = 0;
};

bool hasReceivedDaikinState = false;
uint8_t lastDaikinState[kDaikinStateLength];
uint32_t lastReceivedAtMs = 0;
uint32_t lastIrSendAtMs = 0;
bool hasSentIr = false;
StateSource stateSource = StateSource::Initial;
String serialLine;
String wifiSsid;
String wifiPassword;

// Captured from the ARC446A3 remote paired with AN22NESJ-W.
const uint8_t kCapturedOnState[kDaikinStateLength] = {
    0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7, 0x11, 0xDA,
    0x27, 0x00, 0x42, 0x00, 0x00, 0x54, 0x11, 0xDA, 0x27, 0x00,
    0x00, 0x39, 0x32, 0x00, 0xAF, 0x00, 0x00, 0x06, 0x60, 0x00,
    0x00, 0xC1, 0x00, 0x00, 0x53};
const uint8_t kCapturedOffState[kDaikinStateLength] = {
    0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7, 0x11, 0xDA,
    0x27, 0x00, 0x42, 0x00, 0x00, 0x54, 0x11, 0xDA, 0x27, 0x00,
    0x00, 0x38, 0x32, 0x00, 0xAF, 0x00, 0x00, 0x06, 0x60, 0x00,
    0x00, 0xC1, 0x00, 0x00, 0x52};

class JsonReader {
 public:
  explicit JsonReader(const String &value) : text(value) {}

  bool parsePatch(AcPatch &patch, String &error, int &statusCode) {
    skipWhitespace();
    if (!consume('{')) return fail("invalid_json", error, statusCode, 400);
    skipWhitespace();
    if (consume('}')) return fail("empty_patch", error, statusCode, 422);

    while (true) {
      String key;
      if (!parseString(key)) return fail("invalid_json", error, statusCode, 400);
      skipWhitespace();
      if (!consume(':')) return fail("invalid_json", error, statusCode, 400);

      bool ok = false;
      if (key == "power") {
        patch.hasPower = true;
        ok = parseBool(patch.power);
      } else if (key == "mode") {
        String mode;
        ok = parseString(mode) && parseMode(mode, patch.mode);
        patch.hasMode = ok;
      } else if (key == "temperature") {
        patch.hasTemperature = true;
        ok = parseInt(patch.temperature);
        if (ok && (patch.temperature < kDaikinMinTemp ||
                   patch.temperature > kDaikinMaxTemp)) {
          return fail("invalid_temperature", error, statusCode, 422);
        }
      } else if (key == "fan") {
        patch.hasFan = true;
        ok = parseFan(patch.fan);
      } else if (key == "swing") {
        patch.hasSwing = true;
        ok = parseBool(patch.swing);
      } else if (key == "sleep") {
        patch.hasSleep = true;
        ok = parseBool(patch.sleep);
      } else if (key == "health") {
        patch.hasHealth = true;
        ok = parseBool(patch.health);
      } else if (key == "comfort") {
        patch.hasComfort = true;
        ok = parseBool(patch.comfort);
      } else if (key == "clean") {
        patch.hasClean = true;
        ok = parseBool(patch.clean);
      } else if (key == "quiet") {
        patch.hasQuiet = true;
        ok = parseBool(patch.quiet);
      } else if (key == "timer") {
        ok = parseTimer(patch);
      } else {
        return fail("unknown_field", error, statusCode, 422);
      }

      if (!ok) return fail("invalid_value", error, statusCode, 422);
      skipWhitespace();
      if (consume('}')) break;
      if (!consume(',')) return fail("invalid_json", error, statusCode, 400);
      skipWhitespace();
    }

    skipWhitespace();
    if (!atEnd()) return fail("invalid_json", error, statusCode, 400);
    return true;
  }

 private:
  const String &text;
  size_t position = 0;

  bool atEnd() const { return position >= text.length(); }

  void skipWhitespace() {
    while (!atEnd()) {
      const char c = text[position];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
      position++;
    }
  }

  bool consume(const char expected) {
    skipWhitespace();
    if (atEnd() || text[position] != expected) return false;
    position++;
    return true;
  }

  bool parseString(String &value) {
    skipWhitespace();
    if (atEnd() || text[position++] != '"') return false;
    value = "";
    while (!atEnd()) {
      const char c = text[position++];
      if (c == '"') return true;
      if (c == '\\') {
        if (atEnd()) return false;
        const char escaped = text[position++];
        switch (escaped) {
          case '"': value += '"'; break;
          case '\\': value += '\\'; break;
          case '/': value += '/'; break;
          case 'b': value += '\b'; break;
          case 'f': value += '\f'; break;
          case 'n': value += '\n'; break;
          case 'r': value += '\r'; break;
          case 't': value += '\t'; break;
          default: return false;
        }
      } else {
        if (static_cast<uint8_t>(c) < 0x20) return false;
        value += c;
      }
    }
    return false;
  }

  bool parseBool(bool &value) {
    skipWhitespace();
    if (text.startsWith("true", position)) {
      position += 4;
      value = true;
      return true;
    }
    if (text.startsWith("false", position)) {
      position += 5;
      value = false;
      return true;
    }
    return false;
  }

  bool parseNull() {
    skipWhitespace();
    if (!text.startsWith("null", position)) return false;
    position += 4;
    return true;
  }

  bool parseInt(int &value) {
    skipWhitespace();
    const size_t start = position;
    if (!atEnd() && text[position] == '-') position++;
    const size_t digits = position;
    while (!atEnd() && text[position] >= '0' && text[position] <= '9') {
      position++;
    }
    if (position == digits) {
      position = start;
      return false;
    }
    const String number = text.substring(start, position);
    value = number.toInt();
    return true;
  }

  bool parseMode(const String &value, uint8_t &mode) {
    if (value == "auto") mode = kDaikinAuto;
    else if (value == "cool") mode = kDaikinCool;
    else if (value == "heat") mode = kDaikinHeat;
    else if (value == "dry") mode = kDaikinDry;
    else if (value == "fan") mode = kDaikinFan;
    else return false;
    return true;
  }

  bool parseFan(uint8_t &fan) {
    skipWhitespace();
    if (!atEnd() && text[position] == '"') {
      String value;
      if (!parseString(value)) return false;
      if (value == "auto") fan = kDaikinFanAuto;
      else if (value == "quiet") fan = kDaikinFanQuiet;
      else if (value == "1") fan = 1;
      else if (value == "2") fan = 2;
      else if (value == "3") fan = 3;
      else if (value == "4") fan = 4;
      else if (value == "5") fan = 5;
      else return false;
      return true;
    }
    int value = 0;
    if (!parseInt(value) || value < 1 || value > 5) return false;
    fan = static_cast<uint8_t>(value);
    return true;
  }

  bool parseTimer(AcPatch &patch) {
    if (!consume('{')) return false;
    skipWhitespace();
    if (consume('}')) return false;
    while (true) {
      String key;
      if (!parseString(key) || !consume(':')) return false;
      bool enabled = true;
      int value = 0;
      if (parseNull()) {
        enabled = false;
      } else if (!parseInt(value) || value < 0 || value > 1439) {
        return false;
      }
      if (key == "on") {
        patch.hasTimerOn = true;
        patch.timerOnEnabled = enabled;
        patch.timerOn = static_cast<uint16_t>(value);
      } else if (key == "off") {
        patch.hasTimerOff = true;
        patch.timerOffEnabled = enabled;
        patch.timerOff = static_cast<uint16_t>(value);
      } else {
        return false;
      }
      skipWhitespace();
      if (consume('}')) return true;
      if (!consume(',')) return false;
    }
  }

  bool fail(const char *code, String &error, int &statusCode,
            const int codeValue) {
    error = code;
    statusCode = codeValue;
    return false;
  }
};

const char *stateSourceName() {
  switch (stateSource) {
    case StateSource::Transmitted: return "transmitted";
    case StateSource::Received: return "received";
    default: return "initial";
  }
}

const char *modeName(const uint8_t mode) {
  switch (mode) {
    case kDaikinCool: return "cool";
    case kDaikinHeat: return "heat";
    case kDaikinDry: return "dry";
    case kDaikinFan: return "fan";
    default: return "auto";
  }
}

String fanName(const uint8_t fan) {
  if (fan == kDaikinFanAuto) return "auto";
  if (fan == kDaikinFanQuiet) return "quiet";
  return String(fan);
}

String jsonBool(const bool value) { return value ? "true" : "false"; }

String jsonTimer(const bool enabled, const uint16_t value) {
  return enabled ? String(value) : "null";
}

String acStateJson() {
  uint8_t *raw = ac.getRaw();
  String json;
  json.reserve(520);
  json += "{\"power\":";
  json += jsonBool(ac.getPower());
  json += ",\"mode\":\"";
  json += modeName(ac.getMode());
  json += "\",\"temperature\":";
  json += String(static_cast<int>(ac.getTemp()));
  json += ",\"fan\":\"";
  json += fanName(ac.getFan());
  json += "\",\"swing\":";
  json += jsonBool(ac.getSwingVertical());
  json += ",\"sleep\":";
  json += jsonBool((raw[kArc446A3SleepByte] & kArc446A3SleepMask) != 0);
  json += ",\"health\":";
  json += jsonBool((raw[kArc446A3HealthByte] & kArc446A3HealthMask) != 0);
  json += ",\"comfort\":";
  json += jsonBool(ac.getComfort());
  json += ",\"clean\":";
  json += jsonBool((raw[kArc446A3CleanByte] & kArc446A3CleanMask) != 0);
  json += ",\"quiet\":";
  json += jsonBool(ac.getQuiet());
  json += ",\"timer\":{\"on\":";
  json += jsonTimer(ac.getOnTimerEnabled(), ac.getOnTime());
  json += ",\"off\":";
  json += jsonTimer(ac.getOffTimerEnabled(), ac.getOffTime());
  json += "},\"state_source\":\"";
  json += stateSourceName();
  json += "\"}";
  return json;
}

String errorJson(const char *code, const String &message) {
  String json = "{\"error\":{\"code\":\"";
  json += code;
  json += "\",\"message\":\"";
  json += message;
  json += "\"}}";
  return json;
}

void sendJson(const int statusCode, const String &body) {
  server.send(statusCode, "application/json; charset=utf-8", body);
}

void sendError(const int statusCode, const char *code, const String &message) {
  sendJson(statusCode, errorJson(code, message));
}

void syncStateSource(const StateSource source) { stateSource = source; }

void sendCurrentState() {
  const uint32_t now = millis();
  if (hasSentIr) {
    const uint32_t elapsed = now - lastIrSendAtMs;
    if (elapsed < kMinimumIrSendIntervalMs) {
      delay(kMinimumIrSendIntervalMs - elapsed);
    }
  }
  ac.send(0);
  lastIrSendAtMs = millis();
  hasSentIr = true;
  syncStateSource(StateSource::Transmitted);
}

void sendInvertedRawState(const uint8_t state[], const uint16_t repeat) {
  acInverted.setRaw(state, kDaikinStateLength);
  acInverted.send(repeat);
  digitalWrite(kIrLedPin, LOW);
  lastIrSendAtMs = millis();
  hasSentIr = true;
  syncStateSource(StateSource::Transmitted);
}

void applyPatch(const AcPatch &patch) {
  if (patch.hasMode) {
    ac.on();
    ac.setMode(patch.mode);
  }
  if (patch.hasPower) ac.setPower(patch.power);
  if (patch.hasTemperature) ac.setTemp(patch.temperature);
  if (patch.hasFan) ac.setFan(patch.fan);
  if (patch.hasSwing) ac.setSwingVertical(patch.swing);
  if (patch.hasSleep) {
    uint8_t *raw = ac.getRaw();
    if (patch.sleep) raw[kArc446A3SleepByte] |= kArc446A3SleepMask;
    else raw[kArc446A3SleepByte] &= ~kArc446A3SleepMask;
  }
  if (patch.hasHealth) {
    uint8_t *raw = ac.getRaw();
    if (patch.health) raw[kArc446A3HealthByte] |= kArc446A3HealthMask;
    else raw[kArc446A3HealthByte] &= ~kArc446A3HealthMask;
  }
  if (patch.hasComfort) ac.setComfort(patch.comfort);
  if (patch.hasClean) {
    uint8_t *raw = ac.getRaw();
    if (patch.clean) raw[kArc446A3CleanByte] |= kArc446A3CleanMask;
    else raw[kArc446A3CleanByte] &= ~kArc446A3CleanMask;
  }
  if (patch.hasQuiet) ac.setQuiet(patch.quiet);
  if (patch.hasTimerOn) {
    if (patch.timerOnEnabled) ac.enableOnTimer(patch.timerOn);
    else ac.disableOnTimer();
  }
  if (patch.hasTimerOff) {
    if (patch.timerOffEnabled) ac.enableOffTimer(patch.timerOff);
    else ac.disableOffTimer();
  }
}

void handleAcStatePatch() {
  if (!server.hasArg("plain")) {
    sendError(400, "invalid_json", "request body is required");
    return;
  }
  const String body = server.arg("plain");
  if (body.length() > kMaxRequestBody) {
    sendError(413, "request_too_large", "request body must be 2048 bytes or less");
    return;
  }

  AcPatch patch;
  String parseError;
  int parseStatus = 400;
  JsonReader reader(body);
  if (!reader.parsePatch(patch, parseError, parseStatus)) {
    if (parseError == "invalid_temperature") {
      sendError(parseStatus, parseError.c_str(), "temperature must be between 10 and 32");
    } else if (parseError == "unknown_field") {
      sendError(parseStatus, parseError.c_str(), "field is not supported");
    } else if (parseError == "empty_patch") {
      sendError(parseStatus, parseError.c_str(), "at least one field is required");
    } else {
      sendError(parseStatus, parseError.c_str(), "invalid JSON or field value");
    }
    return;
  }

  applyPatch(patch);
  sendCurrentState();
  String response = "{\"ok\":true,\"state\":";
  response += acStateJson();
  response += "}";
  sendJson(200, response);
}

void handleReceivedIr() {
  if (!hasReceivedDaikinState) {
    sendJson(200, "{\"received\":false}");
    return;
  }
  String response = "{\"received\":true,\"protocol\":\"DAIKIN\",\"bits\":280,\"received_at_ms\":";
  response += String(lastReceivedAtMs);
  response += ",\"state_loaded\":true}";
  sendJson(200, response);
}

void handleReplay() {
  if (!hasReceivedDaikinState) {
    sendError(409, "no_received_ir_state", "No DAIKIN IR frame has been received");
    return;
  }
  ac.setRaw(lastDaikinState, kDaikinStateLength);
  sendCurrentState();
  sendJson(200, "{\"ok\":true,\"ir\":{\"transmitted\":true,\"acknowledged\":false}}");
}

void handleDiagnostics() {
  if (!kEnableIrDiagnostics) {
    sendError(404, "not_found", "IR diagnostics are disabled");
    return;
  }
  sendError(501, "not_implemented", "IR diagnostics are not enabled in this build");
}

void handleHealth() { sendJson(200, "{\"status\":\"ok\"}"); }

void handleInfo() {
  String response = "{\"device\":\"esp32c6\",\"model\":\"daikin-an22nesj-w-controller\",\"api_version\":\"v1\",\"ir\":{\"protocol\":\"DAIKIN\",\"bits\":280,\"tx_gpio\":4,\"rx_gpio\":5},\"wifi\":{\"rssi\":";
  response += String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  response += "},\"uptime_seconds\":";
  response += String(millis() / 1000);
  response += ",\"free_heap\":";
  response += String(ESP.getFreeHeap());
  response += "}";
  sendJson(200, response);
}

bool isKnownApiPath(const String &uri) {
  return uri == "/api/v1/ac/state" || uri == "/api/v1/ir/received" ||
         uri == "/api/v1/ir/replay" || uri == "/api/v1/ir/diagnostics/send" ||
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
  server.on("/api/v1/ac/state", HTTP_GET, []() {
    sendJson(200, acStateJson());
  });
  server.on("/api/v1/ac/state", HTTP_PATCH, handleAcStatePatch);
  server.on("/api/v1/ir/received", HTTP_GET, handleReceivedIr);
  server.on("/api/v1/ir/replay", HTTP_POST, handleReplay);
  server.on("/api/v1/ir/diagnostics/send", HTTP_POST, handleDiagnostics);
  server.on("/api/v1/system/health", HTTP_GET, handleHealth);
  server.on("/api/v1/system/info", HTTP_GET, handleInfo);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("[INFO] HTTP API listening on port 80"));
}

void loadWifiCredentials() {
  preferences.begin("secrets", true);
  wifiSsid = preferences.getString("ssid", "");
  wifiPassword = preferences.getString("wifi_pass", "");
  preferences.end();
}

void saveWifiCredentials(const String &ssid, const String &password) {
  preferences.begin("secrets", false);
  preferences.putString("ssid", ssid);
  preferences.putString("wifi_pass", password);
  preferences.end();
}

bool decodeHex(const String &encoded, String &decoded) {
  if (encoded.length() % 2 != 0) return false;
  decoded = "";
  decoded.reserve(encoded.length() / 2);
  for (size_t i = 0; i < encoded.length(); i += 2) {
    const char high = encoded[i];
    const char low = encoded[i + 1];
    const int highValue = isDigit(high) ? high - '0' : (toLowerCase(high) - 'a' + 10);
    const int lowValue = isDigit(low) ? low - '0' : (toLowerCase(low) - 'a' + 10);
    if (highValue < 0 || highValue > 15 || lowValue < 0 || lowValue > 15) return false;
    decoded += static_cast<char>((highValue << 4) | lowValue);
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
  const String encodedSsid = line.substring(11, separator);
  const String encodedPassword = line.substring(separator + 1);
  String ssid;
  String password;
  if (!decodeHex(encodedSsid, ssid) || !decodeHex(encodedPassword, password) ||
      ssid.isEmpty() || password.isEmpty()) {
    Serial.println(F("[WARN] provisioning rejected"));
    return;
  }
  saveWifiCredentials(ssid, password);
  Serial.println(F("[INFO] PROVISION_OK; restarting"));
  delay(300);
  ESP.restart();
}

void processSerialProvisioning() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      serialLine.trim();
      handleProvisionLine(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 512) {
      serialLine += c;
    }
  }
}

void printProvisioningHelp() {
  Serial.println(F("[INFO] NVS provisioning: run provision.py with --ssid and --password"));
  Serial.println(F("[INFO] credentials are stored in Preferences namespace 'secrets'"));
}

bool connectWifi() {
  if (wifiSsid.isEmpty() || wifiPassword.isEmpty()) {
    Serial.println(F("[WARN] Wi-Fi credentials are not provisioned"));
    printProvisioningHelp();
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("daikin-an22nesj-w");
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  Serial.println(F("[INFO] connecting to provisioned Wi-Fi"));
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < kWifiConnectTimeoutMs) {
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

void receiveIrFrame() {
  if (!irrecv.decode(&results)) return;
  if (results.decode_type == decode_type_t::DAIKIN &&
      results.bits == kDaikinBits) {
    memcpy(lastDaikinState, results.state, kDaikinStateLength);
    ac.setRaw(lastDaikinState, kDaikinStateLength);
    hasReceivedDaikinState = true;
    lastReceivedAtMs = millis();
    syncStateSource(StateSource::Received);
    Serial.println(F("[INFO] DAIKIN 280-bit state received from VS1838B"));
  } else {
    Serial.println(F("[DEBUG] non-DAIKIN IR frame ignored"));
  }
  irrecv.resume();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  serialLine.reserve(512);

  ac.begin();
  digitalWrite(kIrLedPin, LOW);
  irrecv.enableIRIn();

  ac.on();
  ac.setMode(kDaikinCool);
  ac.setTemp(kDefaultTemperature);
  ac.setFan(kDaikinFanAuto);

  Serial.println(F("[INFO] Daikin AN22NESJ-W HTTP API"));
  Serial.println(F("[INFO] ARC446A3 / DAIKIN 280-bit / TX GPIO4 / RX GPIO5"));
  Serial.println(F("[INFO] Basic authentication disabled; LAN/VPN use only"));

  loadWifiCredentials();
  if (connectWifi()) setupHttpRoutes();
  printProvisioningHelp();
}

void loop() {
  processSerialProvisioning();
  receiveIrFrame();
  if (WiFi.status() == WL_CONNECTED) server.handleClient();
}
