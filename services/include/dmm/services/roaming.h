// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

/// DMR roaming: the repeaters heard, how the repeater in use is doing, when
/// to listen to the others and which one to use.
/// Positions are places in the roaming zone, 0 to MaxRoamingSites - 1.
/// Times are milliseconds on a clock that may wrap.
namespace dmm::services::roaming {

/// The repeaters of the roaming zone heard lately, a few at a time, with
/// their level and the floor it is judged against, in dB above it: a
/// reference the radio model sets per band.
class HeardList
{
public:
    static constexpr std::size_t Capacity = 8;
    /// A level is current for this long after it was measured or the
    /// repeater was last heard.
    static constexpr std::uint32_t ValidMs = 120'000;

    struct Entry
    {
        std::uint8_t position = 0;
        std::int8_t levelDbm = 0;
        std::int8_t floorDbm = 0;
        std::uint8_t samples = 0; // levels measured while current, up to 255; 0: none
        bool unanswered = false;  // a wake-up went unanswered since it was last heard
        std::uint32_t measuredMs = 0;

        int levelDb() const { return levelDbm - floorDbm; }
        bool current(std::uint32_t nowMs) const { return samples > 0 && nowMs - measuredMs < ValidMs; }
    };

    void clear() { m_count = 0; }
    /// Repeater @p position (-1: none) is in use: a full list never drops
    /// it, so that how it was last heard stays known while others come and
    /// go.
    void keep(int position) { m_keep = position; }
    /// Repeater @p position heard at @p levelDbm on a channel whose floor is
    /// @p floorDbm.
    void heard(std::uint8_t position, int levelDbm, int floorDbm, std::uint32_t nowMs);
    /// Repeater @p position is heard again without a new level: a current
    /// level stays current.
    void stillHeard(std::uint8_t position, std::uint32_t nowMs);
    /// Repeater @p position did not answer a wake-up: until it is heard
    /// again.
    void unanswered(std::uint8_t position, std::uint32_t nowMs);

    const Entry *find(std::uint8_t position) const;
    std::span<const Entry> entries() const { return {m_entries, m_count}; }

private:
    Entry &place(std::uint8_t position, std::uint32_t nowMs);

    Entry m_entries[Capacity];
    std::size_t m_count = 0;
    int m_keep = -1;
};

/// The order a search tries the zone's @p zoneSize repeaters in: @p current
/// (when >= 0), then the heard list, current levels before old ones and
/// strongest first, then the rest of the zone in its own order. Each
/// position once. Returns how many it wrote to @p out.
std::size_t searchOrder(const HeardList &heard, int current, std::size_t zoneSize, std::uint32_t nowMs,
    std::span<std::uint8_t> out);

/// Which repeater a search settles on, fed with each repeater it tried in
/// order. Levels are in dB above each channel's floor. A search stops at
/// the first repeater at or above @p goodDb; otherwise it takes the
/// strongest that answered, the first tried of equals: the repeater in
/// use, which a search tries first.
class Pick
{
public:
    explicit Pick(int goodDb) : m_goodDb(goodDb) {}
    /// Repeater @p position answered at @p levelDb, or did not answer.
    /// True when the search can stop.
    bool tried(int position, bool answered, int levelDb);
    /// The repeater to use; -1 when none answered.
    int chosen() const { return m_best; }

private:
    int m_goodDb;
    int m_best = -1;
    int m_bestDb = 0;
};

/// Signal levels that steer roaming, in dB above each channel's floor. A
/// repeater still decodes close to its floor, where the level can no
/// longer be told from no signal.
struct Levels
{
    int goodDb = 10;  // a search stops here; a quiet repeater is left for one this strong
    int weakDb = 6;   // below: look for a better repeater
    int marginDb = 6; // better by this to leave a weak repeater
};

/// How the repeater in use is doing.
enum class Health : std::uint8_t
{
    Good,
    Weak,  // below the weak level, or heard lately but a wake-up went unanswered
    Quiet, // not heard lately, last heard at the weak level or above, or none in use yet
    Lost,  // not heard lately, and a wake-up went unanswered or it was last heard below the weak level
};

/// The health of repeater @p site (-1: none in use).
Health health(const HeardList &heard, int site, const Levels &levels, std::uint32_t nowMs);

/// The repeater to move to from @p site, or -1 to stay. Weak: the
/// strongest current one, measured twice, better by the margin. Quiet: the
/// strongest current one at the good level. Lost: the strongest current
/// one; after fading out, only when it is stronger than the one in use was
/// last heard.
int moveTo(const HeardList &heard, int site, Health state, const Levels &levels, std::uint32_t nowMs);

/// The transmissions of the repeater in use, from the DMR heard on its
/// output: whether it carries traffic, and whether it sends idle bursts
/// only (a beacon, or the hang time after a call).
class Activity
{
public:
    /// DMR heard this recently: on the air.
    static constexpr std::uint32_t OnAirMs = 2'000;
    /// A call starts with idle bursts too; its header follows within this.
    static constexpr std::uint32_t SettleMs = 1'500;
    /// The overs of a call follow each other within this.
    static constexpr std::uint32_t CallGapMs = 10'000;

