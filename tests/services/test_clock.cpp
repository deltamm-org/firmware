// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/clock.h"

#include <doctest/doctest.h>

using namespace dmm::services;

TEST_CASE("clock converts known dates both ways")
{
    struct Case
    {
        std::uint32_t seconds;
        DateTime t;
    };
    const Case cases[] = {
        {0, {1970, 1, 1, 0, 0, 0}},
        {951'782'400, {2000, 2, 29, 0, 0, 0}},
        {1'704'067'200, {2024, 1, 1, 0, 0, 0}},
        {1'709'208'000, {2024, 2, 29, 12, 0, 0}},
        {1'793'043'255, {2026, 10, 26, 19, 34, 15}},
        {2'147'483'647, {2038, 1, 19, 3, 14, 7}},
        {4'102'444'800, {2100, 1, 1, 0, 0, 0}},
    };
    for (const Case &c : cases) {
        CHECK(dateTime(c.seconds) == c.t);
        CHECK(secondsSince1970(c.t) == c.seconds);
    }
}

TEST_CASE("clock round trips every day for three centuries")
{
    for (std::uint32_t day = 0; day < 49'000; ++day) {
        const std::uint32_t s = day * 86'400 + 45'296; // 12:34:56
        const DateTime t = dateTime(s);
        REQUIRE(t.hour == 12);
        REQUIRE(t.minute == 34);
        REQUIRE(t.second == 56);
        REQUIRE(secondsSince1970(t) == s);
    }
}

TEST_CASE("clock rate fit finds a clock running fast")
{
    // 31.7 ppm fast with a 400 ms delay and up to 5 ms of jitter, one
    // sample a second for half an hour.
    ClockRateFit fit;
    const std::int64_t start = 1'793'043'255'000;
    for (std::int64_t s = 0; s < 1800; ++s) {
        const std::int32_t jitter = static_cast<std::int32_t>((s * 7919) % 11) - 5;
        const auto error = static_cast<std::int32_t>(-1300 + 400 + s * 317 / 10'000) + jitter;
        fit.add(start + s * 1000, error);
    }
    CHECK(fit.count() == 1800);
    CHECK(fit.spanMs() == 1'799'000);
    CHECK(fit.ratePpb() > 31'000);
    CHECK(fit.ratePpb() < 32'400);
    CHECK(fit.scatterMs() <= 5);
}

TEST_CASE("clock rate fit finds a slow clock and needs two samples")
{
    ClockRateFit fit;
    CHECK(fit.ratePpb() == 0);
    fit.add(0, 0);
    CHECK(fit.ratePpb() == 0);
    // 10 ms behind after 1000 s: 10 ppm slow.
    fit.add(1'000'000, -10);
    CHECK(fit.ratePpb() == -10'000);
    CHECK(fit.scatterMs() == 0);
    fit.clear();
    CHECK(fit.count() == 0);
}
