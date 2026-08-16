#include <Arduino.h>
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
constexpr bool kStartupIrTest = false;

IRDaikinESP ac(kIrLedPin);
IRDaikinESP acInverted(kIrLedPin, true);
IRrecv irrecv(kIrReceiverPin, 1024, 50, true);
decode_results results;
bool hasReceivedDaikinState = false;
uint8_t lastDaikinState[kDaikinStateLength];

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

void printHelp() {
  Serial.println(F("[INFO] power/mode: on, off, auto [temp], cool [temp], heat [temp], dry [temp], fan"));
  Serial.println(F("[INFO] settings: temp [10..32], fan auto|quiet|1|2|3|4|5, swing on|off"));
  Serial.println(F("[INFO] features: sleep on|off, comfort on|off, mold on|off, quiet on|off"));
  Serial.println(F("[INFO] timers: timer-on [minutes], timer-off [minutes], timer-cancel"));
  Serial.println(F("[INFO] diagnostics: replay, raw_on, raw_off, burst_on, burst_off, inv_on, inv_off, status, help"));
}

void sendCurrentState(const char *reason, const uint16_t repeat = 0) {
  Serial.print(F("[INFO] send: "));
  Serial.println(reason);
  Serial.print(F("[DEBUG] state: "));
  Serial.println(ac.toString());
  ac.send(repeat);
  Serial.println(F("[INFO] IR frame sent"));
}

void sendInvertedRawState(const uint8_t state[], const char *reason) {
  Serial.print(F("[INFO] send: "));
  Serial.println(reason);
  acInverted.setRaw(state, kDaikinStateLength);
  Serial.println(F("[DEBUG] output polarity: inverted / active LOW"));
  acInverted.send(2);
  // The inverted sender considers HIGH to be off. Restore the normal
  // low-side transistor off level before returning to the receive loop.
  digitalWrite(kIrLedPin, LOW);
  Serial.println(F("[INFO] inverted IR frame sent x3"));
}

void setModeFromCommand(const uint8_t mode, const String &command,
                        const char *modeName) {
  ac.on();
  ac.setMode(mode);

  const String argument = command.substring(strlen(modeName));
  if (argument.length() > 0) {
    const int temperature = argument.toInt();
    if (temperature >= kDaikinMinTemp && temperature <= kDaikinMaxTemp) {
      ac.setTemp(temperature);
    } else {
      Serial.println(F("[WARN] temperature must be 10..32C; keeping the current value"));
    }
  }

  sendCurrentState(modeName);
}

void setTemperatureFromCommand(const String &command) {
  const String argument = command.substring(4);
  const int temperature = argument.toInt();
  if (argument.length() == 0 || temperature < kDaikinMinTemp ||
      temperature > kDaikinMaxTemp) {
    Serial.println(F("[WARN] temperature must be 10..32C"));
    return;
  }
  ac.setTemp(temperature);
  sendCurrentState("temp");
}

void setFanFromCommand(const String &command) {
  String argument = command.substring(3);
  argument.trim();
  if (argument == "auto") {
    ac.setFan(kDaikinFanAuto);
  } else if (argument == "quiet") {
    ac.setFan(kDaikinFanQuiet);
  } else {
    const int speed = argument.toInt();
    if (argument.length() == 0 || speed < kDaikinFanMin ||
        speed > kDaikinFanMax) {
      Serial.println(F("[WARN] fan must be auto, quiet, or 1..5"));
      return;
    }
    ac.setFan(speed);
  }
  sendCurrentState("fan speed");
}

bool parseToggle(const String &command, const char *name, bool &value) {
  String argument = command.substring(strlen(name));
  argument.trim();
  if (argument == "on") {
    value = true;
    return true;
  }
  if (argument == "off") {
    value = false;
    return true;
  }
  Serial.print(F("[WARN] use "));
  Serial.print(name);
  Serial.println(F(" on|off"));
  return false;
}

void setTimerFromCommand(const String &command, const char *name,
                         const bool onTimer) {
  String argument = command.substring(strlen(name));
  argument.trim();
  const int minutes = argument.toInt();
  if (argument.length() == 0 || minutes < 0 || minutes > 1439) {
    Serial.println(F("[WARN] timer must be minutes from 0 to 1439"));
    return;
  }
  if (onTimer) {
    ac.enableOnTimer(minutes);
  } else {
    ac.enableOffTimer(minutes);
  }
  sendCurrentState(name);
}

void setSleepFromCommand(const String &command) {
  bool enabled = false;
  if (!parseToggle(command, "sleep", enabled)) return;
  uint8_t *raw = ac.getRaw();
  if (enabled) {
    raw[kArc446A3SleepByte] |= kArc446A3SleepMask;
  } else {
    raw[kArc446A3SleepByte] &= ~kArc446A3SleepMask;
  }
  sendCurrentState("sleep");
}

