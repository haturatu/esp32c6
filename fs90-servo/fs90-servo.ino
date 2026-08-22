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

Servo servo;
String serialLine;
int currentAngle = kNeutralAngle;

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

void runSweep() {
  Serial.println(F("[INFO] sweep: 60 -> 90 -> 120 -> 90"));
  moveAndWait(kTestLeftAngle, 2000);
  moveAndWait(kNeutralAngle, 2000);
  moveAndWait(kTestRightAngle, 2000);
  moveAndWait(kNeutralAngle, 500);
  detachServo();
}

void pressHeaterSwitch(const char *name, const uint8_t pressAngle) {
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
  Serial.println(F("[INFO]   sweep            60 -> 90 -> 120 -> 90"));
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
}

void handleCommand(String command) {
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
    moveTo(kNeutralAngle);
    return;
  }
  if (command == "sweep") {
    runSweep();
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
    } else if (serialLine.length() < 64) {
      serialLine += value;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  serialLine.reserve(64);

  Serial.println(F("[INFO] XIAO ESP32-C6 FS90 servo controller"));
  Serial.println(F("[INFO] no automatic movement on boot; type help"));
}

void loop() {
  processSerial();
}
