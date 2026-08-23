#include "IrSender.h"

namespace {
constexpr uint32_t kMinimumIrSendIntervalMs = 100;
}

IrSender::IrSender(const uint8_t pin)
    : sender_(pin),
      daikin_(pin),
      pin_(pin),
      begun_(false),
      hasSent_(false),
      lastSendAtMs_(0) {}

void IrSender::begin() {
  sender_.begin();
  daikin_.begin();
  digitalWrite(pin_, LOW);
  begun_ = true;
}

IrSendResult IrSender::sendNec(const uint64_t data, const uint16_t bits,
                               const uint16_t repeats,
                               const bool waitForInterval) {
  const IrSendResult ready = prepareSend(waitForInterval);
  if (ready != IrSendResult::Ok) return ready;
  if (bits == 0 || bits > 64) return IrSendResult::InvalidCode;
  sender_.sendNEC(data, bits, repeats);
  finishSend();
  return IrSendResult::Ok;
}

IrSendResult IrSender::sendRaw(const uint16_t *timings, const size_t length,
                               const uint32_t frequency,
                               const bool waitForInterval) {
  const IrSendResult ready = prepareSend(waitForInterval);
  if (ready != IrSendResult::Ok) return ready;
  if (timings == nullptr || length == 0 || frequency == 0) {
    return IrSendResult::InvalidCode;
  }
  sender_.sendRaw(timings, length, frequency / 1000);
  finishSend();
  return IrSendResult::Ok;
}

IrSendResult IrSender::sendDaikin(const uint16_t repeats) {
  const IrSendResult ready = prepareSend(true);
  if (ready != IrSendResult::Ok) return ready;
  daikin_.send(repeats);
  finishSend();
  return IrSendResult::Ok;
}

IrSendResult IrSender::prepareSend(const bool waitForInterval) {
  if (!begun_) return IrSendResult::NotInitialized;
  if (!hasSent_) return IrSendResult::Ok;

  const uint32_t elapsed = millis() - lastSendAtMs_;
  if (elapsed >= kMinimumIrSendIntervalMs) return IrSendResult::Ok;
  if (!waitForInterval) return IrSendResult::RateLimited;
  delay(kMinimumIrSendIntervalMs - elapsed);
  return IrSendResult::Ok;
}

void IrSender::finishSend() {
  digitalWrite(pin_, LOW);
  hasSent_ = true;
  lastSendAtMs_ = millis();
}

IRDaikinESP &IrSender::daikin() { return daikin_; }

uint8_t IrSender::pin() const { return pin_; }

bool IrSender::hasSent() const { return hasSent_; }

uint32_t IrSender::lastSendAtMs() const { return lastSendAtMs_; }
