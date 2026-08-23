#pragma once

#include <stdint.h>

// ProtocolDefault lets the protocol implementation choose its native repeat
// waveform. For NEC this is one full frame followed by NEC repeat frames.
// FullFrame is available for devices that require repeated complete frames.
enum class IrRepeatMode : uint8_t {
  ProtocolDefault,
  FullFrame,
};

struct IrTransmitProfile {
  // Number of additional frames after the first frame.
  uint16_t repeats;
  // Used only by FullFrame. ProtocolDefault uses the protocol's timing.
  uint32_t interFrameGapUs;
  IrRepeatMode repeatMode;
};

constexpr IrTransmitProfile kDefaultIrTransmitProfile = {
    0, 0, IrRepeatMode::ProtocolDefault};
