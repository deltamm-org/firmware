// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/services/sct3258_baseband.h"

#include "dmm/lib/format.h"
#include "dmm/lib/strings.h"
#include "dmm/services/log.h"

namespace dmm::services {

namespace {

using drivers::baseband::Sct3258;
namespace log = services::log;

constexpr std::uint8_t VocoderAmbePlus2 = 0x04;
constexpr std::uint8_t VocoderAnalog = 0x10;
constexpr std::uint8_t ProcessAnalog = 0x80;

// Analog mode byte: DCS high-pass on and 1.5 dB less at 3 kHz; the chip's
// own tone generator, the compander and the emphasis skip stay off. Wide
// channels add the 3 kHz audio low-pass (else 2.55 kHz).
constexpr std::uint8_t AnalogNarrow = 0xC0;
constexpr std::uint8_t AnalogWide = AnalogNarrow | 0x02;

// Reply times from the packet interface document, with margin.
constexpr unsigned CodecSelectMs = 1500;
constexpr unsigned VocoderSelectMs = 3500;

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

} // namespace

Sct3258Baseband::Sct3258Baseband(Sct3258 &chip, BasebandBoard &board, std::span<const std::uint8_t> bootBlob,
    const Audio &audio, TrimsSource trims)
    : m_chip(chip), m_board(board), m_blob(bootBlob), m_audio(audio), m_trims(trims)
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
        log::error("baseband") << bootProblem(boot) << " (code " << ack << ")";
        return fail(bootProblem(boot));
    }
    m_chip.queryText(Sct3258::QueryProduct, m_info.product, sizeof m_info.product);
    m_chip.queryText(Sct3258::QueryHardware, m_info.hardware, sizeof m_info.hardware);
    m_chip.queryText(Sct3258::QuerySoftware, m_info.software, sizeof m_info.software);

    const std::uint8_t codec[] = {Sct3258::CodecSelect, m_audio.codec};
    const int codecStatus = m_chip.control(codec, CodecSelectMs);
    if (codecStatus != 0) {
        log::warning("baseband") << "codec selection answered " << codecStatus;
    }
    // The vocoder is loaded once, then the chip is put in analog mode.
    for (std::uint8_t vocoder : {VocoderAmbePlus2, VocoderAnalog}) {
        const std::uint8_t select[] = {Sct3258::VocoderSelect, vocoder};
        const int status = m_chip.control(select, VocoderSelectMs);
        if (status != 0) {
            log::warning("baseband") << "vocoder " << vocoder << " selection answered " << status;
        }
    }
    m_info.running = true;
    m_trimsWritten = false;
    if (!write(m_audio.receive)) {
        log::warning("baseband") << "codec did not take the audio set-up";
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
    if (step > VolumeSteps) {
        step = VolumeSteps;
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
        for (std::size_t i = 0; i < trims->codec.size(); ++i) {
            const CodecWrite &at = m_audio.trims[i];
            if (!m_chip.codecWrite(at.page, at.reg, trims->codec[i])) {
                log::warning("baseband") << "codec trims not applied";
                break;
            }
        }
        m_trimsWritten = true;
    }
    const std::uint8_t process[] = {Sct3258::ProcessMode, ProcessAnalog};
    std::uint8_t config[2 + 10] = {Sct3258::AnalogConfig, wide ? AnalogWide : AnalogNarrow};
    for (std::size_t i = 0; i < trims->analogGains.size(); ++i) {
        config[2 + i] = trims->analogGains[i];
    }
    return m_chip.control(process) == 0 && m_chip.control(config) == 0;
}

bool Sct3258Baseband::setWorkMode(WorkMode mode)
{
    const std::uint8_t code = mode == WorkMode::Receive ? Sct3258::WorkReceive : mode == WorkMode::Transmit ? Sct3258::WorkTransmit
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
    return write(on ? m_audio.transmit : m_audio.receive);
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
            log::info("baseband") << "reply type " << lib::hex(p.type, 2) << ", " << p.length << " bytes";
            // 24 bytes per line.
            for (std::size_t at = 0; at < p.length; at += 24) {
                lib::StaticString<24 * 3 + 2> line("  ");
                for (std::size_t i = at; i < p.length && i < at + 24; ++i) {
                    line << lib::hex(p.fields[i], 2) << ' ';
                }
                log::info("baseband") << line.view();
            }
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
    return "bb codec <page> <reg> <value>, bb packet <type> <bytes> (hex)";
}

} // namespace dmm::services
