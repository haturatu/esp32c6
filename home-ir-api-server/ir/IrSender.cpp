#include "IrSender.h"

IrSender::IrSender(const uint8_t pin) : sender_(pin), pin_(pin), begun_(false) {}

void IrSender::begin() {
  sender_.begin();
  digitalWrite(pin_, LOW);
  begun_ = true;
}

bool IrSender::sendNec(const uint64_t data, const uint16_t bits,
                       const uint16_t repeats) {
  if (!begun_) return false;
  sender_.sendNEC(data, bits, repeats);
  digitalWrite(pin_, LOW);
  return true;
}

bool IrSender::sendRaw(const uint16_t *timings, const size_t length,
                       const uint32_t frequency) {
  if (!begun_ || timings == nullptr || length == 0 || frequency == 0) {
    return false;
  }
  sender_.sendRaw(timings, length, frequency / 1000);
  digitalWrite(pin_, LOW);
  return true;
}

uint8_t IrSender::pin() const { return pin_; }
