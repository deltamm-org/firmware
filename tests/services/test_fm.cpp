// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/d878uv2_calibration.h"
#include "dmm/services/rda1846_transceiver.h"
#include "dmm/services/squelch_gate.h"

#include <doctest/doctest.h>

#include <array>
#include <map>
#include <string>
#include <vector>

using dmm::drivers::radio::Rda1846;
using dmm::services::D878Uv2Calibration;
using dmm::services::FmChannel;
using dmm::services::Rda1846Transceiver;
using dmm::services::SquelchGate;

namespace {

class FakeBus final : public dmm::hal::RegisterBus
{
public:
    std::uint16_t read(std::uint8_t address) override { return registers[address]; }
    void write(std::uint8_t address, std::uint16_t value) override
    {
        registers[address] = value;
        events.push_back("r" + std::to_string(address) + "=" + std::to_string(value));
    }

    std::map<std::uint8_t, std::uint16_t> registers;
    std::vector<std::string> events;
};

class FakeRf final : public dmm::services::RfHardware
{
public:
    void setReferenceTrim(std::uint16_t v) override { trim = v; }
    void setReceiveTune(std::uint16_t v) override { tune = v; }
    void setTransmitPath(bool on) override
    {
        path = on;
        note(on ? "path on" : "path off");
    }
    void setPowerAmplifier(bool on) override
    {
        amplifier = on;
        note(on ? "pa on" : "pa off");
    }
    void setPowerControl(std::uint16_t v) override
    {
        power = v;
        note("power " + std::to_string(v));
    }
    void note(const std::string &s)
    {
        if (events != nullptr) {
            events->push_back(s);
        }
    }

    std::vector<std::string> *events = nullptr;

    int trim = -1;
    int tune = -1;
    bool path = true;
    bool amplifier = true;
    int power = -1;
};

void noSleep(unsigned) {}

constexpr Rda1846Transceiver::Pins BoardPins{3, 0, 4, 5, 7, 2, 300'000'000, 165'000'000, 0xFFF};

D878Uv2Calibration calibration()
{
    std::array<std::uint8_t, D878Uv2Calibration::Bytes> d{};
    d[0x14] = 'w';
    d[0x15] = 'w';
    d[0x00] = 0x12; // trim 0x0812
    d[0x01] = 0x08;
    d[0x09] = 120;  // UHF squelch base
    d[0x0A] = 0xE8; // 1000 at 400 MHz
    d[0x0B] = 0x03;
    d[0x0C] = 0xD0; // 2000 at 440 MHz
    d[0x0D] = 0x07;
    d[0x0E] = 0xB8; // 3000 at 480 MHz
    d[0x0F] = 0x0B;
    d[0x29] = 110;  // VHF squelch base
    d[0x02] = 40;   // UHF Low power base
    d[0x05] = 120;  // UHF Turbo power base
    d[0x22] = 30;   // VHF Low power base
    d[0x06] = 0x2A; // deviation: voice, CTCSS, DCS
    d[0x07] = 0x12;
    d[0x08] = 0x0C;
    D878Uv2Calibration cal;
    cal.use(d);
    return cal;
}

std::uint16_t gpioField(std::uint16_t gpio, unsigned pin)
{
    return static_cast<std::uint16_t>((gpio >> (2 * pin)) & 3);
}

} // namespace

TEST_CASE("the squelch gate needs three polls either way")
{
    CHECK(Rda1846Transceiver::squelchOpenNoiseDb(true, 3) == 56);
    CHECK(Rda1846Transceiver::squelchOpenNoiseDb(false, 1) == 50);

    SquelchGate g;
    CHECK_FALSE(g.update(true));
    CHECK_FALSE(g.update(true));
    CHECK(g.update(true));
    CHECK(g.update(false)); // one dropout changes nothing
    CHECK(g.update(true));
    CHECK(g.update(false));
    CHECK(g.update(false));
    CHECK_FALSE(g.update(false));
    g.setForcedOpen(true);
    CHECK(g.update(false));
    g.setForcedOpen(false);
    CHECK_FALSE(g.open());
}

