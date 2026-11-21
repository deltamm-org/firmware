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

/// Signal levels that steer roaming, in dB above each channel's floor. A
/// repeater still decodes close to its floor, where the level can no
/// longer be told from no signal.
struct Levels
{
    int searchDb = 10; // a search stops here; a quiet repeater is left for one at this level
    int fairDb = 6;    // below: look for a better repeater
    int marginDb = 6;  // better by this to leave a good or fair repeater
    int strongDb = 20; // at or above: no listening, no move
    // Strong ends, and so does below fair, only this far back past the
    // level, so that a level wavering about it does not flip the state.
    int backDb = 3;
};

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
    /// A reading of the repeater in use is the mean of its bursts over
    /// ReadingSpanMs from the start of a transmission, then the next span
    /// while it goes on: a beacon gives one, a call one every ReadingSpanMs.
    /// Its bursts stopping for ReadingGapMs end it early, unless the radio
    /// was away for a check during it. Of a current level, a reading of fewer
    /// than ReadingBursts is dropped.
    static constexpr std::uint32_t ReadingSpanMs = 5'000;
    static constexpr std::uint32_t ReadingGapMs = 300;
    static constexpr int ReadingBursts = 8;
    /// The level is the mean of the last Readings readings, none older than
    /// ValidMs: it follows a call within seconds, and beacons from one to
    /// the next, while one faded reading moves it only part of the way.
    static constexpr std::size_t Readings = 3;

    struct Entry
    {
        std::uint8_t position = 0;
        // In 1/16 dB: the mean of a few readings, which in whole dB would
        // round to the stronger or weaker side.
        std::int16_t level16Dbm = 0;
        std::int8_t floorDbm = 0;
        std::uint8_t samples = 0;  // levels measured while current, up to 255; 0: none
        std::uint8_t readings = 0; // of reading16Dbm, newest first, that count
        bool unanswered = false;   // a wake-up went unanswered since it was last heard
        bool strong = false;       // at the strong level, or not yet back below it by backDb since
        bool belowFair = false;    // below the fair level, or not yet back above it by backDb since
        std::int16_t reading16Dbm[Readings] = {};
        std::uint32_t readingMs[Readings] = {};
        std::uint32_t measuredMs = 0; // when it was last heard
        // When it was last not below the fair level, or when its current
        // levels began if it has not been since.
        std::uint32_t aboveFairMs = 0;

        // Rounded to the nearest dB, so that a steady level reads as itself
        // from either side.
        int levelDbm() const { return (level16Dbm + 8) >> 4; }
        int levelDb() const { return levelDbm() - floorDbm; }
        bool current(std::uint32_t nowMs) const { return samples > 0 && nowMs - measuredMs < ValidMs; }
    };

    void clear()
    {
        m_count = 0;
        m_pending = {};
    }
    /// Repeater @p position (-1: none) is in use: a full list never drops
    /// it, so that how it was last heard stays known while others come and
    /// go. An entry with no level, which only marks the one in use as not
    /// answering, goes once another is in use.
    void keep(int position);
    /// Repeater @p position read at @p levelDbm on a channel whose floor is
    /// @p floorDbm, judged against @p levels.
    void heard(std::uint8_t position, int levelDbm, int floorDbm, std::uint32_t nowMs, const Levels &levels = {});
    /// A transmission of repeater @p position starts, its first burst at
    /// @p levelDbm: a reading begins, after an open one is taken.
    void started(std::uint8_t position, int levelDbm, int floorDbm, std::uint32_t nowMs, const Levels &levels = {});
    /// A burst of repeater @p position at @p levelDbm, still on the air: a
    /// current level stays current, and the burst counts towards an open
    /// reading or begins the next.
    void stillHeard(std::uint8_t position, int levelDbm, int floorDbm, std::uint32_t nowMs, const Levels &levels = {});
    /// The radio leaves the repeater in use for a moment (a check): an open
    /// reading only pauses, goes on with the transmission once back, and
    /// ends with its span. So a beacon stays one reading, however many
    /// checks come during it.
    void pause();
    /// Takes an open reading whose span has passed or whose bursts stopped.
    void flush(std::uint32_t nowMs);
    /// Repeater @p position did not answer a wake-up: until it is heard
    /// again.
    void unanswered(std::uint8_t position, std::uint32_t nowMs);
    /// Another zone: what was at position k is now at @p moved[k], or gone
    /// when that is -1 or k is past its end.
    void renumber(std::span<const std::int16_t> moved);

    const Entry *find(std::uint8_t position) const;
    std::span<const Entry> entries() const { return {m_entries, m_count}; }

private:
    struct Pending
    {
        int position = -1; // -1: no reading open
        int floorDbm = 0;
        Levels levels;
        std::int32_t sumDbm = 0;
        int bursts = 0;
        std::uint32_t fromMs = 0;
        std::uint32_t lastMs = 0;
        bool paused = false; // the radio was away during it: it ends with its span
    };

    Entry &place(std::uint8_t position, std::uint32_t nowMs);
    void read(std::uint8_t position, std::int32_t level16Dbm, int floorDbm, std::uint32_t nowMs, const Levels &levels);
    void take();

    Entry m_entries[Capacity];
    std::size_t m_count = 0;
    int m_keep = -1;
    Pending m_pending;
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