void handleCommand(String command) {
  command.trim();
  command.toLowerCase();
  if (command.length() == 0) return;

  if (command == "help") {
    printHelp();
  } else if (command == "status") {
    Serial.print(F("[INFO] state: "));
    Serial.println(ac.toString());
  } else if (command == "on") {
    ac.on();
    sendCurrentState("on");
  } else if (command == "off") {
    ac.off();
    sendCurrentState("off");
  } else if (command == "replay") {
    if (hasReceivedDaikinState) {
      ac.setRaw(lastDaikinState, kDaikinStateLength);
      sendCurrentState("replay received DAIKIN state");
    } else {
      Serial.println(F("[WARN] receive a DAIKIN frame first"));
    }
  } else if (command == "raw_on") {
    ac.setRaw(kCapturedOnState, kDaikinStateLength);
    sendCurrentState("raw captured remote ON");
  } else if (command == "raw_off") {
    ac.setRaw(kCapturedOffState, kDaikinStateLength);
    sendCurrentState("raw captured remote OFF");
  } else if (command == "burst_on") {
    ac.setRaw(kCapturedOnState, kDaikinStateLength);
    sendCurrentState("raw captured remote ON x3", 2);
  } else if (command == "burst_off") {
    ac.setRaw(kCapturedOffState, kDaikinStateLength);
    sendCurrentState("raw captured remote OFF x3", 2);
  } else if (command == "inv_on") {
    sendInvertedRawState(kCapturedOnState, "raw captured remote ON, inverted output");
  } else if (command == "inv_off") {
    sendInvertedRawState(kCapturedOffState, "raw captured remote OFF, inverted output");
  } else if (command == "auto" || command.startsWith("auto ")) {
    setModeFromCommand(kDaikinAuto, command, "auto");
  } else if (command.startsWith("cool")) {
    setModeFromCommand(kDaikinCool, command, "cool");
  } else if (command.startsWith("heat")) {
    setModeFromCommand(kDaikinHeat, command, "heat");
  } else if (command.startsWith("dry")) {
    setModeFromCommand(kDaikinDry, command, "dry");
  } else if (command == "fan") {
    ac.on();
    ac.setMode(kDaikinFan);
    sendCurrentState("fan");
  } else if (command.startsWith("fan ")) {
    setFanFromCommand(command);
  } else if (command.startsWith("temp")) {
    setTemperatureFromCommand(command);
  } else if (command.startsWith("sleep")) {
    setSleepFromCommand(command);
  } else if (command.startsWith("swing")) {
    bool enabled = false;
    if (parseToggle(command, "swing", enabled)) {
      ac.setSwingVertical(enabled);
      sendCurrentState("swing");
    }
  } else if (command.startsWith("comfort")) {
    bool enabled = false;
    if (parseToggle(command, "comfort", enabled)) {
      ac.setComfort(enabled);
      sendCurrentState("comfort");
    }
  } else if (command.startsWith("mold")) {
    bool enabled = false;
    if (parseToggle(command, "mold", enabled)) {
      ac.setMold(enabled);
      sendCurrentState("mold");
    }
  } else if (command.startsWith("quiet")) {
    bool enabled = false;
    if (parseToggle(command, "quiet", enabled)) {
      ac.setQuiet(enabled);
      sendCurrentState("quiet");
    }
  } else if (command.startsWith("timer-on")) {
    setTimerFromCommand(command, "timer-on", true);
  } else if (command.startsWith("timer-off")) {
    setTimerFromCommand(command, "timer-off", false);
  } else if (command == "timer-cancel") {
    ac.disableOnTimer();
    ac.disableOffTimer();
    sendCurrentState("timer-cancel");
  } else {
    Serial.println(F("[WARN] unknown command; type help"));
  }
}

void printHexByte(const uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void printDaikinStateDiff(const uint8_t previous[], const uint8_t current[]) {
  bool changed = false;
  for (uint8_t i = 0; i < kDaikinStateLength; i++) {
    if (previous[i] == current[i]) continue;
    changed = true;
    Serial.print(F("[DEBUG] diff byte["));
    Serial.print(i);
    Serial.print(F("]: 0x"));
    printHexByte(previous[i]);
    Serial.print(F(" -> 0x"));
    printHexByte(current[i]);
    Serial.print(F(" xor=0x"));
    printHexByte(previous[i] ^ current[i]);
    Serial.println();
  }
  if (!changed) Serial.println(F("[DEBUG] diff: no state bytes changed"));
}

void receiveIrFrame() {
  if (!irrecv.decode(&results)) return;

  Serial.println(F("[INFO] IR frame received from VS1838B"));
  Serial.print(resultToHumanReadableBasic(&results));
  Serial.println(resultToSourceCode(&results));
  if (results.decode_type == decode_type_t::DAIKIN &&
      results.bits == kDaikinBits) {
    if (hasReceivedDaikinState) {
      printDaikinStateDiff(lastDaikinState, results.state);
    } else {
      Serial.println(F("[INFO] first ARC446A3-compatible DAIKIN state captured"));
    }
    memcpy(lastDaikinState, results.state, kDaikinStateLength);
    ac.setRaw(lastDaikinState, kDaikinStateLength);
    hasReceivedDaikinState = true;
    Serial.println(F("[INFO] captured state loaded; use replay to resend it"));
  }
  irrecv.resume();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  ac.begin();
  digitalWrite(kIrLedPin, LOW);
  irrecv.enableIRIn();

  Serial.println(F("[INFO] Daikin AN22NESJ-W IR test"));
  Serial.println(F("[INFO] remote: ARC446A3 / protocol: DAIKIN 280-bit"));
  Serial.println(F("[INFO] IR LED GPIO: 4"));
  Serial.println(F("[INFO] VS1838B OUT GPIO: 5"));

  ac.on();
  ac.setMode(kDaikinCool);
  ac.setTemp(kDefaultTemperature);
  ac.setFan(kDaikinFanAuto);

  if (kStartupIrTest) {
    sendCurrentState("startup test: ON COOL 26C AUTO");
  } else {
    Serial.println(F("[INFO] startup IR test skipped; use raw_on or on"));
  }
  printHelp();
}

void loop() {
  receiveIrFrame();
  if (Serial.available() > 0) {
    handleCommand(Serial.readStringUntil('\n'));
  }
}
