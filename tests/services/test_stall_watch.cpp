// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/stall_watch.h"

#include <doctest/doctest.h>

using dmm::services::StallWatch;

namespace {

// Ticks @p ms milliseconds; true if any of them reported a stall.
bool ticks(StallWatch &w, unsigned ms)
{
    bool stalled = false;
    for (unsigned i = 0; i < ms; ++i) {
        stalled = w.tick() || stalled;
    }
    return stalled;
}

} // namespace

TEST_CASE("stall watch: nothing before the first feed")
{
    StallWatch w{1500};
    CHECK_FALSE(ticks(w, 10'000));
}

TEST_CASE("stall watch: regular feeds are no stall; feeds that stop are")
{
    StallWatch w{1500};
    w.fed();
    for (int i = 0; i < 1000; ++i) {
        REQUIRE_FALSE(ticks(w, 10));
        w.fed();
    }
    // A feed 1499 ms late is still in time.
    CHECK_FALSE(ticks(w, 1499));
    w.fed();
    CHECK_FALSE(ticks(w, 1499));
    CHECK(w.tick());
    CHECK(w.sinceFeedMs() == 1500);
    CHECK(w.tick());
    w.fed();
    CHECK(w.sinceFeedMs() == 0);
    CHECK_FALSE(w.tick());
}
