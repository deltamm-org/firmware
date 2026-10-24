// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/input.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace dmm;
using namespace dmm::services;
using Type = KeyDebouncer::EventType;

namespace {

struct Recorder
{
    KeyDebouncer debouncer;
    std::vector<std::string> events;
    os::Ticks now = 0;

    // Feeds @p raw for @p ms in 10 ms samples.
    void feed(KeyMask raw, os::Ticks ms)
    {
        for (os::Ticks t = 0; t < ms; t += 10) {
            debouncer.update(raw, now, [&](const KeyDebouncer::Event &e) {
                static constexpr const char *Types[] = {"press", "release", "long", "repeat"};
                events.push_back(std::string(keyName(e.key)) + " " + Types[static_cast<int>(e.type)] + "@" + std::to_string(now));
            });
            now += 10;
        }
    }
};

} // namespace

TEST_CASE("a press needs two equal samples; a one-sample blip is ignored")
{
    Recorder r;
    r.feed(keyBit(Key::Digit5), 10);
    r.feed(0, 30);
    CHECK(r.events.empty());
    r.feed(keyBit(Key::Digit5), 20);
    r.feed(0, 20);
    CHECK(r.events == std::vector<std::string>{"5 press@50", "5 release@70"});
    CHECK(r.debouncer.pressed() == 0);
}

TEST_CASE("long press after a second, once")
{
    Recorder r;
    r.feed(keyBit(Key::Menu), 1500);
    r.feed(0, 20);
    CHECK(r.events == std::vector<std::string>{"Menu press@10", "Menu long@1010", "Menu release@1510"});
}

TEST_CASE("Up repeats while held; other keys do not")
{
    Recorder r;
    r.feed(keyBit(Key::Up) | keyBit(Key::Digit1), 800);
    std::vector<std::string> ups;
    for (const auto &e : r.events) {
        if (e.starts_with("Up")) {
            ups.push_back(e);
        }
    }
    CHECK(ups == std::vector<std::string>{"Up press@10", "Up repeat@510", "Up repeat@660"});
    CHECK(r.events.size() == 4); // plus "1 press"
}

TEST_CASE("keys pressed together are reported together, PTT included")
{
    Recorder r;
    r.feed(keyBit(Key::Ptt) | keyBit(Key::Side1), 20);
    CHECK(r.events == std::vector<std::string>{"Side1 press@10", "PTT press@10"});
    CHECK(keyName(Key::Star) == "*");
    CHECK(keyName(Key::Count) == "?");
}
