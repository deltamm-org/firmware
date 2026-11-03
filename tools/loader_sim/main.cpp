// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

// loader_sim: DeltaMM's loader logic on the PC. A file stands for the
// radio's 1 MB program flash and a pseudo-terminal for its USB port, so
// dmm-radio update, confirm, rollback and loader run against it unchanged
// (--port <link>). A small stand-in for the app answers the app's data
// protocol (information, restart into the loader, confirm) from the image
// in slot A. Killing the program is a power cut: started again on the same
// file, it goes on from the boot record.
//
//   loader_sim --flash <file> --link <path> [--install <slot image>]
//              [--confirm-after <s>] [--idle <s>] [--op-delay <ms>]
//
// Images whose version contains "crash" fail at once at every start, as a
// crashing image would; the loader then counts its starts.

#include "dmm/services/boot_loader.h"
#include "dmm/services/d878uv2_boot_layout.h"
#include "dmm/services/data_link.h"
#include "dmm/services/loader_protocol.h"
#include "dmm/services/system_control.h"
#include "dmm/version.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <poll.h>
#include <span>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

namespace boot = dmm::services::boot;
namespace layout = dmm::services::d878uv2;

std::uint32_t nowMs()
{
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return static_cast<std::uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

void say(const std::string &text)
{
    std::printf("[%7.3f] %s\n", nowMs() / 1000.0, text.c_str());
    std::fflush(stdout);
}

// The program flash in a file, written through after every operation, with
// the chip's rules: pages of 2 KB below 0x08080000 and 4 KB above, words
// programmed only when erased or to 0.
class FileFlash final : public boot::ProgramFlash
{
public:
    bool open(const std::string &path, bool &created)
    {
        m_fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
        if (m_fd < 0) {
            return false;
        }
        m_bytes.assign(layout::FlashEnd - layout::FlashBase, 0xFF);
        const ssize_t got = ::pread(m_fd, m_bytes.data(), m_bytes.size(), 0);
        created = got <= 0;
        if (got != static_cast<ssize_t>(m_bytes.size())) {
            std::fill(m_bytes.begin() + (got > 0 ? got : 0), m_bytes.end(), 0xFF);
            return persist(layout::FlashBase, static_cast<std::uint32_t>(m_bytes.size()));
        }
        return true;
    }

    void setDelayMs(int ms) { m_delayMs = ms; }
    std::uint8_t *at(std::uint32_t address) { return m_bytes.data() + (address - layout::FlashBase); }

    bool persist(std::uint32_t address, std::uint32_t length)
    {
        return ::pwrite(m_fd, at(address), length, address - layout::FlashBase) == static_cast<ssize_t>(length) && ::fdatasync(m_fd) == 0;
    }

    bool read(std::uint32_t address, std::span<std::uint8_t> out) override
    {
        if (!inside(address, out.size())) {
            return false;
        }
        std::memcpy(out.data(), at(address), out.size());
        return true;
    }

    bool eraseSector(std::uint32_t address) override
    {
        if (address % boot::SectorBytes != 0 || !inside(address, boot::SectorBytes)) {
            return false;
        }
        for (std::uint32_t page = address; page < address + boot::SectorBytes;) {
            const std::uint32_t size = page < 0x08080000 ? 2048 : 4096;
            delay();
            std::memset(at(page), 0xFF, size);
            if (!persist(page, size)) {
                return false;
            }
            page += size;
        }
        return true;
    }

    bool program(std::uint32_t address, std::span<const std::uint8_t> data) override
    {
        if (address % 4 != 0 || data.size() % 4 != 0 || !inside(address, data.size())) {
            return false;
        }
        delay();
        for (std::size_t i = 0; i < data.size(); i += 4) {
            std::uint8_t *word = at(address + static_cast<std::uint32_t>(i));
            const bool erased = word[0] == 0xFF && word[1] == 0xFF && word[2] == 0xFF && word[3] == 0xFF;
            const bool zero = data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 0;
            if (!erased && !zero) {
                persist(address, static_cast<std::uint32_t>(i));
                return false;
            }
            std::memcpy(word, data.data() + i, 4);
        }
        return persist(address, static_cast<std::uint32_t>(data.size()));
    }

private:
    bool inside(std::uint32_t address, std::size_t length) const
    {
        return address >= layout::FlashBase && address <= layout::FlashEnd && length <= layout::FlashEnd - address;
    }
    void delay() const
    {
        if (m_delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_delayMs));
        }
    }

    int m_fd = -1;
    int m_delayMs = 0;
    std::vector<std::uint8_t> m_bytes;
};

