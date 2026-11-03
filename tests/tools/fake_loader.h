// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#pragma once

#include "boot_test_rig.h"
#include "transport.h"

#include "dmm/services/d878uv2_boot_layout.h"
#include "dmm/services/loader_protocol.h"

#include <QByteArray>

/// A radio in loader mode: the loader's own protocol code on a fake flash
/// with the radio's layout. Can drop the reply to request n (the request
/// still takes effect) or not answer it at all.
class FakeLoader final : public Transport
{
public:
    FakeLoader()
        : rig(dmm::services::d878uv2::BootLayout, dmm::services::d878uv2::FlashBase,
              dmm::services::d878uv2::FlashEnd - dmm::services::d878uv2::FlashBase)
    {
    }

    /// Starts the loader as after a restart; returns what it did.
    dmm::services::boot::BootResult start(const dmm::services::boot::BootInputs &in = {})
    {
        m_protocol.reset();
        m_loader.emplace(rig.guarded, rig.layout, rig.buffer, rig.events);
        const auto r = m_loader->boot(in);
        m_protocol.emplace(*m_loader, "D878UV2", "9.9");
        return r;
    }

    bool write(const QByteArray &data) override
    {
        const int index = requests++;
        if (index == ignoreRequest) {
            return true;
        }
        QByteArray reply;
        m_protocol->feed(std::span(reinterpret_cast<const std::uint8_t *>(data.constData()), static_cast<std::size_t>(data.size())),
            now, [&](std::span<const std::uint8_t> b) { reply.append(reinterpret_cast<const char *>(b.data()), static_cast<qsizetype>(b.size())); });
        now += 10;
        if (index != dropReply) {
            m_rx += reply;
        }
        return true;
    }

    QByteArray read(qsizetype count, int) override
    {
        const QByteArray out = m_rx.left(count);
        m_rx.remove(0, out.size());
        return out;
    }

    void discardInput() override { m_rx.clear(); }

    const dmm::services::boot::LoaderProtocol::Restart &restart() const { return m_protocol->restart(); }

    boot_test::Rig rig;
    int requests = 0;
    int dropReply = -1;
    int ignoreRequest = -1;
    std::uint32_t now = 0;

private:
    std::optional<dmm::services::boot::Loader> m_loader;
    std::optional<dmm::services::boot::LoaderProtocol> m_protocol;
    QByteArray m_rx;
};
