// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// The simulators' parts that the app uses as concrete types: the radio's
// calibration format with sample data, no baseband and no data port.

#include "sim_aprs_link.h"
#include "sim_battery.h"
#include "sim_fm.h"
#include "sim_gps.h"
#include "sim_input.h"
#include "sim_modem.h"
#include "sim_rtc.h"

#include "dmm/services/baseband.h"
#include "dmm/services/casic_receiver.h"
#include "dmm/services/d878uv2_calibration.h"
#include "dmm/services/d878uv2_link_board.h"
#include "dmm/services/packet_modem.h"
#include "dmm/services/serial_link.h"
#include "dmm/services/usb_channel.h"

namespace dmm::board {

using Fm = SimFm;
using Baseband = services::NoBaseband;
using Calibration = services::D878Uv2Calibration;
using Input = SimInput;
using Battery = SimBattery;
// No data port: the platform's pointer stays empty.
using DataChannel = services::UsbChannel;
using GpsReceiver = services::CasicReceiver;
using Gps = SimGps;
using Rtc = SimRtc;
using Modem = SimModem;
// The board the simulated radio has, sending in its format.
using AprsLink = SimAprsLink;
using AprsLinkReader = services::D878Uv2LinkBoardReader;

static_assert(services::FmTransceiver<Fm>);
static_assert(services::Calibration<Calibration>);
static_assert(services::InputDevices<Input>);
static_assert(services::BatterySensor<Battery>);
static_assert(services::GpsPort<Gps>);
static_assert(services::GpsReceiver<GpsReceiver>);
static_assert(services::RealTimeClock<Rtc>);
static_assert(services::PacketModem<Modem>);
static_assert(services::SerialLink<AprsLink>);

} // namespace dmm::board
