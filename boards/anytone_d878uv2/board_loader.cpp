// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "board_loader.h"

#include "handover.h"
#include "image_info.h"
#include "mcu_flash.h"

#include "dmm/hal/cortex_m/core.h"
#include "dmm/os/os.h"
#include "dmm/services/boot_loader.h"
#include "dmm/services/boot_record.h"
#include "dmm/services/d878uv2_boot_layout.h"

DMM_IMAGE_INFO(dmm::services::boot::ImageKind::App);

namespace board {

namespace {

namespace boot = dmm::services::boot;
namespace layout = dmm::services::d878uv2;

DMM_HANDOVER_BLOCK(handover);

// The link and system tasks both read the record and may both confirm.
dmm::os::Mutex &recordLock()
{
    static dmm::os::Mutex lock;
    return lock;
}

// The app only appends to the record; it never erases.
McuFlash &recordFlash()
{
    static McuFlash flash{{layout::RecordBase, layout::RecordEnd}};
    return flash;
}

bool confirmLocked()
{
    boot::BootRecord record(recordFlash(), layout::BootLayout.record);
    if (!record.load()) {
        return false;
    }
    const boot::BootState &s = record.state();
    if (s.a.state == boot::SlotState::Confirmed) {
        return true;
    }
    return s.a.state == boot::SlotState::Trial && record.append(boot::entry::confirm(s.a.crc)) &&
        record.state().a.state == boot::SlotState::Confirmed;
}

} // namespace

dmm::services::LoaderStatus loaderStatus()
{
    boot::BootRecord record(recordFlash(), layout::BootLayout.record);
    dmm::services::LoaderStatus status;
    recordLock().lock();
    const bool loaded = record.load();
    recordLock().unlock();
    if (!loaded) {
        return status;
    }
    const boot::BootState &s = record.state();
    status.present = true;
    status.trial = s.a.state == boot::SlotState::Trial;
    status.manual = s.a.mode == boot::ConfirmMode::Manual;
    status.attempts = s.attempts;
    status.maxAttempts = boot::Loader::MaxAttempts;
    status.previousKept = s.b.state == boot::SlotState::Confirmed;
    status.fallback = s.lastFallback;
    return status;
}

bool confirmImage()
{
    recordLock().lock();
    const bool confirmed = confirmLocked();
    recordLock().unlock();
    return confirmed;
}

void enterLoader(dmm::services::LoaderRequest request)
{
    storeHandover(handover, boot::makeHandover(request == dmm::services::LoaderRequest::Rollback ? boot::HandoverRequest::Rollback : boot::HandoverRequest::Stay, 0));
    dmm::hal::cortex_m::systemReset();
}

} // namespace board