    /// DMR heard from it; @p call: a burst of a call, not an idle one.
    void heard(bool call, std::uint32_t nowMs);
    /// Nothing known: another repeater is in use.
    void clear() { *this = {}; }
    /// On the air, or between the overs of a call.
    bool busy(std::uint32_t nowMs) const { return onAir(nowMs) || callLately(nowMs); }
    /// On the air for a while with idle bursts only, and no call lately.
    bool idle(std::uint32_t nowMs) const
    {
        return onAir(nowMs) && nowMs - m_startMs >= SettleMs && !callLately(nowMs);
    }

private:
    bool onAir(std::uint32_t nowMs) const { return m_heard && nowMs - m_lastMs < OnAirMs; }
    bool callLately(std::uint32_t nowMs) const { return m_call && nowMs - m_callMs < CallGapMs; }

    bool m_heard = false;
    bool m_call = false;
    std::uint32_t m_startMs = 0;
    std::uint32_t m_lastMs = 0;
    std::uint32_t m_callMs = 0;
};

/// Listening to the zone's other repeaters without transmitting: when a
/// check may start, and which repeater it looks at. A check tunes to a
/// repeater's output for a moment and reads its level if it is on the air.
class Listening
{
public:
    /// Zone positions a pass can hold: as many as a roaming zone has.
    static constexpr std::size_t MaxZone = 64;
    /// Good or weak: one check at most this often, while the repeater in
    /// use sends idle bursts.
    static constexpr std::uint32_t HopMs = 400;
    /// Quiet or lost: passes one after another for BurstMs, long enough to
    /// span a repeater's beacon period; then a gap. Quiet: doubling from
    /// FirstGapMs up to MaxGapMs, kept short so that a repeater met after a
    /// long stretch without any is found soon. Lost: always FirstGapMs, as
    /// no call on the repeater in use can be missed then.
    static constexpr std::uint32_t BurstMs = 60'000;
    static constexpr std::uint32_t FirstGapMs = 120'000;
    static constexpr std::uint32_t MaxGapMs = 300'000;
    /// Between passes the radio stays this long on the repeater in use, to
    /// hear it if it is on the air.
    static constexpr std::uint32_t DwellMs = 500;

    /// Whether to listen at @p nowMs: good or weak, one check while the
    /// repeater in use sends idle bursts (@p idle); quiet or lost, a pass.
    /// Never while the repeater in use is good at the good level.
    bool due(const HeardList &heard, int site, const Levels &levels, bool idle, std::uint32_t nowMs);
    /// The zone position to check next, or -1 for none. In passes: the
    /// repeaters heard lately, strongest first, then the rest of the zone in
    /// its order. Each frequency once a pass, and never the frequency of
    /// @p site, the one in use. @p receiveHz holds each zone position's
    /// output frequency.
    int next(const HeardList &heard, std::span<const std::uint32_t> receiveHz, int site, std::uint32_t nowMs);
    /// The position next() gave last ended its pass.
    bool passDone() const { return m_passNext >= m_passCount; }
    /// A check or a pass ended at @p nowMs.
    void done(std::uint32_t nowMs)
    {
        m_lastMs = nowMs;
        m_any = true;
    }

private:
    std::uint8_t m_pass[MaxZone] = {};
    std::size_t m_passCount = 0;
    std::size_t m_passNext = 0;
    std::uint32_t m_lastMs = 0;
    bool m_any = false;
    bool m_scanning = false;
    std::uint32_t m_burstEndMs = 0;
    std::uint32_t m_gapMs = FirstGapMs;
};

} // namespace dmm::services::roaming
