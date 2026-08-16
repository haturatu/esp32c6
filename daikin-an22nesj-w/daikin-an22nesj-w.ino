#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <ir_Daikin.h>

constexpr uint16_t kIrLedPin = 4;
constexpr uint16_t kIrReceiverPin = 5;
constexpr uint8_t kDefaultTemperature = 26;

IRDaikinESP ac(kIrLedPin);
IRrecv irrecv(kIrReceiverPin, 1024, 50, true);
decode_results results;

void printHelp() {
  Serial.println(F("[INFO] commands: on, off, cool [temp], heat [temp], dry [temp], fan, status, help"));
}

void sendCurrentState(const char *reason) {
  Serial.print(F("[INFO] send: "));
  Serial.println(reason);
  Serial.print(F("[DEBUG] state: "));
  Serial.println(ac.toString());
  ac.send();
  Serial.println(F("[INFO] IR frame sent"));
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
  irrecv.resume();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  ac.begin();
  irrecv.enableIRIn();

  Serial.println(F("[INFO] Daikin AN22NESJ-W IR test"));
  Serial.println(F("[INFO] expected remote: ARC469A18 / protocol: DAIKIN 280-bit"));
  Serial.println(F("[INFO] IR LED GPIO: 4"));
  Serial.println(F("[INFO] VS1838B OUT GPIO: 5"));

  ac.on();
  ac.setMode(kDaikinCool);
  ac.setTemp(kDefaultTemperature);
  ac.setFan(kDaikinFanAuto);

  sendCurrentState("startup test: ON COOL 26C AUTO");
  printHelp();
}

void loop() {
  receiveIrFrame();
  if (Serial.available() > 0) {
    handleCommand(Serial.readStringUntil('\n'));
  }
}
