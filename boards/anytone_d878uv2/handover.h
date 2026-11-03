// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// The hand-over block between the app and the loader: 16 bytes at the
// same RAM address in both (the linker scripts' HANDOVER region), kept
// over a restart. Written and read as volatile, since nothing in the
// writing image reads it back.

#include "dmm/services/boot_image.h"

namespace board {

inline void storeHandover(volatile dmm::services::boot::Handover &to, const dmm::services::boot::Handover &value)
{
    to.magic = value.magic;
    to.request = value.request;
    to.argument = value.argument;
    to.check = value.check;
}

inline dmm::services::boot::Handover loadHandover(const volatile dmm::services::boot::Handover &from)
{
    return {from.magic, from.request, from.argument, from.check};
}

} // namespace board

/// Defines the block in this image.
#define DMM_HANDOVER_BLOCK(name) [[gnu::section(".handover"), gnu::used]] volatile dmm::services::boot::Handover name
