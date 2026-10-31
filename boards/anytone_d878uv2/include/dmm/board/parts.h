// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

// The D878UVII's parts that the app uses as concrete types.

#include "dmm/board/board_aprs_link.h"
#include "dmm/board/board_battery.h"
#include "dmm/board/board_gps.h"
#include "dmm/board/board_input.h"
#include "dmm/board/board_modem.h"
#include "dmm/board/board_rtc.h"
#include "dmm/services/casic_receiver.h"
#include "dmm/services/d878uv2_calibration.h"
#include "dmm/services/d878uv2_link_board.h"
#include "dmm/services/packet_modem.h"
#include "dmm/services/rda1846_transceiver.h"
#include "dmm/services/sct3258_baseband.h"
#include "dmm/services/serial_link.h"
#include "dmm/services/usb_channel.h"

namespace dmm::board {

using Fm = services::Rda1846Transceiver;
using Baseband = services::Sct3258Baseband;
using Calibration = services::D878Uv2Calibration;
using Input = ::board::BoardInput;
using Battery = ::board::BoardBattery;
using DataChannel = services::UsbChannel;
using GpsReceiver = services::CasicReceiver;
using Gps = ::board::BoardGps;
using Rtc = ::board::BoardRtc;
using Modem = ::board::BoardModem;
using AprsLink = ::board::BoardAprsLink;
using AprsLinkReader = services::D878Uv2LinkBoardReader;

static_assert(services::FmTransceiver<Fm>);
static_assert(services::Baseband<Baseband>);
static_assert(services::Calibration<Calibration>);
static_assert(services::InputDevices<Input>);
static_assert(services::BatterySensor<Battery>);
static_assert(services::ByteChannel<DataChannel>);
static_assert(services::GpsPort<Gps>);
static_assert(services::GpsReceiver<GpsReceiver>);
static_assert(services::RealTimeClock<Rtc>);
static_assert(services::PacketModem<Modem>);
static_assert(services::SerialLink<AprsLink>);

} // namespace dmm::board