TEST_CASE("start leaves the microphone muted and every RF switch off")
{
    FakeBus bus;
    bus.registers[0x00] = 0x1846;
    Rda1846 chip(bus);
    FakeRf rf;
    Rda1846Transceiver fm(chip, rf, BoardPins, noSleep);
    REQUIRE(fm.start());
    const std::uint16_t gpio = bus.registers[0x1F];
    CHECK(gpio == 0x8AE2);
    CHECK(gpioField(gpio, 3) == 3); // mic muted
    for (unsigned pin : {0u, 2u, 4u, 5u, 7u}) {
        CHECK(gpioField(gpio, pin) == 2); // low
    }
    CHECK(rf.power == 0);
    CHECK_FALSE(rf.amplifier);
    CHECK_FALSE(rf.path);
    CHECK_FALSE(fm.receiving());
}

TEST_CASE("receive applies trim, tuning and squelch, then opens one front end")
{
    FakeBus bus;
    bus.registers[0x00] = 0x1846;
    Rda1846 chip(bus);
    FakeRf rf;
    Rda1846Transceiver fm(chip, rf, BoardPins, noSleep);
    REQUIRE(fm.start());
    const D878Uv2Calibration cal = calibration();

    CHECK(fm.receive(FmChannel{433'500'000, 0, false}, 1, cal) == nullptr);
    CHECK(fm.receiving());
    CHECK(rf.trim == 0x0812);
    CHECK(rf.tune == 1837); // 1000 + 1000 * 33.5 / 40
    CHECK(bus.registers[0x30] == 0x4026);
    CHECK(bus.registers[0x49] == Rda1846::squelchWord(-120, -123));
    CHECK(bus.registers[0x48] == ((50 << 7) | 47)); // narrow, level 1
    CHECK(gpioField(bus.registers[0x1F], 0) == 3);  // UHF front end on
    CHECK(gpioField(bus.registers[0x1F], 4) == 2);  // VHF front end off
    CHECK(gpioField(bus.registers[0x1F], 3) == 3);  // mic still muted
    CHECK(rf.power == 0);
    CHECK_FALSE(rf.amplifier);

    // Changing to VHF swaps the front ends.
    CHECK(fm.receive(FmChannel{145'500'000, 0, true}, 1, cal) == nullptr);
    CHECK(gpioField(bus.registers[0x1F], 0) == 2);
    CHECK(gpioField(bus.registers[0x1F], 4) == 3);
    CHECK(bus.registers[0x30] == 0x7026);
    CHECK(bus.registers[0x48] == ((60 << 7) | 57)); // wide, level 1

    const auto calibrated = rf.tune;
    CHECK(fm.debugCommand("tune 4000"));
    CHECK(rf.tune == 4000);
    CHECK_FALSE(fm.debugCommand("tune 4096")); // past the board's range
    CHECK(fm.debugCommand("tune auto"));
    CHECK(rf.tune == calibrated);
    CHECK(fm.debugCommand("tune 4000"));
    CHECK(fm.receive(FmChannel{433'500'000, 0, false}, 1, cal) == nullptr);
    CHECK(rf.tune == 1837); // a new receive puts the calibrated value back

    fm.idle();
    CHECK_FALSE(fm.receiving());
    CHECK(bus.registers[0x30] == 0x4006);
}

TEST_CASE("a sleeping receiver keeps its tuning and wakes on the same front end")
{
    FakeBus bus;
    bus.registers[0x00] = 0x1846;
    Rda1846 chip(bus);
    FakeRf rf;
    Rda1846Transceiver fm(chip, rf, BoardPins, noSleep);
    REQUIRE(fm.start());
    const D878Uv2Calibration cal = calibration();

    fm.sleepReceiver(); // not receiving: nothing to do
    CHECK_FALSE(fm.receiverAsleep());

    REQUIRE(fm.receive(FmChannel{145'500'000, 0, true}, 1, cal) == nullptr);
    const std::uint16_t frequencyHigh = bus.registers[0x29];
    const std::uint16_t frequencyLow = bus.registers[0x2A];
    fm.sleepReceiver();
    CHECK(fm.receiverAsleep());
    CHECK_FALSE(fm.receiving());
    CHECK(bus.registers[0x30] == 0x7006);
    CHECK(gpioField(bus.registers[0x1F], 4) == 2); // VHF front end off
    CHECK(gpioField(bus.registers[0x1F], 0) == 2);

    fm.wakeReceiver();
    CHECK_FALSE(fm.receiverAsleep());
    CHECK(fm.receiving());
    CHECK(bus.registers[0x30] == 0x7026);
    CHECK(gpioField(bus.registers[0x1F], 4) == 3);
    CHECK(bus.registers[0x29] == frequencyHigh);
    CHECK(bus.registers[0x2A] == frequencyLow);

    fm.sleepReceiver();
    fm.idle();
    CHECK_FALSE(fm.receiverAsleep());
    fm.wakeReceiver(); // idle wins
    CHECK_FALSE(fm.receiving());
}

