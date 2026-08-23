#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../ir/IrTransmission.h"

enum class LightCommand : uint8_t {
  PowerOn,
  PowerOff,
  Full,
  Brighter,
  Dimmer,
  Cooler,
  Warmer,
  Toggle,
  NightLight,
  Cancel,
  Timer15Min,
  Timer30Min,
};

enum class IrProtocol : uint8_t { None, Nec, Raw };

struct IrCode {
  IrProtocol protocol;
  uint64_t data;
  uint16_t bits;
  const uint16_t *timings;
  size_t length;
  uint32_t frequency;
  IrTransmitProfile tx;
  bool configured;
};

namespace LightCodes {

// One complete NEC frame plus two standard NEC repeat frames on a 110 ms
// start-to-start raster. This is kept in the code table so different remotes
// can be tuned without changing the device or API layers.
constexpr IrTransmitProfile kLightTransmitProfile = {
    2, 0, IrRepeatMode::NecStandard};

constexpr IrCode Unknown = {IrProtocol::None, 0, 0, nullptr, 0, 0,
                            kDefaultIrTransmitProfile, false};
constexpr IrCode On = {IrProtocol::Nec, 0x807F00FF, 32, nullptr, 0, 38000,
                       kLightTransmitProfile, true};
constexpr IrCode Off = {IrProtocol::Nec, 0x807F807F, 32, nullptr, 0, 38000,
                        kLightTransmitProfile, true};
constexpr IrCode Full = {IrProtocol::Nec, 0x807F609F, 32, nullptr, 0, 38000,
                         kLightTransmitProfile, true};
constexpr IrCode Brighter = {IrProtocol::Nec, 0x807FA05F, 32, nullptr, 0,
                             38000, kLightTransmitProfile, true};
constexpr IrCode Dimmer = {IrProtocol::Nec, 0x807F20DF, 32, nullptr, 0, 38000,
                           kLightTransmitProfile, true};
constexpr IrCode Cooler = {IrProtocol::Nec, 0x807F40BF, 32, nullptr, 0, 38000,
                           kLightTransmitProfile, true};
constexpr IrCode Warmer = {IrProtocol::Nec, 0x807F50AF, 32, nullptr, 0, 38000,
                           kLightTransmitProfile, true};
constexpr IrCode Toggle = {IrProtocol::Nec, 0x807FC03F, 32, nullptr, 0, 38000,
                           kLightTransmitProfile, true};
constexpr IrCode NightLight = {IrProtocol::Nec, 0x807FD02F, 32, nullptr, 0,
                               38000, kLightTransmitProfile, true};
constexpr IrCode Cancel = {IrProtocol::Nec, 0x807FE01F, 32, nullptr, 0, 38000,
                           kLightTransmitProfile, true};
constexpr IrCode Timer15Min = {IrProtocol::Nec, 0x807F22DD, 32, nullptr, 0,
                               38000, kLightTransmitProfile, true};
constexpr IrCode Timer30Min = {IrProtocol::Nec, 0x807FFF00, 32, nullptr, 0,
                               38000, kLightTransmitProfile, true};

inline const IrCode &forCommand(const LightCommand command) {
  switch (command) {
    case LightCommand::PowerOn: return On;
    case LightCommand::PowerOff: return Off;
    case LightCommand::Full: return Full;
    case LightCommand::Brighter: return Brighter;
    case LightCommand::Dimmer: return Dimmer;
    case LightCommand::Cooler: return Cooler;
    case LightCommand::Warmer: return Warmer;
    case LightCommand::Toggle: return Toggle;
    case LightCommand::NightLight: return NightLight;
    case LightCommand::Cancel: return Cancel;
    case LightCommand::Timer15Min: return Timer15Min;
    case LightCommand::Timer30Min: return Timer30Min;
  }
  return Unknown;
}

}  // namespace LightCodes
