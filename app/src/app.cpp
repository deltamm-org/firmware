// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tommi Tauriainen

#include "dmm/app/app.h"

#include "audio.h"
#include "input.h"
#include "radio.h"
#include "system.h"
#include "ui.h"

#include "dmm/lib/format.h"
#include "dmm/lib/strings.h"
#include "dmm/os/os.h"
#include "dmm/services/data_link.h"
#include "dmm/services/log.h"
#include "dmm/version.h"

#include <string_view>

namespace dmm::app {

namespace {

namespace log = services::log;

Platform *platform = nullptr;

os::Task<512> linkTask;

constexpr os::Ticks LinkStepMs = 20;

// Sends what is queued, then gives USB time to carry it out, before the
// console does something that ends this run.
void flushLog()
{
    if (platform->logSink.ready()) {
        log::drain(platform->logSink);
    }
    os::sleepMs(100);
}

// A number at the end of @p command after @p prefix, or -1.
int numberAfter(std::string_view command, std::string_view prefix)
{
    if (!lib::startsWith(command, prefix) || command.size() == prefix.size() || command.size() > prefix.size() + 4) {
        return -1;
    }
    int value = 0;
    command.remove_prefix(prefix.size());
    for (char c : command) {
        if (c < '0' || c > '9') {
            return -1;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

// A hexadecimal number after @p prefix, or -1.
std::int64_t hexAfter(std::string_view command, std::string_view prefix)
{
    if (!lib::startsWith(command, prefix) || command.size() == prefix.size() || command.size() > prefix.size() + 8) {
        return -1;
    }
    command.remove_prefix(prefix.size());
    std::int64_t value = 0;
    for (char c : command) {
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F'                                              ? c - 'A' + 10
                                                                                : -1;
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

// Up to @p count numbers separated by single spaces after @p prefix, in
// @p base (10 or 16). False unless exactly @p count are there.
// The key lock settings the console last sent; the ui starts with the
// same defaults.
services::KeyLockSettings lockSettings;

// "lock set <item> on|off": changes one item of lockSettings; false when
// the command is not one of these.
bool lockSetting(std::string_view command)
{
    struct Item
    {
        std::string_view name;
        bool services::KeyLockSettings::*field;
    };
    constexpr Item Items[] = {{"knob", &services::KeyLockSettings::knob}, {"keypad", &services::KeyLockSettings::keypad},
        {"side", &services::KeyLockSettings::side}, {"forced", &services::KeyLockSettings::forced}};
    constexpr std::string_view Prefix = "lock set ";
    if (!lib::startsWith(command, Prefix)) {
        return false;
    }
    command.remove_prefix(Prefix.size());
    for (const Item &item : Items) {
        for (const bool on : {true, false}) {
            lib::StaticString<24> text;
            text << item.name << (on ? " on" : " off");
            if (command == text.view()) {
                lockSettings.*item.field = on;
                return true;
            }
        }
    }
    return false;
}

// "fm power low|mid|high|turbo" as 0..3, else -1.
int powerLevel(std::string_view command)
{
    constexpr std::string_view Names[] = {"fm power low", "fm power mid", "fm power high", "fm power turbo"};
    for (int i = 0; i < 4; ++i) {
        if (command == Names[i]) {
            return i;
        }
    }
    return -1;
}

bool numbersAfter(std::string_view command, std::string_view prefix, int base, std::int32_t *out, std::size_t count)
{
    if (!lib::startsWith(command, prefix)) {
        return false;
    }
    command.remove_prefix(prefix.size());
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t end = command.find(' ');
        // Not substr(): its range check would pull in exception code.
        const std::string_view word(command.data(), end == std::string_view::npos ? command.size() : end);
        if (word.empty() || word.size() > 6) {
            return false;
        }
        std::int32_t value = 0;
        for (char c : word) {
            const int digit = c >= '0' && c <= '9' ? c - '0' : base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                                                                  : -1;
            if (digit < 0) {
                return false;
            }
            value = value * base + digit;
        }
        out[i] = value;
        const bool last = i + 1 == count;
        if (last != (end == std::string_view::npos)) {
            return false;
        }
        if (!last) {
            command.remove_prefix(end + 1);
        }
    }
    return true;
}

// "fm tone tx|rx <tone>": off, a CTCSS tone in 0.1 Hz (600..2600), or a
// DCS code as d and three octal digits, then n (normal, the default) or i
// (inverted).
bool toneCommand(std::string_view command, radio::FmTone &out)
{
    using services::Tone;
    std::string_view rest = command;
    if (lib::takeWord(rest) != "fm" || lib::takeWord(rest) != "tone") {
        return false;
    }
    const std::string_view direction = lib::takeWord(rest);
    if (direction != "tx" && direction != "rx") {
        return false;
    }
    out.transmit = direction == "tx";
    const std::string_view value = lib::takeWord(rest);
    if (!rest.empty()) {
        return false;
    }
    if (value == "off") {
        out.tone = Tone{};
        return true;
    }
    if ((value.size() == 4 || value.size() == 5) && value[0] == 'd') {
        const char suffix = value.size() == 5 ? value[4] : 'n';
        bool octal = true;
        for (std::size_t i = 1; i <= 3; ++i) {
            octal = octal && value[i] >= '0' && value[i] <= '7';
        }
        if (!octal || (suffix != 'n' && suffix != 'i')) {
            return false;
        }
        const auto bits = static_cast<std::uint16_t>((value[1] - '0') << 6 | (value[2] - '0') << 3 | (value[3] - '0'));
        out.tone = Tone{Tone::Kind::Dcs, bits, suffix == 'i'};
        return true;
    }
    const auto deciHz = lib::parseNumber(value, 10);
    if (deciHz && *deciHz >= 600 && *deciHz <= 2600) {
        out.tone = Tone{Tone::Kind::Ctcss, static_cast<std::uint16_t>(*deciHz), false};
        return true;
    }
    return false;
}

// Hands @p command without its first @p prefixLength characters to one
// chip's own console commands.
bool chipCommand(radio::ChipCommand::Chip chip, std::string_view command, std::size_t prefixLength)
{
    radio::ChipCommand c{chip, {}};
    c.text << std::string_view(command.data() + prefixLength, command.size() - prefixLength);
    return !c.text.truncated() && radio::post(c);
}

// The radio and audio console commands; false if @p command is none of
// them.
bool radioCommand(std::string_view command)
{
    std::int32_t n[3] = {};
    bool posted = true;
    if (command == "bb") {
        posted = radio::post(radio::Info{});
    } else if (command == "bb restart") {
        posted = radio::post(radio::Restart{});
    } else if (numbersAfter(command, "bbtone ", 10, n, 2) && n[0] > 0 && n[0] <= 8000 && n[1] > 0 && n[1] <= 10000) {
        posted = radio::post(radio::ChipTone{static_cast<std::uint16_t>(n[0]), static_cast<std::uint16_t>(n[1])});
    } else if (numbersAfter(command, "dmr rx ", 10, n, 3) && n[0] >= 100'000 && n[1] <= 15 && n[2] <= 2) {
        posted = radio::post(radio::DmrReceive{static_cast<std::uint32_t>(n[0]) * 1000, static_cast<std::uint8_t>(n[1]),
            static_cast<std::uint8_t>(n[2])});
    } else if ((numbersAfter(command, "fm rx ", 10, n, 1) || numbersAfter(command, "fm rxw ", 10, n, 1)) && n[0] >= 100'000) {
        posted = radio::post(radio::FmReceive{static_cast<std::uint32_t>(n[0]) * 1000, lib::startsWith(command, "fm rxw ")});
    } else if (command == "fm off") {
        posted = radio::post(radio::FmIdle{});
    } else if (command == "fm tx off") {
        posted = radio::post(radio::FmTransmitFrequency{0});
    } else if (numbersAfter(command, "fm tx ", 10, n, 1) && n[0] >= 100'000) {
        posted = radio::post(radio::FmTransmitFrequency{static_cast<std::uint32_t>(n[0]) * 1000});
    } else if (radio::FmTone tone{}; toneCommand(command, tone)) {
        posted = radio::post(tone);
    } else if (const int level = powerLevel(command); level >= 0) {
        posted = radio::post(radio::FmPower{static_cast<services::PowerLevel>(level)});
    } else if (command == "fm txaudio both" || command == "fm txaudio bb" || command == "fm txaudio direct") {
        using Route = radio::FmTransmitAudio::Route;
        const Route route = command == "fm txaudio bb" ? Route::Baseband : command == "fm txaudio direct" ? Route::Direct
                                                                                                          : Route::Both;
        posted = radio::post(radio::FmTransmitAudio{route});
    } else if (numbersAfter(command, "fm tot ", 10, n, 1) && n[0] >= 5 && n[0] <= 600) {
        posted = radio::post(radio::FmTimeout{static_cast<std::uint16_t>(n[0])});
    } else if (command == "lock" || command == "unlock") {
        posted = ui::post(ui::SetKeyLock{command == "lock"});
    } else if (command == "lock status") {
        posted = ui::post(ui::KeyLockReport{});
    } else if (lockSetting(command)) {
        posted = ui::post(ui::KeyLockSettings{lockSettings});
    } else if (numbersAfter(command, "lock auto ", 10, n, 1) && n[0] <= 3600) {
        lockSettings.autoLockMs = static_cast<std::uint32_t>(n[0]) * 1000;
        posted = ui::post(ui::KeyLockSettings{lockSettings});
    } else if (command == "save") {
        posted = radio::post(radio::BatterySaveStatus{});
    } else if (numbersAfter(command, "save ", 10, n, 1) && n[0] <= 2) {
        posted = radio::post(radio::BatterySave{static_cast<std::uint8_t>(n[0])});
    } else if (command == "fm status") {
        posted = radio::post(radio::FmStatus{});
    } else if (numbersAfter(command, "fm sq ", 10, n, 1) && n[0] <= 5) {
        posted = radio::post(radio::FmSquelch{static_cast<std::uint8_t>(n[0])});
    } else if (command == "mic on" || command == "mic off") {
        posted = radio::post(radio::Microphone{command == "mic on"});
    } else if (numbersAfter(command, "beep ", 10, n, 2) && n[0] > 0 && n[0] <= 8000 && n[1] > 0 && n[1] <= 10000) {
        posted = audio::post(audio::Tone{static_cast<std::uint16_t>(n[0]), static_cast<std::uint16_t>(n[1])});
    } else if (command == "rx on" || command == "rx off") {
        posted = audio::post(audio::ReceiveAudio{command == "rx on"});
    } else if (lib::startsWith(command, "fm ")) {
        posted = chipCommand(radio::ChipCommand::Chip::Fm, command, 3);
    } else if (lib::startsWith(command, "bb ")) {
        posted = chipCommand(radio::ChipCommand::Chip::Baseband, command, 3);
    } else {
        return false;
    }
    if (!posted) {
        log::info("console") << "no such hardware, or it is busy";
    }
    return true;
}

void storageInfo()
{
    if (platform->storage == nullptr) {
        log::info("console") << "no storage";
        return;
    }
    const services::StorageInfo i = platform->storage->info();
    log::info("console") << "storage: " << (i.present ? "present" : "missing") << ", id " << lib::hex(i.maker, 2) << " "
                         << lib::hex(i.device, 2) << ", " << i.sizeBytes << " bytes, remapped blocks " << i.remappedBlocks;
}

// Logs 16 bytes of storage from @p address, through the remap table.
void peek(std::uint32_t address)
{
    if (platform->storage == nullptr) {
        log::info("console") << "no storage";
        return;
    }
    std::uint8_t bytes[16];
    const services::StorageResult r = platform->storage->read(address, bytes);
    auto line = log::info("console");
    line << lib::hex(address) << ":";
    for (std::uint8_t b : bytes) {
        line << ' ' << lib::hex(b, 2);
    }
    line << " (" << services::resultName(r) << ")";
}

void runCommand(std::string_view command)
{
    log::info("console") << "> " << command;
    if (command == "help") {
        log::info("console") << "commands: help, fault, hang, restart, battery, autooff <min>, storage, peek <hex addr>, "
                                "light 0-5, pattern, logview, keytest, panel on|off, beep <hz> <ms>, rx on|off, bb, bb restart, "
                                "bbtone <hz> <ms>, "
                                "fm rx|rxw <kHz> (12.5|25 kHz), fm off, fm sq 0-5, fm status, fm tx <kHz>|off, fm tone tx|rx off|<0.1 Hz>|d<octal>[n|i], fm power low|mid|high|turbo, fm tot <s>, fm txaudio both|bb|direct, dmr rx <kHz> <colour code> <slot 1|2, 0 = both direct>, mic on|off, lock, unlock, lock status, lock set knob|keypad|side|forced on|off, lock auto <s>, save [0-2], light time <s>";
        log::info("console") << "FM chip: " << platform->fm.debugHelp();
        withBaseband(platform->baseband, [](auto &b) { log::info("console") << "baseband chip: " << b.debugHelp(); });
    } else if (command == "fault") {
        log::info("console") << "crashing on purpose";
        flushLog();
        platform->system.testFault();
    } else if (command == "hang") {
        log::info("console") << "watchdog no longer fed; expect a watchdog restart within about 3 s";
        system::stopFeedingWatchdog();
    } else if (command == "restart") {
        log::info("console") << "restarting";
        flushLog();
        platform->system.restart();
    } else if (command == "storage") {
        storageInfo();
    } else if (const std::int64_t address = hexAfter(command, "peek "); address >= 0) {
        peek(static_cast<std::uint32_t>(address));
    } else if (command == "battery") {
        system::reportBattery();
    } else if (const int minutes = numberAfter(command, "autooff "); minutes >= 0) {
        system::setAutoPowerOffMinutes(minutes);
        if (minutes == 0) {
            log::info("console") << "auto power-off disabled";
        } else {
            log::info("console") << "auto power-off after " << minutes << " min without a key or knob";
        }
    } else if (const int seconds = numberAfter(command, "light time "); seconds >= 0 && seconds <= 3600) {
        ui::post(ui::BacklightTime{static_cast<std::uint16_t>(seconds)});
    } else if (const int level = numberAfter(command, "light "); level >= 0 && level <= 5) {
        if (!ui::post(ui::Backlight{static_cast<std::uint8_t>(level)})) {
            log::info("console") << "no display";
        }
    } else if (command == "pattern") {
        ui::post(ui::ShowTestPattern{});
    } else if (command == "logview") {
        ui::post(ui::ShowLog{});
    } else if (command == "keytest") {
        ui::post(ui::ShowKeyTest{});
    } else if (command == "panel on" || command == "panel off") {
        ui::post(ui::PanelPower{command == "panel on"});
    } else if (radioCommand(command)) {
    } else if (!command.empty()) {
        log::info("console") << "unknown command, try help";
    }
}

// A line-based debug console on the log channel.
class Console
{
public:
    void poll()
    {
        char input[32];
        const std::size_t n = platform->logSink.readInput(input);
        for (std::size_t i = 0; i < n; ++i) {
            const char c = input[i];
            const bool secondHalfOfCrLf = c == '\n' && m_previous == '\r';
            m_previous = c;
            if (secondHalfOfCrLf) {
                continue;
            }
            if (c == '\r' || c == '\n') {
                if (!m_garbled) {
                    runCommand(std::string_view(m_line, m_length));
                }
                m_length = 0;
                m_garbled = false;
            } else if (c < ' ' || c > '~') {
                // Binary bytes (another program probing the port) spoil the line.
                m_garbled = true;
            } else if (m_length < sizeof m_line) {
                m_line[m_length++] = c;
            }
        }
    }

private:
    char m_line[24] = {};
    std::size_t m_length = 0;
    bool m_garbled = false;
    char m_previous = 0;
};

// Writes all of @p bytes to the data port, waiting for room; gives up if the
// port closes or stays full for half a second.
void writeData(std::span<const std::uint8_t> bytes)
{
    board::DataChannel &port = *platform->dataChannel;
    os::Ticks waited = 0;
    while (!bytes.empty() && port.isOpen() && waited < 500) {
        const std::size_t n = port.write(bytes);
        bytes = bytes.subspan(n);
        if (n == 0) {
            os::sleepMs(1);
            ++waited;
        } else {
            waited = 0;
        }
    }
}

// Serves DeltaMM's PC protocol while requests keep coming.
void serveData(services::DataLink &link)
{
    std::uint8_t input[64];
    for (;;) {
        const std::size_t n = platform->dataChannel->read(input);
        if (n == 0) {
            return;
        }
        link.feed(std::span(input, n), os::nowMs(), writeData);
    }
}

// Moves log lines to the board's sink and runs the debug console. Becomes
// the USB link task later.
void linkMain(void *)
{
    Console console;
    static services::DataLink dataLink(platform->storage, Version);
    os::Ticks wake = os::nowMs();
    while (!os::stopRequested()) {
        console.poll();
        if (platform->dataChannel != nullptr) {
            serveData(dataLink);
        }
        if (platform->logSink.ready()) {
            log::drain(platform->logSink);
        }
        os::sleepUntil(wake, LinkStepMs);
    }
    log::drain(platform->logSink);
}

} // namespace

void start(Platform &p)
{
    platform = &p;
    if (p.display != nullptr) {
        ui::start(*p.display, p.codeplug, p.input);
    }
    if (p.input != nullptr) {
        input::start(*p.input);
    }
    if (p.audio != nullptr) {
        audio::start(*p.audio);
    }
    radio::start(p.fm, p.baseband);
    linkTask.start("link", 2, linkMain, nullptr);
    system::start(p);
}

} // namespace dmm::app
