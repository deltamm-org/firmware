// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "sim_storage.h"

#include "dmm/services/d878uv2_calibration.h"

namespace {

constexpr std::uint32_t Size = 256u * 1024 * 1024;

} // namespace

SimStorage::SimStorage()
{
    using dmm::services::D878Uv2Calibration;
    for (std::uint32_t i = 0; i < D878Uv2Calibration::Bytes; ++i) {
        put(D878Uv2Calibration::CopyA + i, 0);
    }
    put(D878Uv2Calibration::CopyA + 0x11, 100); // battery offset 0
    put(D878Uv2Calibration::CopyA + 0x14, 'w');
    put(D878Uv2Calibration::CopyA + 0x15, 'w');
    const std::uint8_t valid[] = {0x55, 0x55, 0xAA, 0xAA};
    for (std::uint32_t i = 0; i < 4; ++i) {
        put(D878Uv2Calibration::CopyA + D878Uv2Calibration::BlockBytes - 4 + i, valid[i]);
    }
    putSampleCodeplug();
}

void SimStorage::putBytes(std::uint32_t address, std::initializer_list<std::uint8_t> bytes)
{
    for (const std::uint8_t b : bytes) {
        put(address++, b);
    }
}

void SimStorage::markValid(std::uint32_t block)
{
    putBytes(block + 0x1FFF8, {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0xAA, 0xAA});
}

// One 64-byte channel record.
void SimStorage::putChannel(std::uint32_t address, std::uint32_t hz, std::int32_t shiftHz, bool wide,
    std::uint8_t ctcssIndex, std::uint8_t mode, std::uint8_t power, const char *name)
{
    auto bcd = [&](std::uint32_t at, std::uint32_t v) {
        for (int i = 3; i >= 0; --i) {
            put(at + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>((v % 10) | ((v / 10 % 10) << 4)));
            v /= 100;
        }
    };
    for (std::uint32_t i = 0; i < 0x40; ++i) {
        put(address + i, 0);
    }
    bcd(address, hz / 10);
    bcd(address + 4, static_cast<std::uint32_t>(shiftHz < 0 ? -shiftHz : shiftHz) / 10);
    const unsigned direction = shiftHz == 0 ? 0 : shiftHz > 0 ? 1
                                                              : 2;
    put(address + 8, static_cast<std::uint8_t>(mode | power << 2 | (wide ? 0x10 : 0) | direction << 6));
    put(address + 9, ctcssIndex != 0xFF ? static_cast<std::uint8_t>(1 << 2) : 0);
    put(address + 0x0A, ctcssIndex != 0xFF ? ctcssIndex : 0);
    for (std::uint32_t i = 0; name[i] != '\0' && i < 16; ++i) {
        put(address + 0x23 + i, static_cast<std::uint8_t>(name[i]));
    }
}

void SimStorage::putSampleCodeplug()
{
    // Channels 0-3 in bank 0; channel bitmap; zone bitmap.
    markValid(0x00800000);
    putChannel(0x00800000 + 0 * 0x40, 145'500'000, 0, true, 0xFF, 0, 0, "Calling 2m");
    putChannel(0x00800000 + 1 * 0x40, 145'600'000, -600'000, true, 9, 0, 2, "Repeater 2m");
    putChannel(0x00800000 + 2 * 0x40, 433'500'000, 0, false, 0xFF, 0, 0, "Calling 70cm");
    putChannel(0x00800000 + 3 * 0x40, 438'150'000, -5'000'000, false, 0xFF, 1, 1, "DMR repeater");
    markValid(0x024C0000);
    for (std::uint32_t i = 0; i < 32; ++i) {
        put(0x024C1300 + i, 0);
    }
    for (std::uint32_t i = 0; i < 500; ++i) {
        put(0x024C1500 + i, 0);
    }
    put(0x024C1300, 0x03); // zones 0 and 1
    put(0x024C1500, 0x0F); // channels 0-3
    // Zone channel lists, names and last selections.
    markValid(0x01000000);
    putBytes(0x01000000, {0, 0, 1, 0, 0xFF, 0xFF});
    putBytes(0x01000200, {2, 0, 3, 0, 0xFF, 0xFF});
    markValid(0x02540000);
    putBytes(0x02540000, {'V', 'H', 'F', 0});
    putBytes(0x02540020, {'U', 'H', 'F', 0});
    markValid(0x02500000);
    for (std::uint32_t i = 0; i < 0x100; ++i) {
        put(0x02500000 + i, 0);
    }
    put(0x02500000 + 0x09, 3); // squelch A
    put(0x02500000 + 0x0A, 3);
    put(0x02500000 + 0x04, 1); // 30 s timeout
    put(0x02500000 + 0xBE, 0x0B);
    putBytes(0x02500100, {1, 0});
    // APRS: N0CALL-9, WIDE1-1,WIDE2-1, symbol /[, 144.800 MHz wide, a
    // beacon every 10 min, and a comment.
    for (std::uint32_t i = 0; i < 0x100; ++i) {
        put(0x02501000 + i, 0);
    }
    put(0x02501000 + 0x0B, 20);
    putBytes(0x02501000 + 0x1D, {'N', '0', 'C', 'A', 'L', 'L', 9});
    putBytes(0x02501000 + 0x24, {'W', 'I', 'D', 'E', '1', '-', '1', ',', 'W', 'I', 'D', 'E', '2', '-', '1'});
    putBytes(0x02501000 + 0x39, {'/', '['});
    put(0x02501000 + 0xAA, 1);
    putBytes(0x02501000 + 0xAC, {0x14, 0x48, 0x00, 0x00});
    putBytes(0x02501200, {'S', 'i', 'm', 'u', 'l', 'a', 't', 'o', 'r', 0});
    // VFOs.
    markValid(0x00FC0000);
    putChannel(0x00FC0800, 145'450'000, 0, true, 0xFF, 0, 0, "");
    putChannel(0x00FC0840, 433'500'000, 0, false, 0xFF, 0, 0, "");
}

dmm::services::StorageInfo SimStorage::info()
{
    return {true, 0x98, 0xCB, Size, 0};
}

dmm::services::StorageResult SimStorage::read(std::uint32_t address, std::span<std::uint8_t> data, bool)
{
    if (address >= Size || data.size() > Size - address) {
        return dmm::services::StorageResult::OutOfRange;
    }
    std::lock_guard guard(m_lock);
    for (std::size_t i = 0; i < data.size(); ++i) {
        const auto it = m_bytes.find(address + static_cast<std::uint32_t>(i));
        data[i] = it == m_bytes.end() ? 0xFF : it->second;
    }
    return dmm::services::StorageResult::Ok;
}
