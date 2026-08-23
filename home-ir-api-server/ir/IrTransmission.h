#pragma once

#include <stdint.h>

// ProtocolDefault lets the protocol implementation choose its native repeat
// waveform. NecStandard explicitly reproduces the standard NEC repeat raster
// and FullFrame is available for devices that require repeated complete frames.
enum class IrRepeatMode : uint8_t {
  ProtocolDefault,
  NecStandard,
  FullFrame,
};

struct IrTransmitProfile {
  // Number of additional frames after the first frame.
  uint16_t repeats;
  // Used only by FullFrame. NecStandard uses a fixed 110 ms start-to-start
  // period defined by the NEC protocol.
  uint32_t interFrameGapUs;
  IrRepeatMode repeatMode;
};

constexpr IrTransmitProfile kDefaultIrTransmitProfile = {
    0, 0, IrRepeatMode::ProtocolDefault};
