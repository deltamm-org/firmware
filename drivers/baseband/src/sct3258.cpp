// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/drivers/baseband/sct3258.h"

namespace dmm::drivers::baseband {

namespace {

constexpr std::uint8_t Start[] = {0x84, 0xA9, 0x61};
constexpr std::size_t BootBlobBytes = 2048;
constexpr unsigned PollUs = 20;
// Words read per poll before the next delay, so a status line stuck at
// "data waiting" still lets the delay function yield.
constexpr unsigned WordsPerPoll = 8;
// Packets dropped before a request, at most.
constexpr int MaxStalePackets = 8;

} // namespace

std::span<const std::uint8_t> Packet::field(std::uint8_t id) const
{
    if (length == 0 || fields[0] != id) {
        return {};
    }
    return {fields + 1, static_cast<std::size_t>(length - 1)};
}

bool PacketParser::feed(std::uint8_t byte, Packet &out)
{
    switch (m_state) {
    case 0:
    case 1:
    case 2:
        if (byte == Start[m_state]) {
            ++m_state;
        } else {
            m_state = byte == Start[0] ? 1 : 0;
        }
        return false;
    case 3:
        m_packet.length = static_cast<std::uint16_t>(byte << 8);
        m_state = 4;
        return false;
    case 4:
        m_packet.length = static_cast<std::uint16_t>(m_packet.length | byte);
        m_state = m_packet.length > Packet::MaxFields ? 0 : 5;
        return false;
    case 5:
        m_packet.type = byte;
        m_have = 0;
        if (m_packet.length == 0) {
            m_state = 0;
            out = m_packet;
            return true;
        }
        m_state = 6;
        return false;
    default:
        m_packet.fields[m_have++] = byte;
        if (m_have == m_packet.length) {
            m_state = 0;
            out = m_packet;
            return true;
        }
        return false;
    }
}

Sct3258::Sct3258(hal::HostPortBus &bus, lib::FnRef<void(unsigned)> delayUs)
    : m_bus(bus), m_delayUs(delayUs)
{
}

Sct3258::BootResult Sct3258::boot(std::span<const std::uint8_t> blob, std::uint8_t &ackCode, unsigned timeoutMs)
{
    if (blob.size() != BootBlobBytes) {
        return BootResult::BadBlob;
    }
    // The chip takes exactly 1024 16-bit words straight after reset, with no
    // packet framing.
    m_bus.write(blob.data(), blob.size());
    m_parser.reset();

    // Its status line already reads "data waiting" before the boot load, so
    // only a well-formed ACK_MESSAGE counts.
    Packet p;
    for (unsigned waited = 0; waited < timeoutMs; ++waited) {
        if (!receive(p, 1)) {
            continue;
        }
        const auto ack = p.field(AckMessage);
        if (ack.size() != 1) {
            continue;
        }
        ackCode = ack[0];
        if (ack[0] == AckReady) {
            return BootResult::Ready;
        }
        return ack[0] == AckReadyNoFirmware ? BootResult::NoFirmware : BootResult::WrongAnswer;
    }
    return BootResult::NoAnswer;
}

void Sct3258::send(std::uint8_t type, std::span<const std::uint8_t> fields)
{
    std::uint8_t packet[6 + Packet::MaxFields + 1];
    const std::size_t n = fields.size() > Packet::MaxFields ? Packet::MaxFields : fields.size();
    packet[0] = Start[0];
    packet[1] = Start[1];
    packet[2] = Start[2];
    packet[3] = static_cast<std::uint8_t>(n >> 8);
    packet[4] = static_cast<std::uint8_t>(n);
    packet[5] = type;
    for (std::size_t i = 0; i < n; ++i) {
        packet[6 + i] = fields[i];
    }
    std::size_t total = 6 + n;
    if (total % 2 != 0) {
        packet[total++] = 0; // the host port moves 16-bit words
    }
    m_bus.write(packet, total);
}

bool Sct3258::receive(Packet &out, unsigned timeoutMs)
{
    // Bounded in time even when the status line never goes idle: every poll
    // reads at most WordsPerPoll words, then waits PollUs.
    const unsigned polls = timeoutMs * 1000 / PollUs;
    for (unsigned i = 0; i <= polls; ++i) {
        for (unsigned words = 0; words < WordsPerPoll && m_bus.dataWaiting(); ++words) {
            // Data comes in 16-bit words: always read both bytes.
            const std::uint8_t a = m_bus.read();
            const std::uint8_t b = m_bus.read();
            const bool first = m_parser.feed(a, out);
            if (first) {
                // A packet ended on the first byte: the second is padding.
                return true;
            }
            if (m_parser.feed(b, out)) {
                return true;
            }
        }
        if (i < polls) {
            m_delayUs(PollUs);
        }
    }
    return false;
}

bool Sct3258::sendFrame(std::uint8_t colourCode, std::uint8_t property, std::span<const std::uint8_t> data,
    unsigned timeoutMs)
{
    constexpr std::size_t MaxData = 36;
    if (data.size() > MaxData) {
        return false;
    }
    std::uint8_t fields[5 + MaxData] = {DmrColourCode, static_cast<std::uint8_t>(colourCode & 0x0F), DataFrame, property,
        static_cast<std::uint8_t>(data.size())};
    for (std::size_t i = 0; i < data.size(); ++i) {
        fields[5 + i] = data[i];
    }
    Packet reply;
    if (!request(TypeDmr, std::span(fields, 5 + data.size()), reply, timeoutMs)) {
        return false;
    }
    // The chip acknowledges each frame with the bare DIGC_DATA_FRAME field.
    return reply.length >= 1 && reply.fields[0] == DataFrame;
}

bool Sct3258::isReport(const Packet &p)
{
    if ((p.type & TypeFarEnd) != 0) {
        return true;
    }
    const auto ack = p.field(AckMessage);
    return !p.field(NewEvent).empty() || (!ack.empty() && ack[0] == AckAllSent);
}

void Sct3258::keep(const Packet &p)
{
    if (m_reports.full()) {
        m_reports.pop(); // the oldest goes
    }
    Report r{p.type, static_cast<std::uint8_t>(p.length < ReportBytes ? p.length : ReportBytes), {}};
    for (std::size_t i = 0; i < r.length; ++i) {
        r.fields[i] = p.fields[i];
    }
    m_reports.push(r);
}

bool Sct3258::nextReport(Packet &out)
{
    if (const auto r = m_reports.pop()) {
        out.type = r->type;
        out.length = r->length;
        for (std::size_t i = 0; i < r->length; ++i) {
            out.fields[i] = r->fields[i];
        }
        return true;
    }
    // Each pass reads a bounded number of words, so a long report takes a
    // few; the bound keeps a stuck status line from holding the caller.
    for (int i = 0; i < 4 * MaxStalePackets && m_bus.dataWaiting(); ++i) {
        if (receive(out, 0) && isReport(out)) {
            return true;
        }
    }
    return false;
}

bool Sct3258::request(std::uint8_t type, std::span<const std::uint8_t> fields, Packet &reply, unsigned timeoutMs)
{
    // Whatever is waiting now came before the request: keep the reports,
    // drop the rest.
    Packet early;
    for (int i = 0; i < MaxStalePackets && receive(early, 0); ++i) {
        if (isReport(early)) {
            keep(early);
        }
    }
    send(type, fields);
    // Reports may come before the reply; each restarts the wait, a bounded
    // number of times.
    for (int i = 0; i < MaxStalePackets; ++i) {
        if (!receive(reply, timeoutMs)) {
            return false;
        }
        if (!isReport(reply)) {
            return true;
        }
        keep(reply);
    }
    return false;
}

bool Sct3258::dataWaiting()
{
    return m_bus.dataWaiting();
}

bool Sct3258::queryText(std::uint8_t field, char *text, std::size_t capacity)
{
    const std::uint8_t q[] = {field};
    Packet reply;
    if (capacity == 0 || !request(TypeControl | TypeRead, q, reply)) {
        return false;
    }
    const auto data = reply.field(field);
    if (data.empty()) {
        return false;
    }
    // A length byte, then the text.
    std::size_t n = data[0] < data.size() - 1 ? data[0] : data.size() - 1;
    if (n > capacity - 1) {
        n = capacity - 1;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const char c = static_cast<char>(data[1 + i]);
        text[i] = c >= ' ' && c <= '~' ? c : '?';
    }
    text[n] = '\0';
    return true;
}

int Sct3258::control(std::span<const std::uint8_t> fields, unsigned timeoutMs)
{
    return control(TypeControl, fields, timeoutMs);
}

int Sct3258::control(std::uint8_t type, std::span<const std::uint8_t> fields, unsigned timeoutMs)
{
    Packet reply;
    if (fields.empty() || !request(type, fields, reply, timeoutMs)) {
        return -1;
    }
    const auto echo = reply.field(fields[0]);
    if (!echo.empty()) {
        return echo[0];
    }
    const auto ack = reply.field(AckMessage);
    return ack.empty() ? -1 : 0x100 | ack[0];
}

bool Sct3258::codecWrite(std::uint8_t page, std::uint8_t reg, std::uint8_t value)
{
    const std::uint8_t command[] = {0x00, page, reg, value, 0x00, 0x00};
    Packet reply;
    if (!request(TypeCodec, command, reply)) {
        return false;
    }
    const auto ack = reply.field(AckMessage);
    return !ack.empty() && ack[0] == 0;
}

} // namespace dmm::drivers::baseband
