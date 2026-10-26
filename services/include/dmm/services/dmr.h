// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// DMR protocol terms from ETSI TS 102 361-1 and -2, the same for every
// radio: call types, the full link control word and burst data types.

#include <cstdint>
#include <optional>
#include <span>

namespace dmm::services::dmr {

/// The largest DMR ID, also the all-call address.
inline constexpr std::uint32_t AllCallId = 0xFFFFFF;

enum class CallType : std::uint8_t
{
    Private,
    Group,
    All,
};

/// Who a transmission goes to; id 0 = nobody set.
struct Target
{
    CallType type = CallType::Group;
    std::uint32_t id = 0;

    bool operator==(const Target &) const = default;
};

/// A channel's DMR settings.
struct ChannelSettings
{
    static constexpr std::uint8_t NoGroupList = 0xFF;

    std::uint8_t colourCode = 1; // 0..15
    std::uint8_t slot = 1;       // 1 or 2
    /// The channel's transmit contact.
    Target contact;
    /// The radio's own ID on this channel; 0 = none.
    std::uint32_t ownId = 0;
    /// The receive group list in the codeplug, or NoGroupList.
    std::uint8_t groupList = NoGroupList;
};

/// What a data burst carries (the slot type's data type).
enum class DataType : std::uint8_t
{
    PiHeader = 0,
    VoiceLcHeader = 1,
    TerminatorWithLc = 2,
    Csbk = 3,
    MbcHeader = 4,
    MbcContinuation = 5,
    DataHeader = 6,
    Rate12Data = 7,
    Rate34Data = 8,
    Idle = 9,
    Rate1Data = 10,
};

/// Full link control opcodes for voice calls.
enum class Flco : std::uint8_t
{
    GroupVoice = 0x00,
    UnitToUnitVoice = 0x03,
};

/// The full link control word without its CRC: 9 bytes on the air.
struct FullLc
{
    static constexpr std::size_t Bytes = 9;

    bool protect = false;
    std::uint8_t flco = 0;
    std::uint8_t featureSet = 0; // FID; 0 = standard
    std::uint8_t serviceOptions = 0;
    std::uint32_t destination = 0;
    std::uint32_t source = 0;

    /// A voice call from @p source to @p target.
    static FullLc voiceCall(const Target &target, std::uint32_t source);

    bool groupCall() const { return flco == static_cast<std::uint8_t>(Flco::GroupVoice); }
    bool privateCall() const { return flco == static_cast<std::uint8_t>(Flco::UnitToUnitVoice); }
    bool emergency() const { return (serviceOptions & 0x80) != 0; }

    void encode(std::span<std::uint8_t, Bytes> out) const;
    static std::optional<FullLc> decode(std::span<const std::uint8_t> in);
};

} // namespace dmm::services::dmr
