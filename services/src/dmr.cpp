// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/dmr.h"

namespace dmm::services::dmr {

namespace {

constexpr std::uint8_t ProtectBit = 0x80;
constexpr std::uint8_t FlcoMask = 0x3F;

void put24(std::uint8_t *out, std::uint32_t v)
{
    out[0] = static_cast<std::uint8_t>(v >> 16);
    out[1] = static_cast<std::uint8_t>(v >> 8);
    out[2] = static_cast<std::uint8_t>(v);
}

std::uint32_t get24(const std::uint8_t *in)
{
    return static_cast<std::uint32_t>(in[0]) << 16 | static_cast<std::uint32_t>(in[1]) << 8 | in[2];
}

} // namespace

std::array<std::uint8_t, 10> repeaterWake(std::uint32_t source)
{
    // Last block, standard feature set, then the target (all repeaters)
    // and the source.
    std::array<std::uint8_t, 10> b{};
    b[0] = static_cast<std::uint8_t>(0x80 | static_cast<std::uint8_t>(CsbkOpcode::RepeaterWake));
    put24(&b[4], AllCallId);
    put24(&b[7], source);
    return b;
}

FullLc FullLc::voiceCall(const Target &target, std::uint32_t source)
{
    FullLc lc;
    lc.flco = static_cast<std::uint8_t>(target.type == CallType::Private ? Flco::UnitToUnitVoice : Flco::GroupVoice);
    lc.destination = target.type == CallType::All ? AllCallId : target.id;
    lc.source = source;
    return lc;
}

void FullLc::encode(std::span<std::uint8_t, Bytes> out) const
{
    out[0] = static_cast<std::uint8_t>((protect ? ProtectBit : 0) | (flco & FlcoMask));
    out[1] = featureSet;
    out[2] = serviceOptions;
    put24(&out[3], destination);
    put24(&out[6], source);
}

std::optional<FullLc> FullLc::decode(std::span<const std::uint8_t> in)
{
    if (in.size() < Bytes) {
        return std::nullopt;
    }
    FullLc lc;
    lc.protect = (in[0] & ProtectBit) != 0;
    lc.flco = in[0] & FlcoMask;
    lc.featureSet = in[1];
    lc.serviceOptions = in[2];
    lc.destination = get24(&in[3]);
    lc.source = get24(&in[6]);
    return lc;
}

} // namespace dmm::services::dmr