/// How the repeater in use is doing.
enum class Health : std::uint8_t
{
    Strong, // at the strong level or above
    Good,   // below the strong level, at the fair level or above
    Fair,   // below the fair level, or heard lately but a wake-up went unanswered
    Poor,   // heard, but only below the fair level for as long as a level stays current
    Quiet,  // not heard lately, last heard at the fair level or above, or none in use yet
    Lost,   // not heard lately, and a wake-up went unanswered or it was last heard below the fair level
};

/// The health of repeater @p site (-1: none in use), by @p levels, the
/// levels the heard list was given.
Health health(const HeardList &heard, int site, const Levels &levels, std::uint32_t nowMs);

/// The repeater to move to from @p site, or -1 to stay. Strong: none.
/// Good or fair: the strongest current one, measured twice, better by the
/// margin. Poor: the strongest current one, better by the margin. Quiet: the
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
    /// A call on it goes on, or has just ended, also without a burst of it
    /// lately: an over can run for many seconds between such bursts, and a
    /// call can end without a terminator.
    void callHeard(std::uint32_t nowMs)
    {
        m_call = true;
        m_callMs = nowMs;
    }
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
    /// Good or fair: one check at most this often, while the repeater in
    /// use sends idle bursts.
    static constexpr std::uint32_t HopMs = 400;
    /// Good or fair: also a burst of passes this long after the last burst,
    /// also one while poor, quiet or lost (or after it was first below
    /// strong), as repeaters that beacon at other times than the one in use
    /// are not heard during its idle bursts.
    static constexpr std::uint32_t ScanGoodMs = 900'000;
    static constexpr std::uint32_t ScanFairMs = 600'000;
    /// Poor, quiet or lost: passes one after another for BurstMs, long
    /// enough to span a repeater's beacon period; then a gap. Poor or quiet:
    /// doubling from FirstGapMs up to MaxGapMs, kept short so that a
    /// repeater met after a long stretch without any is found soon. Lost:
    /// always FirstGapMs, as no call on the repeater in use can be missed
    /// then, until nothing at all was heard for SilentMs.
    static constexpr std::uint32_t BurstMs = 60'000;
    static constexpr std::uint32_t FirstGapMs = 120'000;
    static constexpr std::uint32_t MaxGapMs = 300'000;
    /// Lost with nothing at all heard for this long: the gaps grow, as while
    /// quiet.
    static constexpr std::uint32_t SilentMs = 3'600'000;
    /// Between passes the radio stays this long on the repeater in use, to
    /// hear it if it is on the air.
    static constexpr std::uint32_t DwellMs = 500;

    /// Whether to listen at @p nowMs: good or fair, one check while the
    /// repeater in use sends idle bursts (@p idle), and a burst now and then,
    /// not started while the repeater in use is @p busy; quiet or lost, a
    /// pass. Never while the repeater in use is strong.
    bool due(const HeardList &heard, int site, const Levels &levels, bool idle, std::uint32_t nowMs,
        bool busy = false);
    /// The listening due() allowed is a pass, not a single check.
    bool passing() const { return m_scanning || m_scanBurst; }
    /// For display, as of the last due(): whether bursts of passes are
    /// planned, and whether one runs at @p nowMs or when the next one starts
    /// (@p nextMs, set for Later).
    enum class Bursts : std::uint8_t
    {
        None,
        Now,
        Later,
    };
    Bursts bursts(std::uint32_t nowMs, std::uint32_t &nextMs) const;
    /// The zone position to check next, or -1 for none. In passes: the
    /// repeaters heard lately, strongest first, then the rest of the zone in
    /// its order. Each frequency once a pass, and never the frequency of
    /// @p site, the one in use. @p receiveHz holds each zone position's
    /// output frequency.
    int next(const HeardList &heard, std::span<const std::uint32_t> receiveHz, int site, std::uint32_t nowMs);
    /// Another zone: the next pass starts afresh.
    void restartPass()
    {
        m_passCount = 0;
        m_passNext = 0;
    }
    /// The position next() gave last ended its pass.
    bool passDone() const { return m_passNext >= m_passCount; }
    /// A check or a pass ended at @p nowMs.
    void done(std::uint32_t nowMs)
    {
        m_lastMs = nowMs;
        m_any = true;
    }

private:
    // A burst while poor, quiet or lost also counts as the last one for
    // the good and fair timer.
    void burstStarted(std::uint32_t nowMs)
    {
        m_scanTimer = true;
        m_scanLastMs = nowMs;
    }

    std::uint8_t m_pass[MaxZone] = {};
    std::size_t m_passCount = 0;
    std::size_t m_passNext = 0;
    std::uint32_t m_lastMs = 0;
    bool m_any = false;
    bool m_scanning = false;
    std::uint32_t m_burstEndMs = 0;
    std::uint32_t m_gapMs = FirstGapMs;
    bool m_scanTimer = false;
    bool m_scanBurst = false;
    std::uint32_t m_scanLastMs = 0;
    std::uint32_t m_scanEndMs = 0;
    int m_scanSite = -1;
    std::uint32_t m_passesFromMs = 0;
    bool m_planned = false;
    bool m_fixedGap = false;
    std::uint32_t m_scanPeriodMs = ScanGoodMs;
};

} // namespace dmm::services::roaming
