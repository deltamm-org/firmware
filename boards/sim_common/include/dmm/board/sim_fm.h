// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/services/fm_transceiver.h"
#include "dmm/services/log.h"

/// The FM transceiver as log lines: it tunes and transmits on request but
/// never hears a carrier.
class SimFm
{
public:
    bool present() { return true; }
    bool start()
    {
        m_mode = Mode::Idle;
        return true;
    }
    const char *receive(const dmm::services::FmChannel &channel, int squelchLevel)
    {
        dmm::services::log::info("sim") << "receive " << channel.receiveHz << " Hz, squelch " << squelchLevel;
        m_mode = Mode::Receive;
        return nullptr;
    }
    const char *transmit(const dmm::services::FmChannel &channel, dmm::services::PowerLevel)
    {
        dmm::services::log::info("sim") << "transmit " << channel.transmitHz << " Hz";
        m_mode = Mode::Transmit;
        return nullptr;
    }
    void endTransmit() { m_mode = Mode::Idle; }
    void idle() { m_mode = Mode::Idle; }
    void sleepReceiver() { m_asleep = m_mode == Mode::Receive; }
    void wakeReceiver() { m_asleep = false; }
    bool receiverAsleep() const { return m_asleep; }
    bool receiving() const { return m_mode == Mode::Receive; }
    bool transmitting() const { return m_mode == Mode::Transmit; }
    dmm::services::FmReceiveStatus status() { return {}; }
    void logStatus(std::string_view what) { dmm::services::log::info("sim") << what; }
    void setMicrophoneMuted(bool) {}
    bool debugCommand(std::string_view) { return false; }
    const char *debugHelp() const { return "none"; }

private:
    enum class Mode : std::uint8_t
    {
        Idle,
        Receive,
        Transmit,
    };
    Mode m_mode = Mode::Idle;
    bool m_asleep = false;
};
