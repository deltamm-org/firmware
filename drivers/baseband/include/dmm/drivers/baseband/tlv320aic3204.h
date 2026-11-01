// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <algorithm>
#include <cstdint>

/// The TLV320AIC3204 audio codec's registers (page, register) and value
/// codes, from TI's application reference guide (SLAA557).
namespace dmm::drivers::baseband::tlv320aic3204 {

inline constexpr std::uint8_t Page0 = 0;
inline constexpr std::uint8_t Page1 = 1;

// Page 0.
inline constexpr std::uint8_t LeftDacVolume = 65;  // signed, 0.5 dB steps
inline constexpr std::uint8_t LeftAgcControl = 86; // on, target level, gain hysteresis
inline constexpr std::uint8_t LeftAgcMaxGain = 88; // the most gain the AGC applies

// Page 1.
inline constexpr std::uint8_t HplGain = 16; // headphone-left driver gain, 6-bit signed dB
inline constexpr std::uint8_t LeftOutGain = 18;
inline constexpr std::uint8_t RightOutGain = 19;
inline constexpr std::uint8_t LeftMicPositive = 52; // left microphone amplifier inputs; 0 = none
inline constexpr std::uint8_t LeftMicPgaVolume = 59;
inline constexpr std::uint8_t RightMicPgaVolume = 60;

inline constexpr std::uint8_t HplMuted = 0x40;
/// IN1L on the left microphone amplifier through 20 kOhm.
inline constexpr std::uint8_t MicIn1Left20k = 0x80;
inline constexpr std::uint8_t AgcOn = 0x80;
/// AGC target: the average of the ADC output held 10 dB below full scale.
inline constexpr std::uint8_t AgcTargetMinus10Db = 0x20;

/// The microphone amplifier's volume reaches 47.5 dB, in 0.5 dB steps.
inline constexpr int MicPgaMaxDb = 47;
/// The AGC applies up to 58 dB, in 0.5 dB steps.
inline constexpr int AgcMaxGainMaxDb = 58;

/// The microphone amplifier volume for @p db, limited to 0..MicPgaMaxDb.
/// Bit 7 clear: the volume in use.
constexpr std::uint8_t micPgaVolume(int db)
{
    return static_cast<std::uint8_t>(2 * std::clamp(db, 0, MicPgaMaxDb));
}

/// The AGC's largest gain for @p db, limited to 0..AgcMaxGainMaxDb.
constexpr std::uint8_t agcMaxGain(int db)
{
    return static_cast<std::uint8_t>(2 * std::clamp(db, 0, AgcMaxGainMaxDb));
}

/// The headphone-left driver gain field for @p db (-6..+29).
constexpr std::uint8_t hplDb(int db)
{
    return static_cast<std::uint8_t>(db & 0x3F);
}

} // namespace dmm::drivers::baseband::tlv320aic3204
