// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "board_fm_tuning.h"

#include "dmm/services/log.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace board {

namespace {

using Tuning = dmm::drivers::radio::Rda1846::Tuning;
using Setting = dmm::drivers::radio::Rda1846::Setting;
using WidthSetting = dmm::drivers::radio::Rda1846::WidthSetting;
using RangeSetting = dmm::drivers::radio::Rda1846::RangeSetting;
using BandWord = dmm::drivers::radio::Rda1846::BandWord;

// Where the radio information block names the chip version, first and
// second copy.
constexpr std::uint32_t VersionAt[] = {0x02FA0120, 0x02FC0120};
constexpr std::string_view VersionN = "1846N";

// Start-up, before calibration. 0x0A: PA bias pin voltage and drive. 0x40:
// FM audio paths. 0x42/0x43: receive audio output level. 0x44: receive
// volume step 9 of 15, full DAC gain, transmit gain 9. 0x45: tone detector
// set-up. 0x47: soft mute, kept shallow so weak signals stay audible.
// 0x58/0x59: filters and deviation until the first receive or transmit.
// 0x5A: squelch detection counts. 0x63: pre-emphasis bypass threshold at
// its top. The N version also takes 0x0F.
constexpr Setting StartUpS[] = {
    {0x04, 0x0FD0}, // 26 MHz reference
    {0x0A, 0x4380},
    {0x31, 0x0031},
    {0x40, 0x0030},
    {0x41, 0x470F},
    {0x42, 0x1036},
    {0x43, 0x00A9},
    {0x44, 0x09F9},
    {0x45, 0x100F},
    {0x47, 0x7F2F},
    {0x4F, 0x2C62},
    {0x53, 0x0094},
    {0x54, 0x2A18},
    {0x55, 0x0081},
    {0x57, 0x1C00},
    {0x58, 0x840D},
    {0x59, 0x0C50},
    {0x5A, 0x0CDD},
    {0x63, 0x3FFF},
};
constexpr Setting StartUpN[] = {
    {0x04, 0x0FD0}, // 26 MHz reference
    {0x0A, 0x4380},
    {0x0F, 0x8C24},
    {0x31, 0x0031},
    {0x40, 0x0030},
    {0x41, 0x470F},
    {0x42, 0x1036},
    {0x43, 0x00A9},
    {0x44, 0x09F9},
    {0x45, 0x100F},
    {0x47, 0x7F2F},
    {0x4F, 0x2C62},
    {0x53, 0x0094},
    {0x54, 0x2A18},
    {0x55, 0x0081},
    {0x57, 0x1C00},
    {0x58, 0x840D},
    {0x59, 0x0C50},
    {0x5A, 0x0CDD},
    {0x63, 0x3FFF},
};

// After calibration: settle, 0x27 in three steps, then the DTMF and tone
// detector coefficients (none in use).
constexpr Setting AfterCalibration[] = {
    {0x30, 0x4006, 40},
    {0x27, 0x74A4, 5},
    {0x27, 0x7CA4},
    {0x27, 0x6CA4},
    {0x67, 0x0000},
    {0x68, 0x0000},
    {0x69, 0x0000},
    {0x6A, 0x0410},
    {0x6B, 0x0000},
    {0x6C, 0x0000},
    {0x6D, 0x0000},
    {0x6E, 0x0000},
    {0x6F, 0x0000},
    {0x70, 0x0000},
    {0x71, 0x0000},
    {0x72, 0x0000},
    {0x73, 0x0000},
    {0x74, 0x0000},
    {0x75, 0x0000},
    {0x76, 0x0000},
    {0x77, 0x2284},
    {0x78, 0xD984},
    {0x79, 0x1E3C},
};

// Per channel kind (narrow, wide, digital). 0x0A: PA bias pin higher on
// wide channels. Digital channels have a set of their own. 0x13: by
// chip version. 0x32: AGC target. 0x33: AGC. 0x34: receive digital gain.
// 0x3C/0x60: squelch peak and noise detector thresholds. 0x48: squelch
// noise threshold until the squelch level sets it.
template <std::uint16_t Version>
constexpr WidthSetting Width[] = {
    {0x0A, 0x4380, 0x43A0, 0x4381},
    {0x13, Version, Version, Version},
    {0x15, 0x1100, 0x1F00, 0x1100}, // IF filter tuning
    {0x32, 0x4495, 0x75E3, 0x44D5},
    {0x33, 0x4493, 0x4495, 0x44D5},
    {0x34, 0x2B8E, 0x2B89, 0x2398},
    {0x35, 0x51EC, 0x51EC, 0x50A9},
    {0x36, 0x51EC, 0x51EC, 0x50A9},
    {0x3A, 0x40C3, 0x44C3, 0x40C3}, // transmit from the microphone; squelch detectors
    {0x3C, 0x0408, 0x0408, 0x1A32},
    {0x41, 0x070F, 0x070F, 0x4006}, // transmit voice digital gain
    {0x3F, 0x28D0, 0x29D2, 0x28D0}, // squelch RSSI threshold
    {0x48, 0x1D3B, 0x1E3B, 0x203E},
    {0x50, 0x010A, 0x010A, 0x010B},
    {0x56, 0x0B22, 0x0B22, 0x0B02}, // squelch detect time
    {0x60, 0x1CB8, 0x203E, 0x101E},
    {0x62, 0x0A10, 0x2346, 0x0A10},
    {0x65, 0x2494, 0x248A, 0x2494},
    {0x66, 0xEB2E, 0xFF2E, 0xEB2E}, // RSSI compensation
};
constexpr std::uint16_t Version13S = 0xA100;
constexpr std::uint16_t Version13N = 0xB100;

// AGC table, page 1.
constexpr WidthSetting WidthPage1[] = {
    {0x07, 0x020C, 0x0214, 0x020C},
    {0x08, 0x0214, 0x0224, 0x0214},
    {0x09, 0x030C, 0x0314, 0x030C},
    {0x0A, 0x0314, 0x0324, 0x0314},
    {0x0B, 0x0324, 0x0344, 0x0324},
    {0x0C, 0x0344, 0x0384, 0x0344},
    {0x0D, 0x1344, 0x1384, 0x1344},
    {0x0E, 0x1B44, 0x1B84, 0x1B44},
    {0x0F, 0x3F44, 0x3F84, 0x3F44},
    {0x12, 0xE0EB, 0xE0EB, 0xE0EB},
};

// Digital channels below 300 MHz: AGC and receive gain of their own. Wide
// channels from 200 to 300 MHz: AGC target and receive gain one step up.
constexpr RangeSetting Ranges[] = {
    {0, 299'999'999, {0x33, 0x4493, 0x4495, 0x44DD}},
    {0, 299'999'999, {0x34, 0x2B8E, 0x2B89, 0x2392}},
    {200'000'000, 299'999'999, {0x32, 0x4495, 0x75E4, 0x44D5}},
    {200'000'000, 299'999'999, {0x34, 0x2B8E, 0x2B8B, 0x2392}},
};

// Receive: PA bias by width, receive volume step 9 of 15, full DAC gain;
// filters with the transceiver's de-emphasis bypassed (the baseband chip
// does it) and the 250 Hz tone filter. Digital channels bypass the audio
// filters too and take a different audio path setting.
constexpr WidthSetting Receive[] = {
    {0x0A, 0x4380, 0x43A0, 0x4381},
    {0x44, 0x09F9, 0x09F9, 0x09F9},
    {0x58, 0x8085, 0x8085, 0x80E5},
    {0x40, 0x0030, 0x0030, 0x0031},
};

// Transmit: driver step 7 (the drive the power calibration is made at) and
// microphone amplifier gain 14; voice digital gain 15 on every channel
// kind; transmit gain 9 (11 on digital channels), full DAC gain and
// volume; pre-emphasis on, 250 Hz tone filter. Digital channels keep their
// receive filter setting, with pre-emphasis and the audio filters off:
// the baseband's 4FSK must pass unshaped.
constexpr WidthSetting Transmit[] = {
    {0x0A, 0x3BA0, 0x3BA0, 0x3BA1}, // the low bits kept from the receive setting
    {0x41, 0x070F, 0x070F, 0x070F},
    {0x44, 0x09FF, 0x09FF, 0x0BFF},
    {0x58, 0x8405, 0x8405, 0x80E5},
    {0x40, 0x0030, 0x0030, 0x0031},
};

// Band register: two alternative settings for channels where the
// reference's harmonics and the receiver's own mixing products fall, the
// first match wins.
constexpr std::uint16_t BandNormal = 0x8763;
constexpr std::uint16_t BandA = 0x8543;
constexpr std::uint16_t BandB = 0x8643;
constexpr BandWord BandWords[] = {
    // UHF single channels
    {409'500'000, 409'500'000, BandA},
    {420'875'000, 420'875'000, BandA},
    {420'872'500, 420'872'500, BandA},
    {420'877'500, 420'877'500, BandA},
    {432'250'000, 432'250'000, BandA},
    {443'622'500, 443'622'500, BandA},
    {443'625'000, 443'625'000, BandA},
    {443'627'500, 443'627'500, BandA},
    {466'372'500, 466'372'500, BandA},
    {466'375'000, 466'375'000, BandA},
    {466'377'500, 466'377'500, BandA},
    {477'747'500, 477'747'500, BandA},
    {477'750'000, 477'750'000, BandA},
    {477'752'500, 477'752'500, BandA},
    {489'125'000, 489'125'000, BandA},
    {500'500'000, 500'500'000, BandA},
    {511'875'000, 511'875'000, BandA},

    {455'000'000, 455'000'000, BandB},
    {494'000'000, 494'000'000, BandB},
    {520'000'000, 520'000'000, BandB},
    // UHF ranges
    {408'500'000, 408'800'000, BandA},
    {409'100'000, 410'000'000, BandA},
    {410'200'000, 410'400'000, BandA},
    {431'150'000, 432'300'000, BandA},
    {454'700'000, 454'897'500, BandA},
    {455'700'000, 455'900'000, BandA},
    {484'250'000, 484'350'000, BandA},
    {484'900'000, 485'060'000, BandA},
    {485'070'000, 485'200'000, BandA},
    {488'200'000, 488'420'000, BandA},
    {489'080'000, 489'200'000, BandA},

    {400'500'000, 400'700'000, BandB},
    {432'302'500, 433'300'000, BandB},
    {454'900'000, 454'997'500, BandB},
    {455'002'500, 455'300'000, BandB},
    {471'260'000, 471'350'000, BandB},
    {471'980'000, 472'060'000, BandB},
    {472'070'000, 472'200'000, BandB},
    {473'580'000, 473'800'000, BandB},
    {474'300'000, 474'650'000, BandB},
    {475'100'000, 475'500'000, BandB},
    {476'000'000, 480'000'000, BandB},
    {480'040'000, 480'400'000, BandB},
    {481'000'000, 481'150'000, BandB},
    {481'600'000, 482'000'000, BandB},
    // VHF ranges
    {151'602'500, 151'667'500, BandB},
    {151'672'500, 151'787'500, BandB},
    {173'310'000, 173'327'500, BandB},
    {173'337'500, 173'360'000, BandB},
    {173'580'000, 173'597'500, BandB},
    {173'610'000, 173'627'500, BandB},
    // VHF single channels
    {140'287'500, 140'287'500, BandB},
    {140'290'000, 140'290'000, BandB},
    {140'292'500, 140'292'500, BandB},
    {140'293'750, 140'293'750, BandB},
    {140'295'000, 140'295'000, BandB},
    {144'080'000, 144'080'000, BandB},
    {144'081'250, 144'081'250, BandB},
    {147'872'500, 147'872'500, BandB},
    {147'875'000, 147'875'000, BandB},
    {147'877'500, 147'877'500, BandB},
    {151'670'000, 151'670'000, BandB},
    {151'668'750, 151'668'750, BandB},
    {155'455'000, 155'455'000, BandB},
    {155'456'250, 155'456'250, BandB},
    {155'457'500, 155'457'500, BandB},
    {155'460'000, 155'460'000, BandB},
    {155'462'500, 155'462'500, BandB},
    {163'037'500, 163'037'500, BandB},
    {163'042'500, 163'042'500, BandB},
    {163'043'750, 163'043'750, BandB},
    {163'045'000, 163'045'000, BandB},
    {163'040'000, 163'040'000, BandB},
    {166'830'000, 166'830'000, BandB},
    {166'831'250, 166'831'250, BandB},
    {170'622'500, 170'622'500, BandB},
    {170'625'000, 170'625'000, BandB},
    {170'627'500, 170'627'500, BandB},

    {136'500'000, 136'500'000, BandA},
    {159'250'000, 159'250'000, BandA},

    {136'205'000, 136'225'000, BandA},
    {136'232'500, 136'247'500, BandA},
    {136'420'000, 136'497'500, BandA},
    {136'502'500, 136'590'000, BandA},
    {136'755'000, 136'767'500, BandA},
    {136'777'500, 136'787'500, BandA},
    {143'980'000, 144'077'500, BandA},
    {144'082'500, 144'180'000, BandA},
    {144'330'000, 144'350'000, BandA},
    {144'360'000, 144'372'500, BandA},
    {158'172'500, 158'197'500, BandA},
    {158'417'500, 158'430'000, BandA},
    {158'447'500, 158'457'500, BandA},
    {158'935'000, 159'515'000, BandA},
    {159'522'500, 159'565'000, BandA},
    {160'340'000, 160'357'500, BandA},
    {166'567'500, 166'590'000, BandA},
    {166'777'500, 166'827'500, BandA},
    {166'832'500, 166'910'000, BandA},
    {167'082'500, 167'100'000, BandA},
    {167'110'000, 167'125'000, BandA},
};

constexpr Tuning TuningS{
    .startUp = StartUpS,
    .afterCalibration = AfterCalibration,
    .width = Width<Version13S>,
    .widthPage1 = WidthPage1,
    .ranges = Ranges,
    .receive = Receive,
    .transmit = Transmit,
    .bandWords = BandWords,
    .normalBand = BandNormal,
};
constexpr Tuning TuningN{
    .startUp = StartUpN,
    .afterCalibration = AfterCalibration,
    .width = Width<Version13N>,
    .widthPage1 = WidthPage1,
    .ranges = Ranges,
    .receive = Receive,
    .transmit = Transmit,
    .bandWords = BandWords,
    .normalBand = BandNormal,
};

bool isVersionN(dmm::services::Storage &storage)
{
    for (std::uint32_t at : VersionAt) {
        std::array<std::uint8_t, VersionN.size()> text{};
        if (!dmm::services::readable(storage.read(at, text))) {
            continue;
        }
        return std::equal(text.begin(), text.end(), VersionN.begin(),
            [](std::uint8_t byte, char c) { return byte == static_cast<std::uint8_t>(c); });
    }
    return false;
}

} // namespace

const Tuning &fmTuning(dmm::services::Storage *storage)
{
    const bool n = storage != nullptr && isVersionN(*storage);
    dmm::services::log::info("fm") << "transceiver tuning for the " << (n ? "N" : "S") << " version";
    return n ? TuningN : TuningS;
}

} // namespace board
