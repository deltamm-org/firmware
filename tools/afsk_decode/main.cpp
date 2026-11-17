// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

// Decodes APRS packets from a mono WAV recording with the radio's own
// demodulator.

#include "audio_file.h"

#include "dmm/lib/format.h"
#include "dmm/services/afsk_demodulator.h"
#include "dmm/services/ax25.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dmm::services::AfskDemodulator;
namespace ax25 = dmm::services::ax25;

constexpr std::uint32_t MinRateHz = 8000;

int usage()
{
    std::fprintf(stderr, "usage: afsk-decode [--repair] [--save <file.wav>] <recording>\n"
                         "  <recording>  a mono WAV file (8000 Hz or more; resampled to %u Hz)\n"
                         "  --repair     also repair frames with one wrong bit or tone\n"
                         "  --save       write the audio as the decoder takes it, as a %u Hz WAV\n",
        AfskDemodulator::SampleRateHz, AfskDemodulator::SampleRateHz);
    return 2;
}

void printFrame(std::span<const std::uint8_t> bytes, const AfskDemodulator::FrameInfo &info, std::uint32_t sample)
{
    std::printf("%8.2f s  slicer %+3d dB%s  ", static_cast<double>(sample) / AfskDemodulator::SampleRateHz,
        AfskDemodulator::MarkGainDb[info.slicer], info.repaired ? ", repaired" : "");
    static ax25::Frame frame;
    if (ax25::decode(bytes, frame)) {
        dmm::lib::StaticString<ax25::MaxFrameBytes + 100> text;
        ax25::writeText(text, frame);
        std::string printable;
        for (char c : text.view()) {
            printable.push_back(c >= ' ' && c <= '~' ? c : '.');
        }
        std::printf("%s\n", printable.c_str());
        return;
    }
    std::printf("not a UI frame, %zu bytes:", bytes.size());
    for (std::uint8_t b : bytes) {
        std::printf(" %02X", b);
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char **argv)
{
    bool repair = false;
    std::string save;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--repair") == 0) {
            repair = true;
        } else if (std::strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            save = argv[++i];
        } else if (argv[i][0] == '-' || !path.empty()) {
            return usage();
        } else {
            path = argv[i];
        }
    }
    if (path.empty()) {
        return usage();
    }

    afsk_decode::Audio audio;
    std::string error;
    if (!afsk_decode::readAudio(path, audio, error)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return 1;
    }
    if (audio.rateHz < MinRateHz) {
        std::fprintf(stderr, "%s: %u Hz is too low; record at %u Hz or more\n", path.c_str(), audio.rateHz, MinRateHz);
        return 1;
    }
    std::printf("%s: %zu samples at %u Hz (%.1f s)", path.c_str(), audio.samples.size(), audio.rateHz,
        static_cast<double>(audio.samples.size()) / audio.rateHz);
    if (audio.rateHz != AfskDemodulator::SampleRateHz) {
        std::printf(", resampled to %u Hz", AfskDemodulator::SampleRateHz);
    }
    std::printf("\n");

    const std::vector<float> samples = afsk_decode::resample(audio, AfskDemodulator::SampleRateHz);
    if (!save.empty() && !afsk_decode::writeWav(save, samples, AfskDemodulator::SampleRateHz)) {
        std::fprintf(stderr, "cannot write %s\n", save.c_str());
        return 1;
    }

    AfskDemodulator demodulator;
    demodulator.setRepair(repair);
    // In blocks, as on the radio; the sample count tells the time.
    constexpr std::size_t Block = 192;
    for (std::size_t at = 0; at < samples.size(); at += Block) {
        const std::size_t n = std::min(Block, samples.size() - at);
        demodulator.process(std::span<const float>(samples.data() + at, n),
            [&demodulator](std::span<const std::uint8_t> bytes, const AfskDemodulator::FrameInfo &info) {
                printFrame(bytes, info, demodulator.stats().samples);
            });
    }

    const AfskDemodulator::Stats &s = demodulator.stats();
    std::printf("%u frames (%u repaired), %u copies dropped\n", s.passed, s.repaired, s.duplicates);
    for (std::size_t i = 0; i < AfskDemodulator::Slicers; ++i) {
        std::printf("  slicer %+3d dB: %u right, %u failed the check\n", AfskDemodulator::MarkGainDb[i], s.frames[i], s.failed[i]);
    }
    return 0;
}
