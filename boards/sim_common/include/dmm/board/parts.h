// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// The simulators' parts that the app uses as concrete types: the radio's
// calibration format with sample data, no baseband and no data port.

#include "sim_battery.h"
#include "sim_fm.h"
#include "sim_gps.h"
#include "sim_input.h"
#include "sim_rtc.h"

#include "dmm/services/baseband.h"
#include "dmm/services/casic_receiver.h"
#include "dmm/services/d878uv2_calibration.h"
#include "dmm/services/d878uv2_programming.h"
#include "dmm/services/programming.h"
#include "dmm/services/usb_channel.h"

namespace dmm::board {

using Fm = SimFm;
using Baseband = services::NoBaseband;
using Calibration = services::D878Uv2Calibration;
using Programming = services::D878Uv2Programming;
using Input = SimInput;
using Battery = SimBattery;
// No data port: the platform's pointer stays empty.
using DataChannel = services::UsbChannel;
using GpsReceiver = services::CasicReceiver;
using Gps = SimGps;
using Rtc = SimRtc;

static_assert(services::FmTransceiver<Fm>);
static_assert(services::Calibration<Calibration>);
static_assert(services::Programming<Programming>);
static_assert(services::InputDevices<Input>);
static_assert(services::BatterySensor<Battery>);
static_assert(services::GpsPort<Gps>);
static_assert(services::GpsReceiver<GpsReceiver>);
static_assert(services::RealTimeClock<Rtc>);

} // namespace dmm::board
