// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "dmm/services/loader_protocol.h"

#include "dmm/lib/crc32.h"

namespace dmm::services::boot {

namespace {

constexpr std::uint8_t OpInfo = 'i';
constexpr std::uint8_t OpErase = 'e';
constexpr std::uint8_t OpWrite = 'w';
constexpr std::uint8_t OpChecksum = 'k';
constexpr std::uint8_t OpCommit = 'c';
constexpr std::uint8_t OpBoot = 'b';
constexpr std::uint8_t InfoMagic[] = {'D', 'M', 'L', 'D'};

std::uint32_t le32(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
        static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

std::uint16_t le16(const std::uint8_t *p)
{
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

// Collects a reply and sends it with its length, status and CRC.
class Reply
{
public:
    Reply(LoaderProtocol::Writer write, std::uint8_t op)
        : m_write(write), m_op(op)
    {
    }

    void put8(std::uint8_t v)
    {
        if (m_length < sizeof m_payload) {
            m_payload[m_length++] = v;
        }
    }
    void put32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            put8(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }
    void putText(std::string_view text, std::size_t max)
    {
        const std::size_t n = text.size() < max ? text.size() : max;
        put8(static_cast<std::uint8_t>(n));
        for (std::size_t i = 0; i < n; ++i) {
            put8(static_cast<std::uint8_t>(text[i]));
        }
    }

    void send(LoaderStatus status)
    {
        const std::uint8_t head[] = {LoaderProtocol::Start, m_op, static_cast<std::uint8_t>(m_length),
            static_cast<std::uint8_t>(m_length >> 8)};
        std::uint32_t crc = lib::crc32(head, sizeof head);
        crc = lib::crc32(m_payload, m_length, crc);
        const auto s = static_cast<std::uint8_t>(status);
        crc = lib::crc32(&s, 1, crc);
        const std::uint8_t tail[] = {s, static_cast<std::uint8_t>(crc), static_cast<std::uint8_t>(crc >> 8),
            static_cast<std::uint8_t>(crc >> 16), static_cast<std::uint8_t>(crc >> 24)};
        m_write(head);
        m_write(std::span(m_payload, m_length));
        m_write(tail);
    }

private:
    LoaderProtocol::Writer m_write;
    std::uint8_t m_op;
    std::uint8_t m_payload[192]{};
    std::size_t m_length = 0;
};

} // namespace

LoaderProtocol::LoaderProtocol(Loader &loader, std::string_view model, std::string_view version)
    : m_loader(loader), m_model(model), m_version(version)
{
}

std::size_t LoaderProtocol::expectedLength() const
{
    switch (m_request[1]) {
    case OpInfo:
        return 2;
    case OpErase:
        return 6;
    case OpWrite:
        return m_have < 8 ? 8 : 8 + le16(m_request + 6) + 4;
    case OpChecksum:
        return 10;
    case OpCommit:
        return 11;
    case OpBoot:
        return 5;
    default:
        return 0;
    }
}

void LoaderProtocol::feed(std::span<const std::uint8_t> bytes, std::uint32_t nowMs, Writer write)
{
    if (m_have != 0 && nowMs - m_startedAt > RequestTimeoutMs) {
        m_have = 0;
    }
    for (std::uint8_t b : bytes) {
        if (m_have == 0) {
            if (b == Start) {
                m_request[m_have++] = b;
                m_startedAt = nowMs;
            }
            continue;
        }
        m_request[m_have++] = b;
        const std::size_t length = expectedLength();
        const bool tooLong = m_request[1] == OpWrite && m_have >= 8 && le16(m_request + 6) > MaxWrite;
        if (length == 0 || tooLong) {
            Reply(write, m_request[1]).send(LoaderStatus::BadRequest);
            m_have = 0;
        } else if (m_have == length) {
            handle(write);
            m_have = 0;
        }
    }
}

void LoaderProtocol::info(Writer write)
{
    Reply r(write, OpInfo);
    for (std::uint8_t b : InfoMagic) {
        r.put8(b);
    }
    r.put8(Version);
    r.putText(m_model, 16);
    r.putText(m_version, 24);
    const BootLayout &layout = m_loader.layout();
    r.put32(layout.slotA.begin);
    r.put32(layout.slotA.end - layout.slotA.begin);
    r.put32(layout.slotB.begin);
    r.put32(layout.slotB.end - layout.slotB.begin);
    const BootState &s = m_loader.record().state();
    for (const bool slotB : {false, true}) {
        const SlotRecord &slot = slotB ? s.b : s.a;
        r.put8(static_cast<std::uint8_t>(slot.state));
        r.put8(static_cast<std::uint8_t>(slot.mode));
        r.put32(slot.length);
        r.put32(slot.crc);
        std::uint8_t bytes[ImageInfoBytes];
        ImageInfo image;
        const std::uint32_t at = (slotB ? layout.slotB.begin : layout.slotA.begin) + layout.infoOffset;
        const bool known = slot.state != SlotState::Empty && m_loader.flash().read(at, bytes) && parseImageInfo(bytes, image) &&
            image.kind == ImageKind::App;
        r.putText(known ? std::string_view(image.version) : std::string_view(), ImageVersionBytes);
    }
    r.put8(s.attempts);
    r.put8(Loader::MaxAttempts);
    r.put8(static_cast<std::uint8_t>(s.lastFallback));
    r.put8(s.swap.active ? 1 : 0);
    r.put8(static_cast<std::uint8_t>(m_stay));
    r.send(LoaderStatus::Ok);
}

void LoaderProtocol::handle(Writer write)
{
    const std::uint8_t op = m_request[1];
    const std::uint8_t *p = m_request + 2;
    const BootLayout &layout = m_loader.layout();
    const std::uint32_t capacity = layout.slotB.end - layout.slotB.begin;
    BootRecord &record = m_loader.record();
    const BootState &s = record.state();
    Reply r(write, op);

    switch (op) {
    case OpInfo:
        info(write);
        return;
    case OpErase: {
        const std::uint32_t length = le32(p);
        if (length > capacity) {
            r.send(LoaderStatus::OutOfRange);
        } else if (s.b.state == SlotState::Confirmed && s.a.state != SlotState::Confirmed) {
            r.send(LoaderStatus::Refused);
        } else {
            r.send(m_loader.eraseDownload(length) ? LoaderStatus::Ok : LoaderStatus::FlashError);
        }
        return;
    }
    case OpWrite: {
        const std::uint32_t offset = le32(p);
        const std::uint16_t length = le16(p + 4);
        const std::uint8_t *data = p + 6;
        const std::uint32_t crc = le32(data + length);
        if (lib::crc32(data, length) != crc) {
            r.send(LoaderStatus::BadCrc);
        } else if (length == 0 || length % 4 != 0 || offset % 4 != 0 || offset > capacity || length > capacity - offset) {
            r.send(LoaderStatus::OutOfRange);
        } else if (s.b.state != SlotState::Empty) {
            r.send(LoaderStatus::Refused);
        } else {
            const std::uint32_t at = layout.slotB.begin + offset;
            m_loader.events().busy(true);
            const bool programmed = m_loader.flash().program(at, std::span(data, length));
            m_loader.events().busy(false);
            std::uint32_t back = 0;
            if (!programmed) {
                r.send(LoaderStatus::FlashError);
            } else if (!flashCrc(m_loader.flash(), at, length, back) || back != crc) {
                r.send(LoaderStatus::VerifyFailed);
            } else {
                r.send(LoaderStatus::Ok);
            }
        }
        return;
    }
    case OpChecksum: {
        const std::uint32_t offset = le32(p);
        const std::uint32_t length = le32(p + 4);
        std::uint32_t crc = 0;
        if (offset > capacity || length > capacity - offset) {
            r.send(LoaderStatus::OutOfRange);
        } else if (!flashCrc(m_loader.flash(), layout.slotB.begin + offset, length, crc)) {
            r.send(LoaderStatus::FlashError);
        } else {
            r.put32(crc);
            r.send(LoaderStatus::Ok);
        }
        return;
    }
    case OpCommit: {
        const std::uint32_t length = le32(p);
        const std::uint32_t crc = le32(p + 4);
        const std::uint8_t mode = p[8];
        if (mode > 1) {
            r.send(LoaderStatus::BadRequest);
            return;
        }
        if (s.b.state != SlotState::Empty) {
            r.send(LoaderStatus::Refused);
            return;
        }
        const ImageProblem problem = checkImage(m_loader.flash(), layout, layout.slotB.begin, length, true, crc);
        if (problem != ImageProblem::None) {
            r.put8(static_cast<std::uint8_t>(problem));
            r.send(LoaderStatus::ImageRejected);
            return;
        }
        const SlotRecord committed{SlotState::Committed, static_cast<ConfirmMode>(mode), length, crc};
        r.send(record.reserve(1) && record.append(entry::slot(true, committed)) ? LoaderStatus::Ok : LoaderStatus::FlashError);
        return;
    }
    case OpBoot: {
        const std::uint8_t mode = p[0];
        if (mode > 2) {
            r.send(LoaderStatus::BadRequest);
            return;
        }
        m_restart.wanted = true;
        m_restart.request = mode == 1 ? Request::Rollback : Request::None;
        m_restart.swapCut = mode == 2 ? le16(p + 1) : 0;
        r.send(LoaderStatus::Ok);
        return;
    }
    default:
        r.send(LoaderStatus::BadRequest);
        return;
    }
}

} // namespace dmm::services::boot
