#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>

// The existing VS1838B wiring is reused for investigation.
constexpr uint16_t kIrReceiverPin = 5;
constexpr uint16_t kCaptureBufferSize = 2048;
constexpr uint16_t kCaptureTimeoutMs = 50;

IRrecv irrecv(kIrReceiverPin, kCaptureBufferSize, kCaptureTimeoutMs, true);
decode_results results;
uint32_t captureSequence = 0;

void printHelp() {
  Serial.println(F("[INFO] Point the LED remote at VS1838B and press one button once."));
  Serial.println(F("[INFO] Commands: help, status"));
  Serial.println(F("[INFO] This sketch is receive-only; it does not drive the IR LED."));
}

void printCaptureMetadata() {
  captureSequence++;
  Serial.println();
  Serial.println(F("========== LED_REMOTE_CAPTURE_BEGIN =========="));
  Serial.print(F("capture_id="));
  Serial.println(captureSequence);
  Serial.print(F("captured_at_ms="));
  Serial.println(millis());
  Serial.print(F("protocol="));
  Serial.println(typeToString(results.decode_type, results.repeat));
  Serial.print(F("bits="));
  Serial.println(results.bits);
  Serial.print(F("repeat="));
  Serial.println(results.repeat ? F("true") : F("false"));
  Serial.print(F("overflow="));
  Serial.println(results.overflow ? F("true") : F("false"));
  Serial.print(F("raw_length="));
  Serial.println(results.rawlen);
  Serial.println(F("human_readable="));
  Serial.print(resultToHumanReadableBasic(&results));
  Serial.println(F("source_code="));
  Serial.print(resultToSourceCode(&results));
  Serial.println(F("========== LED_REMOTE_CAPTURE_END =========="));
}

void receiveIrFrame() {
  if (!irrecv.decode(&results)) return;

  printCaptureMetadata();
  irrecv.resume();
}

void handleCommand(String command) {
  command.trim();
  command.toLowerCase();
  if (command == "help") {
    printHelp();
  } else if (command == "status") {
    Serial.print(F("[INFO] capture_count="));
    Serial.println(captureSequence);
    Serial.print(F("[INFO] receiver_gpio="));
    Serial.println(kIrReceiverPin);
  } else if (command.length() > 0) {
    Serial.println(F("[WARN] unknown command; type help"));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  irrecv.enableIRIn();

  Serial.println(F("[INFO] LED light remote IR investigation"));
  Serial.println(F("[INFO] VS1838B OUT GPIO5 / receive-only"));
  Serial.println(F("[INFO] IR LED GPIO4 is not used by this sketch"));
  printHelp();
}

void loop() {
  receiveIrFrame();
  if (Serial.available() > 0) {
    handleCommand(Serial.readStringUntil('\n'));
  }
}
