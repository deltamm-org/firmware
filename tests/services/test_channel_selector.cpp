// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/channel_selector.h"

#include <doctest/doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

using dmm::services::Channel;
using dmm::services::ChannelSelector;
using dmm::services::CodeplugSettings;
using dmm::services::CodeplugSource;

namespace {

class FakeCodeplug final : public CodeplugSource
{
public:
    std::optional<CodeplugSettings> settings() override { return s; }
    std::optional<Channel> channel(std::uint16_t index) override
    {
        const auto it = channels.find(index);
        if (it == channels.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    std::optional<Channel> vfo(bool sideB) override { return sideB ? vfoB : vfoA; }
    bool zoneInUse(std::uint8_t zone) override { return zones.count(zone) != 0; }
    std::size_t zonesInUse(std::span<std::uint8_t> out) override
    {
        std::size_t n = 0;
        for (const auto &z : zones) {
            out[n++] = z.first;
        }
        return n;
    }
    bool zoneName(std::uint8_t zone, std::span<char, ZoneNameBytes + 1> name) override
    {
        std::strncpy(name.data(), names[zone].c_str(), ZoneNameBytes);
        name[ZoneNameBytes] = '\0';
        return true;
    }
    std::size_t zoneChannels(std::uint8_t zone, std::span<std::uint16_t> out) override
    {
        const auto &list = zones[zone];
        for (std::size_t i = 0; i < list.size(); ++i) {
            out[i] = list[i];
        }
        return list.size();
    }
    std::uint16_t zoneSelection(std::uint8_t zone, bool sideB) override
    {
        const auto it = (sideB ? selectionB : selectionA).find(zone);
        return it == (sideB ? selectionB : selectionA).end() ? NoChannel : it->second;
    }

    void addChannel(std::uint16_t index, std::uint32_t hz, const char *name)
    {
        Channel c;
        c.receiveHz = c.transmitHz = hz;
        std::strncpy(c.name, name, Channel::NameBytes);
        channels[index] = c;
    }

    CodeplugSettings s;
    std::map<std::uint16_t, Channel> channels;
    std::map<std::uint8_t, std::vector<std::uint16_t>> zones;
    std::map<std::uint8_t, std::string> names;
    std::map<std::uint8_t, std::uint16_t> selectionA, selectionB;
    std::optional<Channel> vfoA, vfoB;
};

// Zone 0 "Local" holds channels 10, 11, 12; zone 3 "Far" holds 20.
FakeCodeplug sample()
{
    FakeCodeplug cp;
    cp.addChannel(10, 145'500'000, "S20");
    cp.addChannel(11, 145'600'000, "RPT");
    cp.addChannel(12, 433'500'000, "UHF");
    cp.addChannel(20, 438'000'000, "FAR");
    cp.zones[0] = {10, 11, 12};
    cp.zones[3] = {20};
    cp.names[0] = "Local";
    cp.names[3] = "Far";
    cp.selectionA[0] = 1;
    return cp;
}

std::string name(const ChannelSelector &sel)
{
    return sel.current() ? sel.current()->name : "";
}

} // namespace

TEST_CASE("starts on the main side's zone and its last channel")
{
    FakeCodeplug cp = sample();
    ChannelSelector sel(cp);
    REQUIRE(sel.load());
    CHECK_FALSE(sel.vfoMode());
    CHECK(std::string(sel.zoneName()) == "Local");
    CHECK(name(sel) == "RPT");
    CHECK(sel.position() == 2);
    CHECK(sel.zoneSize() == 3);
}

TEST_CASE("the last selection is a position in the zone's list")
{
    FakeCodeplug cp = sample();
    cp.zones[0] = {12, 0, 10, 11};
    cp.addChannel(0, 145'000'000, "ZERO");
    cp.selectionA[0] = 0;
    ChannelSelector sel(cp);
    REQUIRE(sel.load());
    CHECK(name(sel) == "UHF");

    cp.selectionA[0] = 9;
    ChannelSelector beyond(cp);
    REQUIRE(beyond.load());
    CHECK(name(beyond) == "UHF");
}

TEST_CASE("channels step and wrap within the zone, skipping unreadable ones")
{
    FakeCodeplug cp = sample();
    ChannelSelector sel(cp);
    REQUIRE(sel.load());
    REQUIRE(sel.step(1));
    CHECK(name(sel) == "UHF");
    REQUIRE(sel.step(1));
    CHECK(name(sel) == "S20"); // wrapped
    REQUIRE(sel.step(-1));
    CHECK(name(sel) == "UHF");
    REQUIRE(sel.step(-2));
    CHECK(name(sel) == "S20");
    cp.channels.erase(11); // now unreadable
    REQUIRE(sel.step(1));
    CHECK(name(sel) == "UHF");
}

TEST_CASE("zones step to the next one in use, both ways")
{
    FakeCodeplug cp = sample();
    ChannelSelector sel(cp);
    REQUIRE(sel.load());
    REQUIRE(sel.stepZone(1));
    CHECK(sel.zone() == 3);
    CHECK(name(sel) == "FAR");
    REQUIRE(sel.stepZone(1));
    CHECK(sel.zone() == 0); // wrapped round
    REQUIRE(sel.stepZone(-1));
    CHECK(sel.zone() == 3);
    CHECK_FALSE(sel.selectZone(1));
    CHECK(sel.zone() == 3);
    std::uint8_t used[8];
    REQUIRE(sel.zones(used) == 2);
    CHECK(used[1] == 3);
    char label[ChannelSelector::NameBytes];
    sel.zoneLabel(3, label);
    CHECK(std::string(label) == "Far");
}

TEST_CASE("an unused starting zone falls back to the next zone in use")
{
    FakeCodeplug cp = sample();
    cp.s.zoneA = 1;
    ChannelSelector sel(cp);
    REQUIRE(sel.load());
    CHECK(sel.zone() == 3);
}

TEST_CASE("the VFO keeps the VFO record's offset and returns to the memory channel")
{
    FakeCodeplug cp = sample();
    Channel v;
    v.receiveHz = 145'000'000;
    v.transmitHz = 144'400'000;
    v.wide = true;
    cp.vfoA = v;
    ChannelSelector sel(cp);
    REQUIRE(sel.load());
    sel.tune(145'450'000);
    CHECK(sel.vfoMode());
    REQUIRE(sel.current());
    CHECK(sel.current()->receiveHz == 145'450'000);
    CHECK(sel.current()->transmitHz == 144'850'000);
    CHECK(sel.current()->wide);
    REQUIRE(sel.memoryMode());
    CHECK(name(sel) == "RPT");
}

TEST_CASE("a codeplug set to the VFO starts there, and no zones means VFO only")
{
    FakeCodeplug cp = sample();
    cp.s.vfoA = true;
    Channel v;
    v.receiveHz = v.transmitHz = 433'000'000;
    cp.vfoA = v;
    ChannelSelector sel(cp);
    sel.load();
    CHECK(sel.vfoMode());
    CHECK(sel.current()->receiveHz == 433'000'000);

    FakeCodeplug empty;
    empty.vfoA = v;
    ChannelSelector none(empty);
    CHECK_FALSE(none.load());
    CHECK(none.vfoMode());
    CHECK_FALSE(none.step(1));
}

TEST_CASE("squelch, power and squelch mode can be changed for the session")
{
    FakeCodeplug cp = sample();
    ChannelSelector sel(cp);
    REQUIRE(sel.load());
    CHECK(sel.squelch() == 3);
    sel.setSquelch(1);
    CHECK(sel.squelch() == 1);
    sel.setSquelch(9);
    CHECK(sel.squelch() == 5);
    sel.setPower(dmm::services::PowerLevel::Turbo);
    CHECK(sel.current()->power == dmm::services::PowerLevel::Turbo);
    REQUIRE(sel.step(1)); // a new channel brings its own power back
    CHECK(sel.current()->power == dmm::services::PowerLevel::Low);
    sel.setSquelchMode(dmm::services::SquelchMode::Tone);
    CHECK(sel.current()->squelchMode == dmm::services::SquelchMode::Tone);
    REQUIRE(sel.step(-1));
    CHECK(sel.current()->squelchMode == dmm::services::SquelchMode::Carrier);
}
