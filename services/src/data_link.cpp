// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/data_link.h"

#include "dmm/lib/crc32.h"

namespace dmm::services {

namespace {

constexpr std::uint8_t OpInfo = 'i';
constexpr std::uint8_t OpRead = 'r';
constexpr std::uint8_t OpReadPhysical = 'p';
constexpr std::uint8_t OpLoader = 'B';
constexpr std::uint8_t OpConfirm = 'K';

// Total request length once its letter is known; 0 = unknown letter.
std::size_t requestLength(std::uint8_t op)
{
    switch (op) {
    case OpInfo:
    case OpConfirm:
        return 2;
    case OpLoader:
        return 3;
    case OpRead:
    case OpReadPhysical:
        return 8;
    default:
        return 0;
    }
}

std::uint32_t le32(const std::uint8_t *p)
{
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
        static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

// Writes pieces of a reply and keeps its CRC.
class ReplyWriter
{
public:
    explicit ReplyWriter(DataLink::Writer write)
        : m_write(write)
    {
    }

    void put(std::span<const std::uint8_t> bytes)
    {
        m_crc = lib::crc32(bytes.data(), bytes.size(), m_crc);
        m_write(bytes);
    }

    void put8(std::uint8_t v) { put(std::span(&v, 1)); }

    void put16(std::uint16_t v)
    {
        const std::uint8_t b[] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8)};
        put(b);
    }

    void put32(std::uint32_t v)
    {
        const std::uint8_t b[] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
            static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)};
        put(b);
    }

    void finish()
    {
        const std::uint32_t crc = m_crc;
        const std::uint8_t b[] = {static_cast<std::uint8_t>(crc), static_cast<std::uint8_t>(crc >> 8),
            static_cast<std::uint8_t>(crc >> 16), static_cast<std::uint8_t>(crc >> 24)};
        m_write(b);
    }

private:
    DataLink::Writer m_write;
    std::uint32_t m_crc = 0;
};

} // namespace

DataLink::DataLink(Storage *storage, std::string_view version, SystemControl *system)
    : m_storage(storage), m_version(version), m_system(system)
{
}

std::optional<LoaderRequest> DataLink::takeLoaderRequest()
{
    const std::optional<LoaderRequest> r = m_loaderRequest;
    m_loaderRequest.reset();
    return r;
}

void DataLink::feed(std::span<const std::uint8_t> bytes, os::Ticks now, Writer write)
{
    if (m_have != 0 && now - m_startedAt > RequestTimeoutMs) {
        m_have = 0;
    }
    for (std::uint8_t b : bytes) {
        if (m_have == 0) {
            if (b == Start) {
                m_request[m_have++] = b;
                m_startedAt = now;
            }
            continue;
        }
        m_request[m_have++] = b;
        const std::size_t length = requestLength(m_request[1]);
        if (length == 0) {
            ReplyWriter r(write);
            r.put8(Start);
            r.put8(m_request[1]);
            r.put16(0);
            r.put8(BadRequest);
            r.finish();
            m_have = 0;
        } else if (m_have == length) {
            handle(write);
            m_have = 0;
        }
    }
}

void DataLink::handle(Writer write)
{
    const std::uint8_t op = m_request[1];
    ReplyWriter r(write);
    r.put8(Start);
    r.put8(op);
    if (op == OpInfo) {
        const StorageInfo info = m_storage != nullptr ? m_storage->info() : StorageInfo{};
        const auto versionLength = static_cast<std::uint8_t>(m_version.size() < 32 ? m_version.size() : 32);
        r.put16(static_cast<std::uint16_t>(10 + versionLength + (m_system != nullptr ? 7 : 0)));
        r.put8(info.present ? 1 : 0);
        r.put8(info.maker);
        r.put8(info.device);
        r.put32(info.sizeBytes);
        r.put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(info.remappedBlocks)));
        r.put8(versionLength);
        r.put(std::span(reinterpret_cast<const std::uint8_t *>(m_version.data()), versionLength));
        if (m_system != nullptr) {
            const LoaderStatus l = m_system->loaderStatus();
            r.put8(l.present ? 1 : 0);
            r.put8(l.trial ? 1 : 0);
            r.put8(l.manual ? 1 : 0);
            r.put8(l.attempts);
            r.put8(l.maxAttempts);
            r.put8(l.previousKept ? 1 : 0);
            r.put8(static_cast<std::uint8_t>(l.fallback));
        }
        r.put8(0);
        r.finish();
        return;
    }
    if (op == OpLoader) {
        const std::uint8_t which = m_request[2];
        const bool known = which == 1 || which == 2;
        const bool available = m_system != nullptr && !m_busy && m_system->loaderStatus().present;
        r.put16(0);
        r.put8(!known ? BadRequest : available ? 0
                                               : Refused);
        r.finish();
        if (known && available) {
            m_loaderRequest = which == 1 ? LoaderRequest::Stay : LoaderRequest::Rollback;
        }
        return;
    }
    if (op == OpConfirm) {
        const bool confirmed = m_system != nullptr && m_system->confirmImage();
        r.put16(0);
        r.put8(confirmed ? 0 : Refused);
        r.finish();
        return;
    }

    const std::uint32_t address = le32(m_request + 2);
    const auto length = static_cast<std::uint16_t>(m_request[6] | (m_request[7] << 8));
    if (length == 0 || length > MaxReadLength) {
        r.put16(0);
        r.put8(BadRequest);
        r.finish();
        return;
    }
    r.put16(length);
    // Stream in small pieces; an unreadable piece is sent as 0xFF bytes.
    StorageResult worst = StorageResult::Ok;
    std::uint8_t chunk[256];
    for (std::uint32_t done = 0; done < length;) {
        const std::uint32_t n = length - done < sizeof chunk ? length - done : sizeof chunk;
        const std::span<std::uint8_t> piece(chunk, n);
        const StorageResult res =
            m_storage != nullptr ? m_storage->read(address + done, piece, op == OpReadPhysical) : StorageResult::NoChip;
        if (!usable(res) && res != StorageResult::Uncorrectable) {
            for (std::uint8_t &b : piece) {
                b = 0xFF;
            }
        }
        if (static_cast<int>(res) > static_cast<int>(worst)) {
            worst = res;
        }
        r.put(piece);
        done += n;
    }
    r.put8(static_cast<std::uint8_t>(worst));
    r.finish();
}

} // namespace dmm::services