class QuietEvents final : public boot::LoaderEvents
{
public:
    void busy(bool) override {}
    void alive() override {}
};

struct Options
{
    std::string flash;
    std::string link;
    std::string install;
    int confirmAfterS = 60;
    int idleS = 60;
    int opDelayMs = 0;
};

class Sim;

// The app's view of the loader, as the board gives it on the radio.
class SimSystem final : public dmm::services::SystemControl
{
public:
    explicit SimSystem(Sim &sim)
        : m_sim(sim)
    {
    }
    bool powerSwitchOff() override { return false; }
    void holdPower() override {}
    void releasePower() override {}
    void feedWatchdog() override {}
    void restart() override {}
    dmm::services::ResetReport lastReset() override { return {}; }
    void testFault() override {}
    bool enterLoader(dmm::services::LoaderRequest request) override;
    bool confirmImage() override;
    dmm::services::LoaderStatus loaderStatus() override;

private:
    Sim &m_sim;
};

class Sim
{
public:
    explicit Sim(const Options &o)
        : m_options(o), m_system(*this)
    {
    }

    bool start()
    {
        bool created = false;
        if (!m_flash.open(m_options.flash, created)) {
            say("cannot open " + m_options.flash);
            return false;
        }
        if (created && !m_options.install.empty() && !install()) {
            return false;
        }
        m_flash.setDelayMs(m_options.opDelayMs);
        if (!openPty()) {
            return false;
        }
        say("power on");
        boot(true);
        return true;
    }

    void run()
    {
        for (;;) {
            pollfd p{m_master, POLLIN, 0};
            ::poll(&p, 1, 20);
            std::uint8_t buffer[4096];
            const ssize_t n = ::read(m_master, buffer, sizeof buffer);
            const std::uint32_t now = nowMs();
            if (n > 0) {
                m_lastHeard = now;
                feed(std::span(buffer, static_cast<std::size_t>(n)), now);
            }
            tick(now);
        }
    }

    // Called by the app's stand-in.
    void requestLoader(boot::HandoverRequest r)
    {
        m_handover = r;
        m_resetWanted = true;
    }
    boot::BootRecord recordForImage() { return boot::BootRecord(m_imageFlash, layout::BootLayout.record); }

private:
    bool install()
    {
        FILE *f = std::fopen(m_options.install.c_str(), "rb");
        if (f == nullptr) {
            say("cannot read " + m_options.install);
            return false;
        }
        std::vector<std::uint8_t> image(layout::SlotBEnd - layout::SlotBBase);
        const std::size_t n = std::fread(image.data(), 1, image.size(), f);
        std::fclose(f);
        std::memcpy(m_flash.at(layout::SlotABase), image.data(), n);
        say("installed " + std::to_string(n) + " bytes in slot A, as the first install does");
        return m_flash.persist(layout::SlotABase, static_cast<std::uint32_t>(n));
    }

    bool openPty()
    {
        m_master = ::posix_openpt(O_RDWR | O_NOCTTY);
        if (m_master < 0 || ::grantpt(m_master) != 0 || ::unlockpt(m_master) != 0) {
            say("no pseudo-terminal");
            return false;
        }
        const char *name = ::ptsname(m_master);
        // Kept open, raw, so the port stays usable while no client has it.
        m_slave = ::open(name, O_RDWR | O_NOCTTY);
        termios t{};
        ::tcgetattr(m_slave, &t);
        ::cfmakeraw(&t);
        ::tcsetattr(m_slave, TCSANOW, &t);
        ::fcntl(m_master, F_SETFL, ::fcntl(m_master, F_GETFL) | O_NONBLOCK);
        if (!m_options.link.empty()) {
            ::unlink(m_options.link.c_str());
            if (::symlink(name, m_options.link.c_str()) != 0) {
                say("cannot make the link " + m_options.link);
                return false;
            }
        }
        say(std::string("port ") + name + (m_options.link.empty() ? "" : " as " + m_options.link));
        return true;
    }

