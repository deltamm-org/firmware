// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace afsk_decode {

/// Audio read from a file, as levels around 0 at its own rate.
struct Audio
{
    std::uint32_t rateHz = 0;
    std::vector<float> samples;
};

/// Reads a mono PCM WAV file (8, 16 or 32 bits, or 32-bit float). False
/// with @p error set if it is not one.
bool readAudio(const std::string &path, Audio &out, std::string &error);

/// The audio at @p rateHz, band-limited below half the lower of the two
/// rates.
std::vector<float> resample(const Audio &in, std::uint32_t rateHz);

/// Writes mono 16-bit PCM, scaled so the largest level is near full scale.
bool writeWav(const std::string &path, const std::vector<float> &samples, std::uint32_t rateHz);

} // namespace afsk_decode
