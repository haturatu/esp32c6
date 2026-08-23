#pragma once

#include <Arduino.h>
#include <IRsend.h>
#include <ir_Daikin.h>

#include "IrTransmission.h"

enum class IrSendResult : uint8_t {
  Ok,
  NotConfigured,
  NotInitialized,
  InvalidCode,
  RateLimited,
  SendFailed,
};

class IrSender {
 public:
  explicit IrSender(uint8_t pin);

  void begin();
  IrSendResult sendNec(uint64_t data, uint16_t bits,
                       const IrTransmitProfile &profile,
                       bool waitForInterval = false);
  IrSendResult sendNec(uint64_t data, uint16_t bits = 32, uint16_t repeats = 0,
                       bool waitForInterval = false);
  IrSendResult sendRaw(const uint16_t *timings, size_t length,
                       uint32_t frequency, const IrTransmitProfile &profile,
                       bool waitForInterval = false);
  IrSendResult sendRaw(const uint16_t *timings, size_t length,
                       uint32_t frequency, bool waitForInterval = false);
  IrSendResult sendDaikin(uint16_t repeats = 0);
  IRDaikinESP &daikin();
  uint8_t pin() const;
  bool hasSent() const;
  uint32_t lastSendAtMs() const;

 private:
  IrSendResult prepareSend(bool waitForInterval);
  void finishSend();

  IRsend sender_;
  IRDaikinESP daikin_;
  uint8_t pin_;
  bool begun_;
  bool hasSent_;
  uint32_t lastSendAtMs_;
};
