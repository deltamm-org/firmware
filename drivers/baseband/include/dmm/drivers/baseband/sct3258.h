// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#pragma once

#include "dmm/hal/host_port_bus.h"
#include "dmm/lib/fn_ref.h"
#include "dmm/lib/ring_buffer.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace dmm::drivers::baseband {

/// One packet of the SCT3258 packet interface (Sicomm "Packet Interface of
/// CT3258"): start bytes 84 A9 61, a 16-bit length (MSB first), a type
/// byte and the fields.
struct Packet
{
    static constexpr std::size_t MaxFields = 256;

    std::uint8_t type = 0;
    std::uint16_t length = 0;
    std::uint8_t fields[MaxFields]{};

    std::span<const std::uint8_t> data() const { return {fields, length}; }
    /// The bytes after field @p id's identifier, or empty if absent. Only
    /// the first field is checked: replies carry one field.
    std::span<const std::uint8_t> field(std::uint8_t id) const;
};

/// Splits a byte stream from the chip into packets. Bytes outside a
/// packet (the pad byte after an odd-length packet, noise) are skipped.
class PacketParser
{
public:
    /// Feeds one byte; true when it completed @p out.
    bool feed(std::uint8_t byte, Packet &out);
    void reset() { m_state = 0; }

private:
    int m_state = 0;
    std::uint16_t m_have = 0;
    Packet m_packet;
};

/// The SCT3258 DMR/dPMR baseband processor on its host port.
class Sct3258
{
public:
    // Packet types (TYPE byte).
    static constexpr std::uint8_t TypeControl = 0x00;
    /// Added to a type to read fields back instead of writing them.
    static constexpr std::uint8_t TypeRead = 0x20;
    static constexpr std::uint8_t TypeCodec = 0x40;
    /// The packet type for DMR fields.
    static constexpr std::uint8_t TypeDmr = 0x08;
    /// Set in reports of what was received from the far end.
    static constexpr std::uint8_t TypeFarEnd = 0x10;

    // Field identifiers.
    static constexpr std::uint8_t VocoderSelect = 0x10;
    static constexpr std::uint8_t WorkMode = 0x18;
    static constexpr std::uint8_t CarrierReady = 0x19;
    static constexpr std::uint8_t ProcessMode = 0x1A;
    static constexpr std::uint8_t AnalogConfig = 0x3C;
    static constexpr std::uint8_t AckMessage = 0x17;
    static constexpr std::uint8_t AnalogTone = 0x1F;
    static constexpr std::uint8_t CodecSelect = 0x2D;
    static constexpr std::uint8_t QueryProduct = 0x30;
    static constexpr std::uint8_t QueryHardware = 0x31;
    static constexpr std::uint8_t QuerySoftware = 0x32;
    static constexpr std::uint8_t NewEvent = 0x1B;
    static constexpr std::uint8_t ReportField = 0x1D;
    static constexpr std::uint8_t SyncMissCount = 0x2F;
    static constexpr std::uint8_t DataFrame = 0x43;
    static constexpr std::uint8_t DmrCallSlot = 0x6F;
    static constexpr std::uint8_t DmrFlc = 0x76;
    static constexpr std::uint8_t DmrColourCode = 0x77;
    static constexpr std::uint8_t DmrCallOption = 0x79;
    static constexpr std::uint8_t DmrSlotType = 0x7B;
    static constexpr std::uint8_t DmrSlotFound = 0x7F;

    // WorkMode codes.
    static constexpr std::uint8_t WorkIdle = 0;
    static constexpr std::uint8_t WorkReceive = 1;
    static constexpr std::uint8_t WorkTransmit = 2;

    // CodecSelect codes.
    static constexpr std::uint8_t CodecTlv320aic3204 = 1;

    // ACK_MESSAGE codes.
    static constexpr std::uint8_t AckReady = 0x01;
    static constexpr std::uint8_t AckReadyNoFirmware = 0x00;
    /// ACK_MESSAGE code: everything queued for the far end has gone out.
    static constexpr std::uint8_t AckAllSent = 0x0A;

    // PROCESS_MODE values.
    static constexpr std::uint8_t ProcessLayer2 = 0x02;
    static constexpr std::uint8_t ProcessAnalog = 0x80;

    // VOCODER_SEL values: AMBE+2, the one the radio's own firmware selects
    // on digital channels, and analog.
    static constexpr std::uint8_t VocoderAmbePlus2 = 0x04;
    static constexpr std::uint8_t VocoderDigital = 0x0C;
    static constexpr std::uint8_t VocoderAnalog = 0x10;

    enum class BootResult : std::uint8_t
    {
        Ready,
        /// Booted, but reports no application firmware.
        NoFirmware,
        NoAnswer,
        WrongAnswer,
        /// The boot program is not 2048 bytes.
        BadBlob,
    };

    Sct3258(hal::HostPortBus &bus, lib::FnRef<void(unsigned)> delayUs);

    /// Sends the boot program right after reset and waits for the chip to
    /// report ready. @p ackCode receives the code of its first ACK_MESSAGE.
    BootResult boot(std::span<const std::uint8_t> blob, std::uint8_t &ackCode, unsigned timeoutMs = 2000);

    /// The chip's status line says it has data for the host.
    bool dataWaiting();

    /// Sends one packet; an odd total length gets a pad byte.
    void send(std::uint8_t type, std::span<const std::uint8_t> fields);

    /// Waits up to @p timeoutMs for the next packet from the chip.
    bool receive(Packet &out, unsigned timeoutMs);

    /// Sends a packet and waits for the chip's reply to it. Reports that
    /// arrive meanwhile are kept for nextReport().
    bool request(std::uint8_t type, std::span<const std::uint8_t> fields, Packet &reply, unsigned timeoutMs = 100);

    /// A query field (QueryProduct...) as text; false if not answered.
    bool queryText(std::uint8_t field, char *text, std::size_t capacity);

    /// Sends a control field and checks the chip's answer: the same field
    /// with status 0. Returns the status, or -1 without an answer.
    int control(std::span<const std::uint8_t> fields, unsigned timeoutMs = 100);
    /// As control(), in a packet of @p type (TypeDmr for DMR fields).
    int control(std::uint8_t type, std::span<const std::uint8_t> fields, unsigned timeoutMs = 100);

    /// Writes one external codec register through the chip (page, register,
    /// value). The chip passes the fields (a zero, page, register, value, two
    /// zeros) to the codec. True when the chip accepted it.
    bool codecWrite(std::uint8_t page, std::uint8_t reg, std::uint8_t value);

    /// A packet the chip sent on its own: what it received from the far
    /// end, an event, or the note that everything queued has gone out.
    static bool isReport(const Packet &packet);
    /// The next report: one a request set aside, else one waiting now.
    /// False when there is none. Longer reports are cut to ReportBytes.
    bool nextReport(Packet &out);
    static constexpr std::size_t ReportBytes = 48;

private:
    struct Report
    {
        std::uint8_t type;
        std::uint8_t length;
        std::uint8_t fields[ReportBytes];
    };
    void keep(const Packet &packet);

    hal::HostPortBus &m_bus;
    lib::FnRef<void(unsigned)> m_delayUs;
    PacketParser m_parser;
    lib::RingBuffer<Report, 8> m_reports;
};

} // namespace dmm::drivers::baseband
