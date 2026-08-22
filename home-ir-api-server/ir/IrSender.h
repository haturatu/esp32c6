#pragma once

#include <Arduino.h>
#include <IRsend.h>

class IrSender {
 public:
  explicit IrSender(uint8_t pin);

  void begin();
  bool sendNec(uint64_t data, uint16_t bits = 32, uint16_t repeats = 0);
  bool sendRaw(const uint16_t *timings, size_t length, uint32_t frequency);
  uint8_t pin() const;

 private:
  IRsend sender_;
  uint8_t pin_;
  bool begun_;
};
