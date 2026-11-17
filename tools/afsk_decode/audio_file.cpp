// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "audio_file.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace afsk_decode {

namespace {

constexpr double Pi = 3.14159265358979323846;

std::uint32_t le16(const unsigned char *p)
{
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8));
}

std::uint32_t le32(const unsigned char *p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

bool readWav(const std::vector<unsigned char> &file, Audio &out, std::string &error)
{
    if (file.size() < 12 || std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
        error = "not a WAV file";
        return false;
    }
    std::uint32_t format = 0;
    std::uint32_t channels = 0;
    std::uint32_t bits = 0;
    bool haveFormat = false;
    std::size_t at = 12;
    while (at + 8 <= file.size()) {
        const unsigned char *chunk = file.data() + at;
        const std::uint32_t size = le32(chunk + 4);
        const std::size_t body = at + 8;
        const std::size_t available = std::min<std::size_t>(size, file.size() - body);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && available >= 16) {
            format = le16(file.data() + body);
            channels = le16(file.data() + body + 2);
            out.rateHz = le32(file.data() + body + 4);
            bits = le16(file.data() + body + 14);
            // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format.
            if (format == 0xFFFE && available >= 26) {
                format = le16(file.data() + body + 24);
            }
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!haveFormat) {
                error = "WAV data before its format";
                return false;
            }
            if (channels != 1) {
                error = "the WAV file has " + std::to_string(channels) + " channels; make it mono first";
                return false;
            }
            const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 32);
            const bool floats = format == 3 && bits == 32;
            if (!pcm && !floats) {
                error = "unsupported WAV sample format " + std::to_string(format) + " with " + std::to_string(bits) +
                    " bits; use 8, 16 or 32-bit PCM or 32-bit float";
                return false;
            }
            const std::size_t bytes = bits / 8;
            const unsigned char *p = file.data() + body;
            for (std::size_t i = 0; i + bytes <= available; i += bytes) {
                float v = 0.0F;
                if (floats) {
                    std::uint32_t u = le32(p + i);
                    std::memcpy(&v, &u, sizeof v);
                } else if (bits == 8) {
                    v = (static_cast<float>(p[i]) - 128.0F) / 128.0F;
                } else if (bits == 16) {
                    v = static_cast<float>(static_cast<std::int16_t>(le16(p + i))) / 32768.0F;
                } else {
                    v = static_cast<float>(static_cast<std::int32_t>(le32(p + i))) / 2147483648.0F;
                }
                out.samples.push_back(v);
            }
            return true;
        }
        at = body + size + (size & 1);
    }
    error = "the WAV file has no data";
    return false;
}

} // namespace

bool readAudio(const std::string &path, Audio &out, std::string &error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    const std::vector<unsigned char> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out = Audio{};
    if (file.size() < 4 || std::memcmp(file.data(), "RIFF", 4) != 0) {
        error = "not a WAV file";
        return false;
    }
    return readWav(file, out, error);
}

std::vector<float> resample(const Audio &in, std::uint32_t rateHz)
{
    if (in.rateHz == rateHz) {
        return in.samples;
    }
    // A windowed-sinc low-pass at 90 % of the lower Nyquist frequency,
    // taken at each output instant.
    const double step = static_cast<double>(in.rateHz) / rateHz;
    const double cutoff = 0.45 / std::max(step, 1.0);
    const int half = static_cast<int>(std::ceil(6.0 / cutoff));
    const auto outCount = static_cast<std::size_t>(static_cast<double>(in.samples.size()) / step);
    std::vector<float> out(outCount);
    for (std::size_t n = 0; n < outCount; ++n) {
        const double t = static_cast<double>(n) * step;
        const auto centre = static_cast<long>(std::floor(t));
        double sum = 0.0;
        for (long k = centre - half + 1; k <= centre + half; ++k) {
            if (k < 0 || k >= static_cast<long>(in.samples.size())) {
                continue;
            }
            const double u = t - static_cast<double>(k);
            const double x = 2.0 * cutoff * u;
            const double sinc = x == 0.0 ? 1.0 : std::sin(Pi * x) / (Pi * x);
            const double window = 0.5 + 0.5 * std::cos(Pi * u / half);
            sum += in.samples[static_cast<std::size_t>(k)] * 2.0 * cutoff * sinc * window;
        }
        out[n] = static_cast<float>(sum);
    }
    return out;
}

bool writeWav(const std::string &path, const std::vector<float> &samples, std::uint32_t rateHz)
{
    float peak = 1e-9F;
    for (float v : samples) {
        peak = std::max(peak, std::fabs(v));
    }
    const auto put16 = [](std::string &s, std::uint32_t v) {
        s.push_back(static_cast<char>(v & 0xFF));
        s.push_back(static_cast<char>((v >> 8) & 0xFF));
    };
    const auto put32 = [&put16](std::string &s, std::uint32_t v) {
        put16(s, v & 0xFFFF);
        put16(s, v >> 16);
    };
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * 2);
    std::string header = "RIFF";
    put32(header, 36 + dataBytes);
    header += "WAVEfmt ";
    put32(header, 16);
    put16(header, 1);
    put16(header, 1);
    put32(header, rateHz);
    put32(header, rateHz * 2);
    put16(header, 2);
    put16(header, 16);
    header += "data";
    put32(header, dataBytes);
    std::string data;
    data.reserve(dataBytes);
    for (float v : samples) {
        const auto s = static_cast<std::int16_t>(std::lround(v / peak * 30000.0F));
        put16(data, static_cast<std::uint16_t>(s));
    }
    std::ofstream out(path, std::ios::binary);
    out << header << data;
    return static_cast<bool>(out);
}

} // namespace afsk_decode
