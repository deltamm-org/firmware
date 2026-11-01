// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "standby.h"

#include <doctest/doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

using dmm::app::ui::bottomText;
using dmm::app::ui::StandbyState;
using dmm::services::Channel;
using dmm::services::ChannelMode;
using dmm::services::ChannelSelector;
using dmm::services::CodeplugSettings;
using dmm::services::CodeplugSource;
using dmm::services::HomeLayout;

namespace {

// Zone 0 "Local": an FM simplex channel, an FM repeater channel and a DMR
// channel, in that order.
class FakeCodeplug final : public CodeplugSource
{
public:
    FakeCodeplug()
    {
        add(10, "S20", 145'500'000, 145'500'000, ChannelMode::Analog);
        add(11, "RPT", 145'600'000, 145'000'000, ChannelMode::Analog);
        add(12, "DMR", 434'500'000, 432'900'000, ChannelMode::Digital);
    }

    std::optional<CodeplugSettings> settings() override { return CodeplugSettings{}; }
    std::optional<dmm::services::RadioPreferences> preferences() override { return std::nullopt; }
    std::optional<Channel> channel(std::uint16_t index) override
    {
        const auto it = m_channels.find(index);
        return it == m_channels.end() ? std::nullopt : std::optional<Channel>(it->second);
    }
    std::optional<Channel> vfo(bool) override { return std::nullopt; }
    bool zoneInUse(std::uint8_t zone) override { return zone == 0; }
    std::size_t zonesInUse(std::span<std::uint8_t> out) override
    {
        out[0] = 0;
        return 1;
    }
    bool zoneName(std::uint8_t, std::span<char, ZoneNameBytes + 1> name) override
    {
        std::strncpy(name.data(), "Local", ZoneNameBytes);
        name[ZoneNameBytes] = '\0';
        return true;
    }
    std::size_t zoneChannels(std::uint8_t, std::span<std::uint16_t> out) override
    {
        for (std::size_t i = 0; i < m_zone.size(); ++i) {
            out[i] = m_zone[i];
        }
        return m_zone.size();
    }
    bool contactName(std::uint32_t, bool, std::span<char, ContactNameBytes + 1>) override { return false; }
    std::size_t receiveGroups(std::uint8_t, std::span<std::uint32_t>) override { return 0; }
    bool dmrUser(std::uint32_t, DmrUser &) override { return false; }
    std::size_t roamingSites(std::uint8_t, std::span<dmm::services::RoamingSite>) override { return 0; }
    bool roamingSiteName(std::uint8_t, std::size_t, std::span<char, SiteNameBytes + 1>) override { return false; }
    std::size_t roamingZones(std::span<std::uint8_t>) override { return 0; }
    bool roamingZoneName(std::uint8_t, std::span<char, ZoneNameBytes + 1>) override { return false; }
    std::uint16_t zoneSelection(std::uint8_t, bool) override { return 0; }

private:
    void add(std::uint16_t index, const char *name, std::uint32_t rxHz, std::uint32_t txHz, ChannelMode mode)
    {
        Channel c;
        c.receiveHz = rxHz;
        c.transmitHz = txHz;
        c.mode = mode;
        std::strncpy(c.name, name, Channel::NameBytes);
        m_channels[index] = c;
        m_zone.push_back(index);
    }

    std::map<std::uint16_t, Channel> m_channels;
    std::vector<std::uint16_t> m_zone;
};

std::string bottom(const ChannelSelector &sel, const StandbyState &s)
{
    return std::string(bottomText(sel, s, 40).view());
}

} // namespace

TEST_CASE("the bottom row's details: an FM simplex channel shows the zone and its place, as DMR does")
{
    FakeCodeplug cp;
    ChannelSelector sel(cp);
    REQUIRE(sel.load());
    StandbyState s;
    REQUIRE(sel.current().has_value());
    REQUIRE(std::string(sel.current()->name) == "S20");
    CHECK(bottom(sel, s) == "Local  1/3");

    // With the zone large, the channel's name instead.
    s.layout.big = HomeLayout::Big::Zone;
    CHECK(bottom(sel, s) == "S20  1/3");
    s.layout.big = HomeLayout::Big::Channel;

    // A repeater channel shows its offset, a DMR channel the zone.
    REQUIRE(sel.step(1));
    CHECK(bottom(sel, s) == "-0.600");
    REQUIRE(sel.step(1));
    CHECK(bottom(sel, s) == "Local  3/3");

    // Other bottom rows are not details.
    REQUIRE(sel.step(1));
    s.layout.bottom = HomeLayout::Bottom::None;
    CHECK(bottom(sel, s).empty());
}
