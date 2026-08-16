void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-C6 boot OK");
}

void loop() {
  Serial.println("alive");
  delay(1000);
}