    void write(std::span<const std::uint8_t> bytes)
    {
        while (!bytes.empty()) {
            const ssize_t n = ::write(m_master, bytes.data(), bytes.size());
            if (n > 0) {
                bytes = bytes.subspan(static_cast<std::size_t>(n));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    std::string slotVersion()
    {
        std::uint8_t bytes[boot::ImageInfoBytes];
        boot::ImageInfo info;
        if (!m_flash.read(layout::SlotABase + layout::ImageInfoOffset, bytes) || !boot::parseImageInfo(bytes, info)) {
            return "?";
        }
        return info.version;
    }

    // A restart: power-on, or software (the hand-over block survives).
    void boot(bool powerOn)
    {
        ::tcflush(m_master, TCIOFLUSH);
        boot::BootInputs in;
        if (!powerOn) {
            in.request = m_handover == boot::HandoverRequest::Stay ? boot::Request::Stay
                : m_handover == boot::HandoverRequest::Rollback    ? boot::Request::Rollback
                                                                   : boot::Request::None;
            in.swapCut = m_handover == boot::HandoverRequest::SwapCut ? m_swapCut : 0;
        }
        m_handover = boot::HandoverRequest::None;
        for (int restarts = 0; restarts < 50; ++restarts) {
            boot::Loader loader(m_loaderFlash, layout::BootLayout, m_buffer, m_events);
            const boot::BootResult r = loader.boot(in);
            in = {};
            if (r.outcome == boot::Outcome::Reset) {
                say("loader: restart after swap work");
                continue;
            }
            if (r.outcome == boot::Outcome::LoaderMode) {
                enterLoaderMode(r.stay);
                return;
            }
            const std::string version = slotVersion();
            const boot::BootState s = loader.record().state();
            say("app " + version + " started, " +
                (s.a.state == boot::SlotState::Trial ? "trial start " + std::to_string(s.attempts) + " of 3" : std::string("confirmed")));
            if (version.find("crash") != std::string::npos) {
                say("app " + version + " crashes");
                continue;
            }
            enterAppMode(version);
            return;
        }
        say("too many restarts");
        std::exit(3);
    }

    void enterLoaderMode(boot::StayReason why)
    {
        static const char *const Names[] = {"-", "asked for", "force key", "no image to start", "flash error"};
        say(std::string("loader mode: ") + Names[static_cast<int>(why)]);
        m_loaderMode = true;
        m_protocol.reset();
        m_loader.emplace(m_loaderFlash, layout::BootLayout, m_buffer, m_events);
        m_loader->record().load();
        m_protocol.emplace(*m_loader, "D878UV2", dmm::Version);
        m_protocol->setStayReason(why);
        m_lastHeard = nowMs();
    }

    void enterAppMode(const std::string &version)
    {
        m_loaderMode = false;
        m_version = version;
        m_link.emplace(nullptr, m_version, &m_system);
        m_appStarted = nowMs();
        m_autoConfirmDone = false;
    }

    void feed(std::span<const std::uint8_t> bytes, std::uint32_t now)
    {
        auto writer = [this](std::span<const std::uint8_t> b) { write(b); };
        if (m_loaderMode) {
            m_protocol->feed(bytes, now, writer);
            const auto &r = m_protocol->restart();
            if (r.wanted) {
                m_handover = r.request == boot::Request::Rollback ? boot::HandoverRequest::Rollback
                    : r.swapCut != 0                              ? boot::HandoverRequest::SwapCut
                                                                  : boot::HandoverRequest::None;
                m_swapCut = r.swapCut;
                m_resetWanted = true;
            }
        } else {
            m_link->feed(bytes, now, writer);
            if (const auto request = m_link->takeLoaderRequest()) {
                m_system.enterLoader(*request);
            }
        }
    }

    void tick(std::uint32_t now)
    {
        if (m_resetWanted) {
            m_resetWanted = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            say("restart");
            boot(false);
            return;
        }
        if (m_loaderMode) {
            const boot::BootState &s = m_loader->record().state();
            const bool startable = s.a.state == boot::SlotState::Confirmed || s.a.state == boot::SlotState::Trial ||
                s.b.state == boot::SlotState::Committed || s.b.state == boot::SlotState::Confirmed;
            if (startable && now - m_lastHeard > static_cast<std::uint32_t>(m_options.idleS) * 1000) {
                say("loader mode: no PC for a while, restart");
                m_resetWanted = true;
            }
        } else if (!m_autoConfirmDone && now - m_appStarted > static_cast<std::uint32_t>(m_options.confirmAfterS) * 1000) {
            m_autoConfirmDone = true;
            const dmm::services::LoaderStatus l = m_system.loaderStatus();
            if (l.trial && !l.manual && m_system.confirmImage()) {
                say("app confirmed itself");
            }
        }
    }

    Options m_options;
    FileFlash m_flash;
    boot::GuardedFlash m_loaderFlash{m_flash, layout::LoaderWritable};
    boot::GuardedFlash m_imageFlash{m_flash, layout::ImageWritable};
    std::array<std::uint8_t, boot::SectorBytes> m_buffer{};
    QuietEvents m_events;
    SimSystem m_system;
    int m_master = -1;
    int m_slave = -1;
    bool m_loaderMode = false;
    bool m_resetWanted = false;
    boot::HandoverRequest m_handover = boot::HandoverRequest::None;
    std::uint16_t m_swapCut = 0;
    std::uint32_t m_lastHeard = 0;
    std::uint32_t m_appStarted = 0;
    bool m_autoConfirmDone = false;
    std::string m_version;
    std::optional<boot::Loader> m_loader;
    std::optional<boot::LoaderProtocol> m_protocol;
    std::optional<dmm::services::DataLink> m_link;
};

bool SimSystem::enterLoader(dmm::services::LoaderRequest request)
{
    say(request == dmm::services::LoaderRequest::Rollback ? "app: restart into the loader to roll back" : "app: restart into the loader");
    m_sim.requestLoader(request == dmm::services::LoaderRequest::Rollback ? boot::HandoverRequest::Rollback
                                                                          : boot::HandoverRequest::Stay);
    return true;
}

bool SimSystem::confirmImage()
{
    boot::BootRecord record = m_sim.recordForImage();
    if (!record.load()) {
        return false;
    }
    if (record.state().a.state == boot::SlotState::Confirmed) {
        return true;
    }
    const bool ok = record.state().a.state == boot::SlotState::Trial && record.append(boot::entry::confirm(record.state().a.crc));
    if (ok) {
        say("app: image confirmed");
    }
    return ok;
}

dmm::services::LoaderStatus SimSystem::loaderStatus()
{
    boot::BootRecord record = m_sim.recordForImage();
    dmm::services::LoaderStatus l;
    if (!record.load()) {
        return l;
    }
    const boot::BootState &s = record.state();
    l.present = true;
    l.trial = s.a.state == boot::SlotState::Trial;
    l.manual = s.a.mode == boot::ConfirmMode::Manual;
    l.attempts = s.attempts;
    l.maxAttempts = boot::Loader::MaxAttempts;
    l.previousKept = s.b.state == boot::SlotState::Confirmed;
    l.fallback = s.lastFallback;
    return l;
}

} // namespace

int main(int argc, char *argv[])
{
    Options o;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        const std::string value = argv[i + 1];
        if (key == "--flash") {
            o.flash = value;
        } else if (key == "--link") {
            o.link = value;
        } else if (key == "--install") {
            o.install = value;
        } else if (key == "--confirm-after") {
            o.confirmAfterS = std::atoi(value.c_str());
        } else if (key == "--idle") {
            o.idleS = std::atoi(value.c_str());
        } else if (key == "--op-delay") {
            o.opDelayMs = std::atoi(value.c_str());
        } else {
            std::fprintf(stderr, "unknown option %s\n", key.c_str());
            return 2;
        }
    }
    if (o.flash.empty()) {
        std::fprintf(stderr, "usage: loader_sim --flash <file> --link <path> [--install <slot image>] [--confirm-after <s>] "
                             "[--idle <s>] [--op-delay <ms>]\n");
        return 2;
    }
    Sim sim(o);
    if (!sim.start()) {
        return 1;
    }
    sim.run();
}
