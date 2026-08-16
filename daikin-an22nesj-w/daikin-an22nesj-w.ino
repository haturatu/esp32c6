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
constexpr bool kStartupIrTest = false;

IRDaikinESP ac(kIrLedPin);
IRDaikinESP acInverted(kIrLedPin, true);
IRrecv irrecv(kIrReceiverPin, 1024, 50, true);
decode_results results;
bool hasReceivedDaikinState = false;
uint8_t lastDaikinState[kDaikinStateLength];

// Captured from the ARC469A18 remote paired with AN22NESJ-W.
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
  Serial.println(F("[INFO] commands: on, off, cool [temp], heat [temp], dry [temp], fan, replay, raw_on, raw_off, burst_on, burst_off, inv_on, inv_off, status, help"));
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
  } else {
    Serial.println(F("[WARN] unknown command; type help"));
  }
}

void receiveIrFrame() {
  if (!irrecv.decode(&results)) return;

  Serial.println(F("[INFO] IR frame received from VS1838B"));
  Serial.print(resultToHumanReadableBasic(&results));
  Serial.println(resultToSourceCode(&results));
  if (results.decode_type == decode_type_t::DAIKIN &&
      results.bits == kDaikinBits) {
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
  acInverted.begin();
  irrecv.enableIRIn();

  Serial.println(F("[INFO] Daikin AN22NESJ-W IR test"));
  Serial.println(F("[INFO] expected remote: ARC469A18 / protocol: DAIKIN 280-bit"));
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
