// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "dmm/lib/fn_ref.h"
#include "dmm/services/ax25.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::services {

/// Receives 1200 baud Bell 202 AFSK packets (HDLC framed AX.25) from audio
/// sampled at 9600 Hz.
///
/// The audio goes through a band-pass filter, then through mark (1200 Hz)
/// and space (2200 Hz) correlators over one bit. Several slicers compare
/// the two tone levels, each through its own tilt filter that favours one
/// tone by a set number of dB: the level difference between the tones
/// ("twist") depends on the sender's pre-emphasis and the receiver's
/// de-emphasis, and the slicers whose tilt matches it see the cleanest
/// bits; noise also hits the slicers differently. Each slicer recovers its
/// own bit clock with a phase-locked loop that is nudged at every tone
/// change and follows the sender's bit rate, undoes NRZI and bit stuffing,
/// looks for a flag at every bit and checks the frame check sequence of
/// each frame between flags. Optionally a frame whose check failed is
/// repaired when one bit, or two neighbouring bits (one wrong tone),
/// explain the difference and the repaired frame has a well-formed UI
/// header; it is dropped if any slicer read a frame without a repair
/// within a second of it, as it is then most likely a damaged copy. A
/// frame that several slicers find is passed on once.
///
/// Heap-free; about 3.5 KB of state.
class AfskDemodulator
{
public:
    static constexpr std::uint32_t SampleRateHz = 9600;
    static constexpr std::size_t Slicers = 7;
    /// How much each slicer's tilt filter favours the mark tone over the
    /// space tone, in dB.
    static constexpr std::array<std::int8_t, Slicers> MarkGainDb{-6, -3, 0, 2, 4, 6, 9};
    /// Longest frame passed on, from the first address to the check.
    static constexpr std::size_t MaxFrame = ax25::MaxFrameBytes;
    /// Shortest: two addresses, control, protocol and the check.
    static constexpr std::size_t MinFrame = 7 * 2 + 2 + 2;

    struct FrameInfo
    {
        /// The slicer that found it first.
        std::uint8_t slicer = 0;
        /// It was repaired (one bit or one tone).
        bool repaired = false;
    };

    struct Stats
    {
        /// Frames with a right check each slicer found, copies included.
        std::array<std::uint32_t, Slicers> frames{};
        /// Frames of a possible length whose check failed, per slicer.
        std::array<std::uint32_t, Slicers> failed{};
        /// Frames passed on, of them repaired, and copies dropped.
        std::uint32_t passed = 0;
        std::uint32_t repaired = 0;
        std::uint32_t duplicates = 0;
        /// Samples taken in.
        std::uint32_t samples = 0;
    };

    /// Called with each frame, from the first address to the check; the
    /// bytes are valid during the call only.
    using Sink = lib::FnRef<void(std::span<const std::uint8_t> frame, const FrameInfo &info)>;

    AfskDemodulator();

    /// Forgets the audio and frames in progress (after a gap in the
    /// samples); the statistics stay.
    void reset();
    void clearStats() { m_stats = {}; }

    /// Repairing frames on or off (off at start).
    void setRepair(bool on) { m_repair = on; }
    bool repair() const { return m_repair; }

    /// Takes samples in mV (any DC level) or as signed levels.
    void process(std::span<const std::uint16_t> mv, Sink onFrame);
    void process(std::span<const float> samples, Sink onFrame);

    const Stats &stats() const { return m_stats; }

private:
    static constexpr std::size_t FilterTaps = 17;
    static constexpr std::size_t BitSamples = SampleRateHz / 1200;

    struct Slicer
    {
        /// The tilt filter's outer and middle taps.
        std::array<float, 2> tilt{};
        /// The bit clock, in bits: a bit is read as it passes 1.
        float phase = 0.0F;
        /// Added to the bit clock's step: the sender's rate against ours.
        float drift = 0.0F;
        /// The decision value of the previous sample: above 0 for mark.
        float last = 0.0F;
        bool lastTone = false;
        /// A flag was seen and no abort since: bits belong to a frame.
        bool inFrame = false;
        std::uint8_t ones = 0;
        std::uint8_t byte = 0;
        /// Data bits since the last flag.
        std::uint16_t bits = 0;
        std::array<std::uint8_t, MaxFrame> bytes{};
    };

    void sample(float x, Sink &onFrame);
    void slice(std::size_t index, float decision, Sink &onFrame);
    void bit(std::size_t index, bool one, Sink &onFrame);
    void flag(std::size_t index, Sink &onFrame);
    void pass(std::size_t index, std::span<const std::uint8_t> frame, bool repaired, Sink &onFrame);
    void hold(std::size_t index, std::span<const std::uint8_t> frame, Sink &onFrame);
    void passHeld(Sink &onFrame);
    bool tryRepair(std::span<std::uint8_t> frame) const;

    std::array<float, FilterTaps> m_filter{};
    std::array<float, BitSamples> m_markCos{};
    std::array<float, BitSamples> m_markSin{};
    std::array<float, BitSamples> m_spaceCos{};
    std::array<float, BitSamples> m_spaceSin{};
    // Each history twice in a row, so the newest samples always lie in one
    // piece.
    std::array<float, 2 * FilterTaps> m_input{};
    std::array<float, 2 * BitSamples> m_filtered{};
    struct Complex
    {
        float i = 0.0F;
        float q = 0.0F;
    };
    // The correlators' outputs for the last three samples, newest first.
    std::array<Complex, 3> m_mark{};
    std::array<Complex, 3> m_space{};
    std::size_t m_inputAt = 0;
    std::size_t m_filteredAt = 0;
    float m_dc = 0.0F;
    bool m_dcKnown = false;
    std::array<Slicer, Slicers> m_slicers{};
    ax25::RecentFrames m_recent;
    Stats m_stats;
    bool m_repair = false;
    // A repaired frame waiting to be passed on.
    std::array<std::uint8_t, MaxFrame> m_held{};
    std::size_t m_heldLength = 0;
    std::uint32_t m_heldAt = 0;
    std::uint8_t m_heldSlicer = 0;
    // When a frame was last read without a repair.
    std::uint32_t m_cleanAt = 0;
    bool m_cleanSeen = false;
};

} // namespace dmm::services