TEST_CASE("receive refuses frequencies the calibration does not cover")
{
    FakeBus bus;
    bus.registers[0x00] = 0x1846;
    Rda1846 chip(bus);
    FakeRf rf;
    Rda1846Transceiver fm(chip, rf, BoardPins, noSleep);
    REQUIRE(fm.start());
    const D878Uv2Calibration cal = calibration();
    CHECK(fm.receive(FmChannel{250'000'000, 0, false}, 1, cal) != nullptr);
    CHECK_FALSE(fm.receiving());
    D878Uv2Calibration none;
    CHECK(std::string(fm.receive(FmChannel{433'500'000, 0, false}, 1, none)) == "no calibration");
}

TEST_CASE("status reads the carrier flag and RSSI")
{
    FakeBus bus;
    Rda1846 chip(bus);
    FakeRf rf;
    Rda1846Transceiver fm(chip, rf, BoardPins, noSleep);
    bus.registers[0x21] = 0x000F; // poor quality
    bus.registers[0x1C] = 0x0001;
    bus.registers[0x1B] = 0x3042;
    CHECK(fm.status().carrier);
    CHECK(fm.status().rssiDbm == -89);
    bus.registers[0x1C] = 0x0004;
    CHECK_FALSE(fm.status().carrier);
    // A clean strong signal counts without the flag: below 6 on narrow.
    bus.registers[0x21] = 0x0005;
    CHECK(fm.status().carrier);
    bus.registers[0x21] = 0x0006;
    CHECK_FALSE(fm.status().carrier);
}

namespace {

std::size_t indexOf(const std::vector<std::string> &v, const std::string &s)
{
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == s) {
            return i;
        }
    }
    return v.size();
}

} // namespace

namespace {

// The chip's own values, plus the transmit drive the board sets.
constexpr Rda1846::WidthSetting TransmitDrive[] = {{0x0A, 0x3BA0, 0x3BA0}};
const Rda1846::Tuning &transmitTuning()
{
    static const Rda1846::Tuning tuning = [] {
        Rda1846::Tuning t = Rda1846::makerTuning();
        t.transmit = TransmitDrive;
        return t;
    }();
    return tuning;
}

} // namespace

