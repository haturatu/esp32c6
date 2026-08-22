#pragma once

#include <Arduino.h>
#include <IRsend.h>
#include <ir_Daikin.h>

enum class IrSendResult : uint8_t {
  Ok,
  NotConfigured,
  NotInitialized,
  InvalidCode,
  SendFailed,
};

class IrSender {
 public:
  explicit IrSender(uint8_t pin);

  void begin();
  IrSendResult sendNec(uint64_t data, uint16_t bits = 32, uint16_t repeats = 0);
  IrSendResult sendRaw(const uint16_t *timings, size_t length, uint32_t frequency);
  IRDaikinESP &daikin();
  uint8_t pin() const;

 private:
  IRsend sender_;
  IRDaikinESP daikin_;
  uint8_t pin_;
  bool begun_;
};
