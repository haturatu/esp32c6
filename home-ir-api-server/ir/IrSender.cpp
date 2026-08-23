#include "IrSender.h"

namespace {
constexpr uint32_t kMinimumIrSendIntervalMs = 100;
constexpr uint32_t kNecRepeatPeriodUs = 110000;
constexpr uint16_t kNecHeaderMarkUs = 8960;
constexpr uint16_t kNecHeaderSpaceUs = 4480;
constexpr uint16_t kNecRepeatHeaderSpaceUs = 2240;
constexpr uint16_t kNecBitMarkUs = 560;
constexpr uint16_t kNecOneSpaceUs = 1680;
constexpr uint16_t kNecZeroSpaceUs = 560;
constexpr uint16_t kNecCarrierKHz = 38;
constexpr uint8_t kNecDutyCycle = 33;

void delayMicrosecondsLong(uint32_t usec) {
  constexpr uint32_t kMaxAccurateDelayUs = 16000;
  while (usec > kMaxAccurateDelayUs) {
    delayMicroseconds(kMaxAccurateDelayUs);
    usec -= kMaxAccurateDelayUs;
  }
  if (usec > 0) delayMicroseconds(usec);
}

void waitUntil(const uint32_t targetUs) {
  while (true) {
    const int32_t remaining = static_cast<int32_t>(targetUs - micros());
    if (remaining <= 0) return;
    delayMicrosecondsLong(static_cast<uint32_t>(remaining));
  }
}

void sendNecDataFrame(IRsend &sender, const uint64_t data,
                      const uint16_t bits) {
  sender.sendGeneric(
      kNecHeaderMarkUs, kNecHeaderSpaceUs, kNecBitMarkUs, kNecOneSpaceUs,
      kNecBitMarkUs, kNecZeroSpaceUs, kNecBitMarkUs, 0, 0, data, bits,
      kNecCarrierKHz, true, 0, kNecDutyCycle);
}

void sendNecStandard(IRsend &sender, const uint64_t data,
                     const uint16_t bits, const uint16_t repeats) {
  // Standard NEC data frame. There is intentionally no trailing gap here;
  // the next frame is scheduled from the beginning of this frame below.
  const uint32_t firstFrameStartUs = micros();
  sendNecDataFrame(sender, data, bits);

  // NEC repeat is a special 0-bit frame, not a copy of the 32-bit data.
  // Every repeat starts exactly 110 ms after the preceding frame start.
  uint32_t nextFrameStartUs = firstFrameStartUs;
  for (uint16_t i = 0; i < repeats; ++i) {
    nextFrameStartUs += kNecRepeatPeriodUs;
    waitUntil(nextFrameStartUs);
    sender.enableIROut(kNecCarrierKHz, kNecDutyCycle);
    sender.mark(kNecHeaderMarkUs);
    sender.space(kNecRepeatHeaderSpaceUs);
    sender.mark(kNecBitMarkUs);
  }
}
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
                               const IrTransmitProfile &profile,
                               const bool waitForInterval) {
  if (bits == 0 || bits > 64) return IrSendResult::InvalidCode;

  const IrSendResult ready = prepareSend(waitForInterval);
  if (ready != IrSendResult::Ok) return ready;

  switch (profile.repeatMode) {
    case IrRepeatMode::ProtocolDefault:
      sender_.sendNEC(data, bits, profile.repeats);
      break;
    case IrRepeatMode::NecStandard:
      sendNecStandard(sender_, data, bits, profile.repeats);
      break;
    case IrRepeatMode::FullFrame:
      for (uint16_t i = 0; i <= profile.repeats; ++i) {
        sendNecDataFrame(sender_, data, bits);
        if (i < profile.repeats && profile.interFrameGapUs > 0) {
          delayMicrosecondsLong(profile.interFrameGapUs);
        }
      }
      break;
  }

  finishSend();
  return IrSendResult::Ok;
}

IrSendResult IrSender::sendNec(const uint64_t data, const uint16_t bits,
                               const uint16_t repeats,
                               const bool waitForInterval) {
  const IrTransmitProfile profile = {
      repeats, 0, IrRepeatMode::ProtocolDefault};
  return sendNec(data, bits, profile, waitForInterval);
}

IrSendResult IrSender::sendRaw(const uint16_t *timings, const size_t length,
                               const uint32_t frequency,
                               const IrTransmitProfile &profile,
                               const bool waitForInterval) {
  if (timings == nullptr || length == 0 || frequency == 0) {
    return IrSendResult::InvalidCode;
  }

  const IrSendResult ready = prepareSend(waitForInterval);
  if (ready != IrSendResult::Ok) return ready;

  const uint16_t frequencyKHz = frequency / 1000;
  if (frequencyKHz == 0) return IrSendResult::InvalidCode;

  const uint32_t frameCount = static_cast<uint32_t>(profile.repeats) + 1;
  for (uint32_t i = 0; i < frameCount; ++i) {
    sender_.sendRaw(timings, length, frequencyKHz);
    if (i + 1 < frameCount && profile.interFrameGapUs > 0) {
      delayMicroseconds(profile.interFrameGapUs);
    }
  }

  finishSend();
  return IrSendResult::Ok;
}

IrSendResult IrSender::sendRaw(const uint16_t *timings, const size_t length,
                               const uint32_t frequency,
                               const bool waitForInterval) {
  return sendRaw(timings, length, frequency, kDefaultIrTransmitProfile,
                 waitForInterval);
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
