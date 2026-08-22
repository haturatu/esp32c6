#include "IrSender.h"

IrSender::IrSender(const uint8_t pin)
    : sender_(pin), daikin_(pin), pin_(pin), begun_(false) {}

void IrSender::begin() {
  sender_.begin();
  daikin_.begin();
  digitalWrite(pin_, LOW);
  begun_ = true;
}

IrSendResult IrSender::sendNec(const uint64_t data, const uint16_t bits,
                               const uint16_t repeats) {
  if (!begun_) return IrSendResult::NotInitialized;
  if (bits == 0 || bits > 64) return IrSendResult::InvalidCode;
  sender_.sendNEC(data, bits, repeats);
  digitalWrite(pin_, LOW);
  return IrSendResult::Ok;
}

IrSendResult IrSender::sendRaw(const uint16_t *timings, const size_t length,
                               const uint32_t frequency) {
  if (!begun_ || timings == nullptr || length == 0 || frequency == 0) {
    return begun_ ? IrSendResult::InvalidCode : IrSendResult::NotInitialized;
  }
  sender_.sendRaw(timings, length, frequency / 1000);
  digitalWrite(pin_, LOW);
  return IrSendResult::Ok;
}

IRDaikinESP &IrSender::daikin() { return daikin_; }

uint8_t IrSender::pin() const { return pin_; }