TEST_CASE("transmit: band switches and transceiver first, power amplifier last")
{
    using dmm::services::PowerLevel;
    FakeBus bus;
    bus.registers[0x00] = 0x1846;
    Rda1846 chip(bus);
    FakeRf rf;
    rf.events = &bus.events;
    Rda1846Transceiver fm(chip, rf, BoardPins, noSleep, transmitTuning);
    REQUIRE(fm.start());
    const D878Uv2Calibration cal = calibration();
    REQUIRE(fm.receive(FmChannel{433'500'000, 433'500'000, false}, 1, cal) == nullptr);
    bus.events.clear();

    REQUIRE(fm.transmit(FmChannel{433'500'000, 438'500'000, false}, PowerLevel::Low, 11'480, cal) == nullptr);
    CHECK(fm.transmitting());
    CHECK(fm.transmitPower() == 40 * 16);
    CHECK(rf.power == 40 * 16);
    CHECK(rf.amplifier);
    CHECK(rf.path);
    const std::uint16_t gpio = bus.registers[0x1F];
    CHECK(gpioField(gpio, 0) == 2); // receive front ends off
    CHECK(gpioField(gpio, 4) == 2);
    CHECK(gpioField(gpio, 5) == 3); // UHF transmit path
    CHECK(gpioField(gpio, 7) == 2);
    CHECK(gpioField(gpio, 3) == 2); // microphone live
    CHECK(bus.registers[0x29] == (Rda1846::frequencyWord(438'500'000) >> 16));
    CHECK(bus.registers[0x0A] == 0x3BA0);
    CHECK(bus.registers[0x59] == ((0x2A << 6) | 0x12));
    CHECK(bus.registers[0x4A] == 11'480);
    CHECK(bus.registers[0x4E] == 0x2602);
    // The transceiver transmits before the amplifier is enabled, and the
    // amplifier is enabled before any power is applied; the power rises in
    // three steps.
    const auto &e = bus.events;
    const std::size_t txOn = indexOf(e, "r48=" + std::to_string(0x4046));
    const std::size_t paOn = indexOf(e, "pa on");
    const std::size_t third = indexOf(e, "power " + std::to_string(40 * 16 / 3));
    const std::size_t full = indexOf(e, "power " + std::to_string(40 * 16));
    REQUIRE(txOn < e.size());
    CHECK(indexOf(e, "path on") < txOn);
    CHECK(txOn < paOn);
    CHECK(paOn < third);
    CHECK(third < full);
    REQUIRE(full < e.size());

    bus.events.clear();
    fm.endTransmit();
    CHECK_FALSE(fm.transmitting());
    CHECK(fm.transmitPower() == 0);
    CHECK(rf.power == 0);
    CHECK_FALSE(rf.amplifier);
    CHECK_FALSE(rf.path);
    CHECK(gpioField(bus.registers[0x1F], 3) == 3); // microphone muted
    CHECK(gpioField(bus.registers[0x1F], 5) == 2);
    CHECK(bus.registers[0x4E] == 0x2002);
    // The amplifier goes off before the transceiver stops transmitting.
    CHECK(indexOf(bus.events, "pa off") < indexOf(bus.events, "r48=" + std::to_string(0x4006)));
}

TEST_CASE("transmit picks the mid-VHF filter and refuses what the calibration does not cover")
{
    using dmm::services::PowerLevel;
    FakeBus bus;
    bus.registers[0x00] = 0x1846;
    Rda1846 chip(bus);
    FakeRf rf;
    Rda1846Transceiver fm(chip, rf, BoardPins, noSleep);
    REQUIRE(fm.start());
    const D878Uv2Calibration cal = calibration();

    REQUIRE(fm.transmit(FmChannel{170'000'000, 170'000'000, true}, PowerLevel::Low, 0, cal) == nullptr);
    CHECK(gpioField(bus.registers[0x1F], 7) == 3);
    CHECK(gpioField(bus.registers[0x1F], 5) == 2);
    CHECK(bus.registers[0x4E] == 0x2002);
    CHECK(bus.registers[0x59] == (0x2A << 6)); // no tone deviation without a tone
    fm.endTransmit();
    CHECK(gpioField(bus.registers[0x1F], 7) == 2);

    CHECK(fm.transmit(FmChannel{145'000'000, 250'000'000, false}, PowerLevel::Low, 0, cal) != nullptr);
    CHECK_FALSE(fm.transmitting());
    CHECK_FALSE(rf.amplifier);
    CHECK(rf.power == 0);
    D878Uv2Calibration none;
    CHECK(fm.transmit(FmChannel{433'500'000, 433'500'000, false}, PowerLevel::Low, 0, none) != nullptr);
    CHECK_FALSE(rf.amplifier);
}

TEST_CASE("the chip's console commands read and write registers")
{
    FakeBus bus;
    Rda1846 chip(bus);
    FakeRf rf;
    Rda1846Transceiver fm(chip, rf, BoardPins, noSleep);
    bus.registers[0x1F] = 0x1234;
    CHECK(fm.debugCommand("1f"));
    CHECK(fm.debugCommand("30 4006"));
    CHECK(bus.registers[0x30] == 0x4006);
    CHECK_FALSE(fm.debugCommand("80"));       // no such register
    CHECK_FALSE(fm.debugCommand("30 10000")); // too wide
    CHECK_FALSE(fm.debugCommand("30 1 2"));   // one value only
    CHECK_FALSE(fm.debugCommand("hello"));
    const auto before = rf.tune;
    CHECK(fm.debugCommand("sweep")); // refused while idle, but known
    CHECK(rf.tune == before);
}
