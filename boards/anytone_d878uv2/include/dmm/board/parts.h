// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// The D878UVII's parts that the app uses as concrete types.

#include "dmm/board/board_battery.h"
#include "dmm/board/board_gps.h"
#include "dmm/board/board_input.h"
#include "dmm/board/board_rtc.h"
#include "dmm/services/casic_receiver.h"
#include "dmm/services/d878uv2_calibration.h"
#include "dmm/services/d878uv2_programming.h"
#include "dmm/services/programming.h"
#include "dmm/services/rda1846_transceiver.h"
#include "dmm/services/sct3258_baseband.h"
#include "dmm/services/usb_channel.h"

namespace dmm::board {

using Fm = services::Rda1846Transceiver;
using Baseband = services::Sct3258Baseband;
using Calibration = services::D878Uv2Calibration;
using Programming = services::D878Uv2Programming;
using Input = ::board::BoardInput;
using Battery = ::board::BoardBattery;
using DataChannel = services::UsbChannel;
using GpsReceiver = services::CasicReceiver;
using Gps = ::board::BoardGps;
using Rtc = ::board::BoardRtc;

static_assert(services::FmTransceiver<Fm>);
static_assert(services::Baseband<Baseband>);
static_assert(services::Calibration<Calibration>);
static_assert(services::Programming<Programming>);
static_assert(services::InputDevices<Input>);
static_assert(services::BatterySensor<Battery>);
static_assert(services::ByteChannel<DataChannel>);
static_assert(services::GpsPort<Gps>);
static_assert(services::GpsReceiver<GpsReceiver>);
static_assert(services::RealTimeClock<Rtc>);

} // namespace dmm::board
