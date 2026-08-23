#include <Arduino.h>
#include <ESP32Servo.h>

namespace {

// XIAO ESP32-C6: D1 is GPIO1. Use the GPIO number so the sketch also builds
// with the generic esp32:esp32:esp32c6 board definition.
constexpr uint8_t kServoPin = 1;
constexpr uint16_t kServoMinPulseUs = 500;
constexpr uint16_t kServoMaxPulseUs = 2400;
constexpr uint8_t kNeutralAngle = 90;
constexpr uint8_t kTestLeftAngle = 60;
constexpr uint8_t kTestRightAngle = 120;

// Adjust these values after the servo horn is fitted to the heater switch.
constexpr uint8_t kHeaterOnAngle = 65;
constexpr uint8_t kHeaterOffAngle = 115;
constexpr uint16_t kPressDurationMs = 500;
constexpr uint16_t kReleaseDurationMs = 500;
constexpr uint16_t kSweepHoldDurationMs = 2000;

Servo servo;
String serialLine;
int currentAngle = kNeutralAngle;
bool continuousSweep = false;
int nextSweepAngle = kTestLeftAngle;
uint32_t nextSweepAt = 0;

bool attachServo() {
  if (servo.attached()) return true;

  servo.setPeriodHertz(50);
  servo.attach(kServoPin, kServoMinPulseUs, kServoMaxPulseUs);
  if (!servo.attached()) {
    Serial.println(F("[ERROR] servo attach failed"));
    return false;
  }

  Serial.println(F("[INFO] servo attached"));
  return true;
}

bool moveTo(const int angle) {
  if (angle < 0 || angle > 180) {
    Serial.println(F("[WARN] angle must be 0..180"));
    return false;
  }
  if (!attachServo()) return false;

  servo.write(angle);
  currentAngle = angle;
  Serial.print(F("[INFO] servo angle: "));
  Serial.println(angle);
  return true;
}

void detachServo() {
  continuousSweep = false;
  if (!servo.attached()) {
    Serial.println(F("[INFO] servo already detached"));
    return;
  }

  servo.detach();
  Serial.println(F("[INFO] servo detached"));
}

void moveAndWait(const int angle, const uint16_t durationMs) {
  if (moveTo(angle)) delay(durationMs);
}

void startContinuousSweep() {
  if (!attachServo()) return;

  continuousSweep = true;
  nextSweepAngle = kTestLeftAngle;
  nextSweepAt = 0;
  Serial.print(F("[INFO] continuous sweep started: "));
  Serial.print(kTestLeftAngle);
  Serial.print(F(" <-> "));
  Serial.println(kTestRightAngle);
}

void stopContinuousSweep() {
  if (!continuousSweep) {
    Serial.println(F("[INFO] continuous sweep already stopped"));
    return;
  }

  continuousSweep = false;
  moveAndWait(kNeutralAngle, kReleaseDurationMs);
  detachServo();
  Serial.println(F("[INFO] continuous sweep stopped"));
}

void updateContinuousSweep() {
  if (!continuousSweep) return;

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextSweepAt) < 0) return;
  if (!moveTo(nextSweepAngle)) {
    continuousSweep = false;
    return;
  }

  nextSweepAngle = nextSweepAngle == kTestLeftAngle
                       ? kTestRightAngle
                       : kTestLeftAngle;
  nextSweepAt = now + kSweepHoldDurationMs;
}

void pressHeaterSwitch(const char *name, const uint8_t pressAngle) {
  continuousSweep = false;
  Serial.print(F("[INFO] heater switch: "));
  Serial.println(name);
  if (!moveTo(pressAngle)) return;
  delay(kPressDurationMs);
  moveAndWait(kNeutralAngle, kReleaseDurationMs);
  detachServo();
}

bool parseAngle(const String &argument, int &angle) {
  if (argument.isEmpty()) return false;
  for (size_t index = 0; index < argument.length(); ++index) {
    if (!isDigit(argument[index])) return false;
  }

  angle = argument.toInt();
  return angle >= 0 && angle <= 180;
}

void printHelp() {
  Serial.println(F("[INFO] commands:"));
  Serial.println(F("[INFO]   angle <0..180>  move to an angle"));
  Serial.println(F("[INFO]   center           move to neutral angle"));
  Serial.println(F("[INFO]   sweep            continuously move across the test range"));
  Serial.println(F("[INFO]   stop             stop sweep and return to neutral"));
  Serial.println(F("[INFO]   heater-on        press the heater ON side"));
  Serial.println(F("[INFO]   heater-off       press the heater OFF side"));
  Serial.println(F("[INFO]   detach           stop servo PWM"));
  Serial.println(F("[INFO]   status            show current configuration"));
  Serial.println(F("[INFO]   help              show this help"));
}

void printStatus() {
  Serial.print(F("[INFO] pin=D1(GPIO1), angle="));
  Serial.print(currentAngle);
  Serial.print(F(", attached="));
  Serial.println(servo.attached() ? F("true") : F("false"));
  Serial.print(F("[INFO] neutral="));
  Serial.print(kNeutralAngle);
  Serial.print(F(", heater-on="));
  Serial.print(kHeaterOnAngle);
  Serial.print(F(", heater-off="));
  Serial.println(kHeaterOffAngle);
  Serial.print(F("[INFO] sweep="));
  Serial.print(continuousSweep ? F("running ") : F("stopped "));
  Serial.print(kTestLeftAngle);
  Serial.print(F(" <-> "));
  Serial.println(kTestRightAngle);
}

void handleCommand(String command) {
  String normalizedCommand;
  normalizedCommand.reserve(command.length());
  for (size_t index = 0; index < command.length(); ++index) {
    const uint8_t value = static_cast<uint8_t>(command[index]);
    if (value >= 0x20 && value <= 0x7e) normalizedCommand += command[index];
  }
  command = normalizedCommand;
  command.trim();
  command.toLowerCase();
  if (command.isEmpty()) return;

  if (command == "help") {
    printHelp();
    return;
  }
  if (command == "status") {
    printStatus();
    return;
  }
  if (command == "center") {
    continuousSweep = false;
    moveTo(kNeutralAngle);
    return;
  }
  if (command == "sweep") {
    startContinuousSweep();
    return;
  }
  if (command == "stop") {
    stopContinuousSweep();
    return;
  }
  if (command == "heater-on") {
    pressHeaterSwitch("ON", kHeaterOnAngle);
    return;
  }
  if (command == "heater-off") {
    pressHeaterSwitch("OFF", kHeaterOffAngle);
    return;
  }
  if (command == "detach") {
    detachServo();
    return;
  }
  if (command.startsWith("angle ")) {
    int angle = 0;
    String argument = command.substring(6);
    argument.trim();
    if (!parseAngle(argument, angle)) {
      Serial.println(F("[WARN] usage: angle <0..180>"));
      return;
    }
    continuousSweep = false;
    moveTo(angle);
    return;
  }

  Serial.println(F("[WARN] unknown command; type help"));
}

void processSerial() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') continue;
    if (value == '\n') {
      handleCommand(serialLine);
      serialLine = "";
    } else if (value >= 0x20 && value <= 0x7e && serialLine.length() < 64) {
      serialLine += value;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  serialLine.reserve(64);

  Serial.println(F("[INFO] XIAO ESP32-C6 FS90 servo controller"));
  Serial.println(F("[INFO] ready; no automatic movement on boot; type help"));
}

void loop() {
  processSerial();
  updateContinuousSweep();
}
