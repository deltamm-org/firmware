// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/sct3258_baseband.h"

#include "dmm/lib/format.h"
#include "dmm/lib/strings.h"
#include "dmm/os/os.h"
#include "dmm/services/log.h"

namespace dmm::services {

namespace {

using drivers::baseband::Sct3258;
namespace log = services::log;

constexpr std::uint8_t VocoderAmbePlus2 = Sct3258::VocoderAmbePlus2;
constexpr std::uint8_t VocoderAnalog = Sct3258::VocoderAnalog;
constexpr std::uint8_t ProcessAnalog = Sct3258::ProcessAnalog;

// DMR call options: the chip does squelch detection, direct mode without a
// reverse channel (repeaters work with it too), no voice delay; one good
// access channel report before the slot counts as verified, three repeated
// LC headers; every CSBK and idle burst reported.
constexpr std::uint8_t CallOptions[] = {Sct3258::DmrCallOption, 0x41, 0x00, 0x13, 0x0C, 0x00};
// How long the chip may take to switch between receive and transmit.
constexpr unsigned SwitchMs = 500;
// The first frame after the switch to transmit: how often it is handed
// over and how long each wait for the chip to take it.
constexpr int FirstFrameTries = 4;
constexpr unsigned FirstFrameMs = 250;
// Repeater wake-up bursts per try.
constexpr int WakeBursts = 5;
// DMR_CALL_SLOT: receive on both slots (direct channels); 1 or 2 is that
// slot alone.
constexpr std::uint8_t BothSlots = 3;
// CARRIER_READY: a carrier is there, with the demodulated signal as is or
// reversed; with the chip's own squelch detection this starts it.
constexpr std::uint8_t CarrierReady = 1;
constexpr std::uint8_t CarrierReadyReversed = 2;
// NEW_EVENT codes.
constexpr std::uint8_t EventSyncLost = 8;
constexpr std::uint8_t EventCarrierFound = 12;
constexpr std::uint8_t EventCarrierLost = 13;
// DMR_SLOT_FOUND bits.
constexpr std::uint8_t SlotFoundBaseStation = 0x80;
// The packet interface's example reports "verified, no inversion" as 0xA9,
// which puts the verified flag in bit 5 and the inversion flag in bit 6.
constexpr std::uint8_t SlotFoundVerified = 0x20;
constexpr std::uint8_t SlotFoundInverted = 0x40;
constexpr std::uint8_t SlotFoundInboundBusy = 0x08;
constexpr std::uint8_t SlotFoundChannel2 = 0x04;
// DIGC_DATA_FRAME property: voice burst, else data of the low four bits'
// type.
constexpr std::uint8_t FrameVoice = 0x10;
constexpr std::uint8_t FrameData = 0x00;
constexpr std::uint8_t FrameTypeMask = 0x0F;

// Analog mode byte: DCS high-pass on and 1.5 dB less at 3 kHz; the chip's
// own tone generator, the compander and the emphasis skip stay off. Wide
// channels add the 3 kHz audio low-pass (else 2.55 kHz).
constexpr std::uint8_t AnalogNarrow = 0xC0;
constexpr std::uint8_t AnalogWide = AnalogNarrow | 0x02;

// Reply times from the packet interface document, with margin.
constexpr unsigned CodecSelectMs = 1500;
constexpr unsigned VocoderSelectMs = 3500;

// Log lines are built in their own frames, kept out of the callers that go
// on to talk to the chip.
[[gnu::noinline, gnu::cold]] void warn(const char *what)
{
    log::warning("baseband") << what;
}

[[gnu::noinline, gnu::cold]] void bootFailed(const char *problem, int code)
{
    log::error("baseband") << problem << " (code " << code << ")";
}

[[gnu::noinline, gnu::cold]] void warn(const char *what, int code)
{
    log::warning("baseband") << what << " answered " << code;
}

[[gnu::noinline, gnu::cold]] void logSpeakerGain(const std::array<std::uint8_t, 2> &gain)
{
    log::info("baseband") << "digital speaker gain " << lib::hex(gain[0], 2) << lib::hex(gain[1], 2);
}

// One received packet as hex, 24 bytes per line.
[[gnu::noinline]] void logReply(const drivers::baseband::Packet &p)
{
    log::info("baseband") << "reply type " << lib::hex(p.type, 2) << ", " << p.length << " bytes";
    for (std::size_t at = 0; at < p.length; at += 24) {
        lib::StaticString<24 * 3 + 2> line("  ");
        for (std::size_t i = at; i < p.length && i < at + 24; ++i) {
            line << lib::hex(p.fields[i], 2) << ' ';
        }
        log::info("baseband") << line.view();
    }
}

const char *bootProblem(Sct3258::BootResult r)
{
    switch (r) {
    case Sct3258::BootResult::Ready:
        return nullptr;
    case Sct3258::BootResult::NoFirmware:
        return "booted, but reports no firmware";
    case Sct3258::BootResult::NoAnswer:
        return "no answer after the boot program";
    case Sct3258::BootResult::WrongAnswer:
        return "unexpected answer after the boot program";
    case Sct3258::BootResult::BadBlob:
        return "boot program has the wrong size";
    }
    return "?";
}

// Sends the board's SQ_LEVEL, if it gives one.
bool sendSquelch(Sct3258 &chip, std::span<const std::uint8_t> level)
{
    if (level.empty() || level.size() > 12) {
        return level.empty();
    }
    std::uint8_t field[1 + 12] = {Sct3258::SquelchLevel};
    for (std::size_t i = 0; i < level.size(); ++i) {
        field[1 + i] = level[i];
    }
    return chip.control(std::span(field, 1 + level.size())) == 0;
}

} // namespace

Sct3258Baseband::Sct3258Baseband(Sct3258 &chip, BasebandBoard &board, std::span<const std::uint8_t> bootBlob,
    const Audio &audio, TrimsSource trims, DigitalTrimsSource digitalTrims)
    : m_chip(chip), m_board(board), m_blob(bootBlob), m_audio(audio), m_trims(trims), m_digitalTrims(digitalTrims)
{
}

bool Sct3258Baseband::fail(const char *problem)
{
    m_info.running = false;
    m_info.problem = problem;
    return false;
}

bool Sct3258Baseband::start()
{
    m_info = {};
    if (m_blob.empty()) {
        return fail("this build has no boot program (configure with DMM_STOCK_FIRMWARE)");
    }
    m_board.powerUpAndReset();
    std::uint8_t ack = 0xFF;
    const Sct3258::BootResult boot = m_chip.boot(m_blob, ack);
    if (boot != Sct3258::BootResult::Ready) {
        bootFailed(bootProblem(boot), ack);
        return fail(bootProblem(boot));
    }
    m_chip.queryText(Sct3258::QueryProduct, m_info.product, sizeof m_info.product);
    m_chip.queryText(Sct3258::QueryHardware, m_info.hardware, sizeof m_info.hardware);
    m_chip.queryText(Sct3258::QuerySoftware, m_info.software, sizeof m_info.software);

    const std::uint8_t codec[] = {Sct3258::CodecSelect, m_audio.codec};
    const int codecStatus = m_chip.control(codec, CodecSelectMs);
    if (codecStatus != 0) {
        warn("codec selection", codecStatus);
    }
    // The vocoder is loaded once, then the chip is put in analog mode.
    for (std::uint8_t vocoder : {VocoderAmbePlus2, VocoderAnalog}) {
        const std::uint8_t select[] = {Sct3258::VocoderSelect, vocoder};
        const int status = m_chip.control(select, VocoderSelectMs);
        if (status != 0) {
            warn(vocoder == VocoderAnalog ? "analog vocoder selection" : "vocoder selection", status);
        }
    }
    m_vocoder = VocoderAnalog;
    m_callOptionsSet = false;
    m_info.running = true;
    m_trimsWritten = false;
    if (!write(m_audio.receive)) {
        warn("codec did not take the audio set-up");
    }
    if (const std::optional<Trims> trims = m_trims != nullptr ? m_trims() : std::nullopt) {
        writeTrims(*trims);
    }
    return true;
}

BasebandInfo Sct3258Baseband::info()
{
    BasebandInfo i = m_info;
    i.dataWaiting = m_chip.dataWaiting();
    return i;
}

std::uint8_t Sct3258Baseband::volumeRegister(int step) const
{
    if (step <= 0) {
        return m_audio.speakerMuted;
    }
    if (step > SpeakerVolumeSteps) {
        step = SpeakerVolumeSteps;
    }
    return m_audio.speakerSteps[static_cast<std::size_t>(step - 1)];
}

bool Sct3258Baseband::setSpeakerVolume(int step)
{
    if (!m_info.running) {
        return false;
    }
    return m_chip.codecWrite(m_audio.speakerPage, m_audio.speakerRegister, volumeRegister(step));
}

bool Sct3258Baseband::playTone(std::uint16_t hz, std::uint16_t ms, int percent)
{
    // The tone amplitude: 0..64 for 0..1 of full scale.
    const auto amplitude = static_cast<std::uint8_t>((percent < 0 ? 0 : percent > 100 ? 100
                                                                                      : percent) *
        64 / 100);
    if (!m_info.running) {
        return false;
    }
    // One repetition of tone 1 to the local line out, tone 2 silent.
    const std::uint8_t tone[] = {Sct3258::AnalogTone, 0x01, static_cast<std::uint8_t>(hz >> 8), static_cast<std::uint8_t>(hz), 0,
        0, 0, 0, 0, 0, amplitude, 0, 0, 0, static_cast<std::uint8_t>(ms >> 8), static_cast<std::uint8_t>(ms), 0, 1};
    return m_chip.control(tone) == 0;
}

bool Sct3258Baseband::setAnalog(bool wide)
{
    if (!m_info.running) {
        return false;
    }
    const std::optional<Trims> trims = m_trims != nullptr ? m_trims() : std::nullopt;
    if (!trims) {
        return false;
    }
    if (!m_trimsWritten) {
        writeTrims(*trims);
    }
    const bool fromDigital = m_vocoder == Sct3258::VocoderDigital;
    if (!selectVocoder(VocoderAnalog)) {
        return false;
    }
    if (fromDigital && !write(m_audio.receive)) {
        warn("codec did not take the receive levels");
    }
    if (!sendSquelch(m_chip, m_audio.analogSquelch)) {
        warn("analog squelch levels not taken");
    }
    const std::uint8_t process[] = {Sct3258::ProcessMode, ProcessAnalog};
    std::uint8_t config[2 + 10] = {Sct3258::AnalogConfig, wide ? AnalogWide : AnalogNarrow};
    for (std::size_t i = 0; i < trims->analogGains.size(); ++i) {
        config[2 + i] = trims->analogGains[i];
    }
    return m_chip.control(process) == 0 && m_chip.control(config) == 0;
}

bool Sct3258Baseband::selectVocoder(std::uint8_t vocoder)
{
    if (m_vocoder == vocoder) {
        return true;
    }
    const std::uint8_t select[] = {Sct3258::VocoderSelect, vocoder};
    const int status = m_chip.control(select, VocoderSelectMs);
    if (status != 0) {
        warn("vocoder selection", status);
        return false;
    }
    m_vocoder = vocoder;
    return true;
}

bool Sct3258Baseband::setDigital(const dmr::ChannelSettings &settings, std::uint32_t receiveHz, bool repeater)
{
    const bool fromAnalog = m_vocoder != Sct3258::VocoderDigital;
    if (!m_info.running || !selectVocoder(Sct3258::VocoderDigital)) {
        return false;
    }
    if (fromAnalog && !write(m_audio.digitalReceive)) {
        warn("codec did not take the digital receive levels");
    }
    if (const auto trims = m_digitalTrims != nullptr ? m_digitalTrims(receiveHz) : std::nullopt) {
        std::uint8_t offset[1 + 12] = {Sct3258::DcOffset};
        for (std::size_t i = 0; i < trims->dcOffset.size(); ++i) {
            offset[1 + i] = trims->dcOffset[i];
        }
        const std::uint8_t gain[] = {Sct3258::DemodGain, trims->demodGain[0], trims->demodGain[1]};
        if (m_chip.control(offset) != 0 || m_chip.control(gain) != 0) {
            warn("digital calibration not taken");
        }
        if (trims->speakerGain) {
            const std::uint8_t speaker[] = {Sct3258::DigitalSpeakerGain, (*trims->speakerGain)[0], (*trims->speakerGain)[1]};
            if (m_chip.control(speaker) != 0) {
                warn("digital speaker gain not taken");
            } else if (fromAnalog) {
                logSpeakerGain(*trims->speakerGain);
            }
        }
    }
    if (!sendSquelch(m_chip, m_audio.digitalSquelch)) {
        warn("digital squelch levels not taken");
    }
    const std::uint8_t process[] = {Sct3258::ProcessMode, Sct3258::ProcessLayer2};
    if (m_chip.control(process) != 0) {
        return false;
    }
    if (!m_callOptionsSet) {
        // Every report the chip can give, also the burst sync events, and a
        // sync miss count of 1.
        const std::uint8_t report[] = {Sct3258::ReportField, 0xFF};
        const std::uint8_t syncMiss[] = {Sct3258::SyncMissCount, 1};
        if (m_chip.control(report) != 0 || m_chip.control(syncMiss) != 0) {
            warn("DMR report settings not taken");
        }
        const int status = m_chip.control(Sct3258::TypeDmr, CallOptions, CodecSelectMs);
        if (status != 0) {
            warn("DMR call options", status);
            return false;
        }
        m_callOptionsSet = true;
    }
    const std::uint8_t cc[] = {Sct3258::DmrColourCode, static_cast<std::uint8_t>(settings.colourCode & 0x0F)};
    m_repeater = repeater;
    // Through a repeater only the channel's slot is received; the other
    // slot's calls would reach the vocoder too. Direct: both slots.
    const std::uint8_t slot[] = {Sct3258::DmrCallSlot, repeater ? static_cast<std::uint8_t>(settings.slot == 2 ? 2 : 1) : BothSlots};
    const std::uint8_t work[] = {Sct3258::WorkMode, Sct3258::WorkReceive, 0, 0};
    const std::uint8_t carrier[] = {
        Sct3258::CarrierReady, m_audio.demodulatorInverted ? CarrierReadyReversed : CarrierReady};
    return m_chip.control(Sct3258::TypeDmr, cc) == 0 && m_chip.control(Sct3258::TypeDmr, slot) == 0 &&
        m_chip.control(work) == 0 && m_chip.control(carrier) == 0;
}

bool Sct3258Baseband::toEvent(const drivers::baseband::Packet &p, dmr::Event &e) const
{
    using Kind = dmr::Event::Kind;
    // Anything not decoded below goes out raw, for diagnostics.
    const auto other = [&p, &e] {
        e.kind = Kind::Other;
        e.raw = p.type;
        e.length = static_cast<std::uint8_t>(p.length < dmr::Event::MaxData ? p.length : dmr::Event::MaxData);
        for (std::size_t i = 0; i < e.length; ++i) {
            e.data[i] = p.fields[i];
        }
        return true;
    };
    if (p.length >= 2 && p.fields[0] == Sct3258::NewEvent) {
        switch (p.fields[1]) {
        case EventSyncLost:
            e.kind = Kind::SyncLost;
            return true;
        case EventCarrierFound:
            e.kind = Kind::CarrierFound;
            return true;
        case EventCarrierLost:
            e.kind = Kind::CarrierLost;
            return true;
        default:
            return other();
        }
    }
    if (p.length >= 2 && p.fields[0] == Sct3258::AckMessage) {
        e.kind = Kind::AllSent;
        return p.fields[1] == Sct3258::AckAllSent;
    }
    if (p.length >= 2 && p.fields[0] == Sct3258::DmrSlotFound) {
        const std::uint8_t b = p.fields[1];
        e.kind = Kind::SlotFound;
        e.baseStation = (b & SlotFoundBaseStation) != 0;
        e.verified = (b & SlotFoundVerified) != 0;
        e.inverted = (b & SlotFoundInverted) != 0;
        e.inboundBusy = (b & SlotFoundInboundBusy) != 0;
        e.nextSlot = (b & SlotFoundChannel2) != 0 ? 2 : 1;
        return true;
    }
    // A received burst: the colour code, then the frame.
    std::size_t at = 0;
    e.colourCode = 0;
    if (p.length >= 2 && p.fields[0] == Sct3258::DmrColourCode) {
        e.colourCode = p.fields[1] & 0x0F;
        at = 2;
    }
    if (at + 3 > p.length || p.fields[at] != Sct3258::DataFrame) {
        return other();
    }
    const std::uint8_t property = p.fields[at + 1];
    const std::size_t length = p.fields[at + 2];
    const std::size_t have = p.length - (at + 3) < length ? p.length - (at + 3) : length;
    e.kind = Kind::Burst;
    e.raw = property;
    e.voice = (property & FrameVoice) != 0;
    e.type = static_cast<dmr::DataType>(property & FrameTypeMask);
    e.length = static_cast<std::uint8_t>(have < dmr::Event::MaxData ? have : dmr::Event::MaxData);
    for (std::size_t i = 0; i < e.length; ++i) {
        e.data[i] = p.fields[at + 3 + i];
    }
    return true;
}

bool Sct3258Baseband::startDigitalCall(const dmr::ChannelSettings &settings, const dmr::FullLc &lc,
    std::uint32_t transmitHz)
{
    if (!m_info.running) {
        return false;
    }
    if (const auto trims = m_digitalTrims != nullptr ? m_digitalTrims(transmitHz) : std::nullopt) {
        const std::uint8_t gain[] = {Sct3258::ModGain, trims->modGain[0], trims->modGain[1]};
        if (m_chip.control(gain) != 0) {
            warn("modulator gain not taken");
        }
        if (trims->micGain) {
            const std::uint8_t mic[] = {Sct3258::DigitalMicGain, (*trims->micGain)[0], (*trims->micGain)[1]};
            if (m_chip.control(mic) != 0) {
                warn("digital microphone gain not taken");
            }
        }
        if (trims->slotTiming) {
            std::uint8_t timing[1 + 8] = {Sct3258::DmrOffset};
            for (std::size_t i = 0; i < 8; ++i) {
                timing[1 + i] = (*trims->slotTiming)[i];
            }
            if (m_chip.control(Sct3258::TypeDmr, timing) != 0) {
                warn("slot timing not taken");
            }
        }
    }
    // Through a repeater the call's slots follow the repeater's timing.
    const std::uint8_t slot[] = {Sct3258::DmrCallSlot,
        static_cast<std::uint8_t>(Sct3258::SlotTransmit | (m_repeater ? Sct3258::SlotFollowReceive : 0) | settings.slot)};
    if (!leaveReceive() || m_chip.control(Sct3258::TypeDmr, slot, SwitchMs) != 0 || !write(m_audio.transmit) ||
        !enterTransmit()) {
        warn("digital transmit set-up not taken");
        return false;
    }
    std::array<std::uint8_t, dmr::FullLc::Bytes> b{};
    lc.encode(b);
    // The header, then the first voice superframe with the same link
    // control; the chip sends voice from then on.
    return sendFirstFrame(settings.colourCode, FrameData | static_cast<std::uint8_t>(dmr::DataType::VoiceLcHeader), b) &&
        m_chip.sendFrame(settings.colourCode, FrameVoice | static_cast<std::uint8_t>(dmr::DataType::VoiceLcHeader), b);
}

bool Sct3258Baseband::endDigitalCall(const dmr::ChannelSettings &settings, const dmr::FullLc &lc)
{
    std::array<std::uint8_t, dmr::FullLc::Bytes> b{};
    lc.encode(b);
    const bool sent =
        m_info.running && m_chip.sendFrame(settings.colourCode, static_cast<std::uint8_t>(dmr::DataType::TerminatorWithLc), b);
    write(receiveWrites());
    return sent;
}

bool Sct3258Baseband::wakeRepeater(const dmr::ChannelSettings &settings, std::uint32_t source)
{
    if (!m_info.running) {
        return false;
    }
    const std::uint8_t slot[] = {Sct3258::DmrCallSlot, static_cast<std::uint8_t>(Sct3258::SlotTransmit | settings.slot)};
    const auto wake = dmr::repeaterWake(source);
    if (!leaveReceive() || m_chip.control(Sct3258::TypeDmr, slot, SwitchMs) != 0 || !enterTransmit()) {
        return false;
    }
    // Several in a row when asked for: a repeater that is just starting
    // its receiver may miss the first. Each waits
    // until the one before has gone out; the caller awaits the last.
    for (int i = 0; i < WakeBursts; ++i) {
        if (!(i == 0 ? sendFirstFrame(settings.colourCode, static_cast<std::uint8_t>(dmr::DataType::Csbk), wake)
                     : m_chip.sendFrame(settings.colourCode, static_cast<std::uint8_t>(dmr::DataType::Csbk), wake))) {
            warn("wake-up block not taken, reply", m_chip.lastFrameReply());
            return i > 0;
        }
        if (i + 1 < WakeBursts && !m_chip.awaitSent()) {
            warn("wake-up block did not go out");
            return true;
        }
    }
    return true;
}

bool Sct3258Baseband::sendFirstFrame(std::uint8_t colourCode, std::uint8_t property, std::span<const std::uint8_t> data)
{
    // Just after the switch to transmit the chip may let a frame pass
    // without a word, the first time after start-up for one; it is handed
    // over again until taken.
    // A chip that let it pass is put back to receive and into transmit
    // again first: it takes no frame until then.
    for (int i = 0; i < FirstFrameTries; ++i) {
        if (i > 0 && (!setWorkMode(BasebandWorkMode::Receive) || !enterTransmit())) {
            return false;
        }
        if (m_chip.sendFrame(colourCode, property, data, FirstFrameMs)) {
            if (i > 0) {
                log::info("baseband") << "frame taken on try " << i + 1;
            }
            return true;
        }
    }
    return false;
}

bool Sct3258Baseband::leaveReceive()
{
    // No carrier any more: the chip is told before it transmits.
    const std::uint8_t carrier[] = {Sct3258::CarrierReady, 0};
    if (m_chip.control(carrier, SwitchMs) != 0) {
        warn("carrier off not taken");
        return false;
    }
    return true;
}

bool Sct3258Baseband::enterTransmit()
{
    const std::uint8_t work[] = {Sct3258::WorkMode, Sct3258::WorkTransmit, 0, 0};
    const int status = m_chip.control(work, SwitchMs);
    if (status != 0) {
        warn("transmit mode", status);
        return false;
    }
    return true;
}

bool Sct3258Baseband::nextEvent(dmr::Event &event)
{
    drivers::baseband::Packet report;
    while (m_chip.nextReport(report)) {
        if (toEvent(report, event)) {
            return true;
        }
    }
    return false;
}

bool Sct3258Baseband::setWorkMode(BasebandWorkMode mode)
{
    const std::uint8_t code = mode == BasebandWorkMode::Receive ? Sct3258::WorkReceive : mode == BasebandWorkMode::Transmit ? Sct3258::WorkTransmit
                                                                                                                            : Sct3258::WorkIdle;
    const std::uint8_t work[] = {Sct3258::WorkMode, code, 0, 0};
    return m_info.running && m_chip.control(work) == 0;
}

bool Sct3258Baseband::setCarrier(bool present)
{
    const std::uint8_t carrier[] = {Sct3258::CarrierReady, static_cast<std::uint8_t>(present ? 1 : 0)};
    return m_info.running && m_chip.control(carrier) == 0;
}

bool Sct3258Baseband::setMicrophone(bool on)
{
    if (!m_info.running) {
        return false;
    }
    return write(on ? m_audio.transmit : receiveWrites());
}

std::span<const Sct3258Baseband::CodecWrite> Sct3258Baseband::receiveWrites() const
{
    return m_vocoder == Sct3258::VocoderDigital ? m_audio.digitalReceive : m_audio.receive;
}

bool Sct3258Baseband::write(std::span<const CodecWrite> writes)
{
    for (const CodecWrite &w : writes) {
        if (!m_chip.codecWrite(w.page, w.reg, w.value)) {
            return false;
        }
    }
    return true;
}

void Sct3258Baseband::writeTrims(const Trims &trims)
{
    for (std::size_t i = 0; i < trims.codec.size(); ++i) {
        const CodecRegister &at = m_audio.trims[i];
        if (!m_chip.codecWrite(at.page, at.reg, trims.codec[i])) {
            warn("codec trims not applied");
            break;
        }
    }
    m_trimsWritten = true;
}

bool Sct3258Baseband::codecWrite(std::uint8_t page, std::uint8_t reg, std::uint8_t value)
{
    return m_info.running && m_chip.codecWrite(page, reg, value);
}

int Sct3258Baseband::exchange(std::uint8_t type, std::span<const std::uint8_t> fields, unsigned waitMs)
{
    m_chip.send(type, fields);
    drivers::baseband::Packet p;
    int replies = 0;
    for (unsigned waited = 0; waited < waitMs && replies < 4; ++waited) {
        if (m_chip.receive(p, 1)) {
            ++replies;
            logReply(p);
        }
    }
    return replies;
}

bool Sct3258Baseband::debugCommand(std::string_view command)
{
    std::string_view rest = command;
    const std::string_view verb = lib::takeWord(rest);
    if (verb == "codec") {
        std::int32_t n[3] = {};
        for (std::int32_t &v : n) {
            const auto value = lib::parseNumber(lib::takeWord(rest), 16);
            if (!value || *value > 0xFF) {
                return false;
            }
            v = *value;
        }
        if (!rest.empty() || n[1] > 0x7F) {
            return false;
        }
        const bool ok = codecWrite(static_cast<std::uint8_t>(n[0]), static_cast<std::uint8_t>(n[1]), static_cast<std::uint8_t>(n[2]));
        log::info("baseband") << "codec page " << n[0] << " register " << lib::hex(static_cast<std::uint32_t>(n[1]), 2) << " = "
                              << lib::hex(static_cast<std::uint32_t>(n[2]), 2) << ": " << (ok ? "accepted" : "refused");
        return true;
    }
    if (verb == "txtest") {
        // The switch to transmit and one wake-up block after @p ms, with
        // the RF path left off: nothing goes on the air.
        const auto ms = lib::parseNumber(lib::takeWord(rest), 10);
        if (!ms || *ms < 0 || *ms > 5000 || !rest.empty()) {
            return false;
        }
        const std::uint8_t slot[] = {Sct3258::DmrCallSlot, static_cast<std::uint8_t>(Sct3258::SlotTransmit | 1)};
        const bool ready = leaveReceive() && m_chip.control(Sct3258::TypeDmr, slot, SwitchMs) == 0 && enterTransmit();
        os::sleepMs(static_cast<os::Ticks>(*ms));
        const os::Ticks start = os::nowMs();
        const bool taken = ready && m_chip.sendFrame(1, static_cast<std::uint8_t>(dmr::DataType::Csbk), dmr::repeaterWake(1), 1000);
        const os::Ticks took = os::nowMs() - start;
        const bool sent = taken && m_chip.awaitSent(500);
        setWorkMode(BasebandWorkMode::Receive);
        log::info("baseband") << "txtest after " << *ms << " ms: " << (!ready ? "switch refused" : !taken ? "block not taken"
                                                                                                          : "taken")
                              << " in " << took << " ms" << (taken ? (sent ? ", sent" : ", not sent") : "");
        return true;
    }
    if (verb == "packet") {
        const auto type = lib::parseNumber(lib::takeWord(rest), 16);
        if (!type || *type > 0xFF) {
            return false;
        }
        std::uint8_t fields[16];
        std::size_t count = 0;
        while (!rest.empty()) {
            const auto value = lib::parseNumber(lib::takeWord(rest), 16);
            if (!value || *value > 0xFF || count == sizeof fields) {
                return false;
            }
            fields[count++] = static_cast<std::uint8_t>(*value);
        }
        log::info("baseband") << "sending type " << lib::hex(static_cast<std::uint32_t>(*type), 2) << ", " << count
                              << " field bytes";
        if (exchange(static_cast<std::uint8_t>(*type), {fields, count}, 1000) == 0) {
            log::info("baseband") << "no reply within 1 s";
        }
        return true;
    }
    return false;
}

const char *Sct3258Baseband::debugHelp() const
{
    return "bb codec <page> <reg> <value>, bb packet <type> <bytes> (hex), bb txtest <ms> (no RF)";
}

static_assert(Baseband<Sct3258Baseband>);

} // namespace dmm::services
