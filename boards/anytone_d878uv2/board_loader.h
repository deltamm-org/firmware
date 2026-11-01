// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

// The app's side of DeltaMM's loader (DMM_LOADER builds): the status from
// the boot record, the confirmation and the restart into the loader.

#include "dmm/services/system_control.h"

namespace board {

dmm::services::LoaderStatus loaderStatus();
bool confirmImage();
/// Leaves the request in the hand-over block and restarts.
[[noreturn]] void enterLoader(dmm::services::LoaderRequest request);

} // namespace board
