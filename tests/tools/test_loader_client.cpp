// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Henri Lavikainen

#include "fake_loader.h"
#include "loader_client.h"
#include "loader_images.h"
#include "update_files.h"
#include "zlib_crc.h"

#include "dmm/services/d878uv2_boot_layout.h"

#include <doctest/doctest.h>

#include <QtEndian>

namespace layout = dmm::services::d878uv2;
using namespace dmm::services::boot;

namespace {

QByteArray toBytes(const std::vector<std::uint8_t> &v)
{
    return QByteArray(reinterpret_cast<const char *>(v.data()), static_cast<qsizetype>(v.size()));
}

QByteArray slotImage(std::uint32_t length, std::uint32_t seed, const char *version)
{
    return toBytes(boot_test::makeImage(layout::BootLayout, length, seed, version));
}

// A radio with @p running adopted in slot A, in loader mode.
void prepare(FakeLoader &radio, const QByteArray &running)
{
    std::memcpy(radio.rig.raw.at(layout::SlotABase), running.constData(), static_cast<std::size_t>(running.size()));
    REQUIRE(radio.start().outcome == Outcome::StartImage);
    REQUIRE(radio.start({Request::Stay}).outcome == Outcome::LoaderMode);
}

} // namespace

TEST_CASE("loader client: reads the information")
{
    FakeLoader radio;
    const QByteArray old = slotImage(200 * 1024, 1, "0.24.0");
    prepare(radio, old);
    LoaderClient client(radio);
    QString error;
    const auto info = client.info(&error);
    REQUIRE(info);
    CHECK(info->model == "D878UV2");
    CHECK(info->version == "9.9");
    CHECK(info->slotA == layout::SlotABase);
    CHECK(info->slotBSize == layout::SlotBEnd - layout::SlotBBase);
    CHECK(info->a.state == 1);
    CHECK(info->a.length == 200 * 1024);
    CHECK(info->a.crc == dmmCrc32(old));
    CHECK(info->a.version == "0.24.0");
    CHECK(info->b.state == 0);
    CHECK(info->maxAttempts == 3);
    CHECK(info->stayReason == 0);

    FakeLoader nothing;
    nothing.start();
    LoaderClient c2(nothing);
    CHECK(c2.info(&error));
}

TEST_CASE("loader client: upload, commit, restart, and the image runs")
{
    FakeLoader radio;
    prepare(radio, slotImage(200 * 1024, 1, "0.24.0"));
    const QByteArray next = slotImage(250 * 1024 + 8, 2, "0.25.0");
    LoaderClient client(radio);
    LoaderClient::UploadReport report;
    QString error;
    qint64 last = 0;
    REQUIRE(client.upload(next, true, report, &error, [&](qint64 done, qint64) { last = done; }));
    CHECK_FALSE(report.erased); // slot B was empty and erased
    CHECK(report.writtenChunks == 63);
    CHECK(last == next.size());
    REQUIRE(client.restart(0, 0, &error));
    CHECK(radio.restart().wanted);
    CHECK(radio.rig.settle().outcome == Outcome::StartImage);
    CHECK(radio.rig.slotHolds(false, std::vector<std::uint8_t>(next.begin(), next.end())));
    CHECK(radio.rig.state().a.state == SlotState::Trial);
    CHECK(radio.rig.state().a.mode == ConfirmMode::Manual);
    CHECK(radio.rig.untouched());
}

TEST_CASE("loader client: lost replies, a corrupt chunk and a resumed upload")
{
    const QByteArray next = slotImage(100 * 1024, 2, "0.25.0");
    SUBCASE("a write reply lost: the chunk was written, the checksum shows it")
    {
        FakeLoader radio;
        prepare(radio, slotImage(64 * 1024, 1, "old"));
        radio.dropReply = 30;
        LoaderClient client(radio);
        LoaderClient::UploadReport report;
        QString error;
        REQUIRE(client.upload(next, false, report, &error));
        CHECK(report.retries == 1);
        CHECK(radio.rig.state().b.state == SlotState::Committed);
    }
    SUBCASE("a write never arrived: it is sent again")
    {
        FakeLoader radio;
        prepare(radio, slotImage(64 * 1024, 1, "old"));
        radio.ignoreRequest = 30;
        LoaderClient client(radio);
        LoaderClient::UploadReport report;
        QString error;
        REQUIRE(client.upload(next, false, report, &error));
        CHECK(report.retries == 1);
        CHECK(radio.rig.state().b.state == SlotState::Committed);
    }
    SUBCASE("a chunk with a wrong CRC is refused, then sent right")
    {
        FakeLoader radio;
        prepare(radio, slotImage(64 * 1024, 1, "old"));
        LoaderClient client(radio);
        LoaderClient::UploadReport report;
        QString error;
        REQUIRE(client.upload(next, false, report, &error, {}, 5));
        CHECK(report.retries == 1);
        CHECK(radio.rig.state().b.state == SlotState::Committed);
    }
    SUBCASE("an upload broken off is picked up where it stopped")
    {
        FakeLoader radio;
        prepare(radio, slotImage(64 * 1024, 1, "old"));
        LoaderClient client(radio);
        QString error;
        REQUIRE(client.erase(static_cast<quint32>(next.size()), &error));
        for (int c = 0; c < 10; ++c) {
            REQUIRE(client.write(static_cast<quint32>(c * 4096), next.mid(c * 4096, 4096), &error));
        }
        LoaderClient::UploadReport report;
        REQUIRE(client.upload(next, false, report, &error));
        CHECK_FALSE(report.erased);
        CHECK(report.skippedChunks == 10);
        CHECK(report.writtenChunks == 15);
    }
    SUBCASE("a different image half-written: erased and written again")
    {
        FakeLoader radio;
        prepare(radio, slotImage(64 * 1024, 1, "old"));
        LoaderClient client(radio);
        QString error;
        const QByteArray other = slotImage(100 * 1024, 7, "other");
        REQUIRE(client.erase(0, &error));
        REQUIRE(client.write(0, other.left(4096), &error));
        LoaderClient::UploadReport report;
        REQUIRE(client.upload(next, false, report, &error));
        CHECK(report.erased);
        CHECK(report.writtenChunks == 25);
    }
}

TEST_CASE("loader client: refusals reach the PC")
{
    FakeLoader radio;
    prepare(radio, slotImage(64 * 1024, 1, "old"));
    LoaderClient client(radio);
    QString error;
    const QByteArray next = slotImage(64 * 1024, 2, "new");
    REQUIRE(client.erase(0, &error));
    for (int c = 0; c < 16; ++c) {
        REQUIRE(client.write(static_cast<quint32>(c * 4096), next.mid(c * 4096, 4096), &error));
    }
    int problem = 0;
    CHECK_FALSE(client.commit(static_cast<quint32>(next.size()), dmmCrc32(next) ^ 1, false, &error, &problem));
    CHECK(problem == 7);
    CHECK(error.contains("CRC"));
    CHECK_FALSE(client.write(0, next.left(4096), &error));
    CHECK(error.contains("not erased"));
    // Too large for the spare slot.
    LoaderClient::UploadReport report;
    CHECK_FALSE(client.upload(QByteArray(0x80000, '\0'), false, report, &error));
}

TEST_CASE("loader images: checks and the first-install image")
{
    const QByteArray app = slotImage(180 * 1024, 3, "0.25.0");
    CHECK(checkSlotImage(app).isEmpty());
    // A plain image is refused for the slot, and a slot image for update mode.
    const auto plainLayout = BootLayout{{layout::StubBase, layout::SlotAEnd}, layout::BootLayout.slotB,
        {layout::RecordBase, layout::RecordBase + SectorBytes}, layout::BootLayout.ram, layout::ImageInfoOffset};
    const QByteArray plain = toBytes(boot_test::makeImage(plainLayout, 100 * 1024, 4, "plain"));
    CHECK(checkSlotImage(plain).contains("plain image"));
    CHECK(checkImage(plain).isEmpty());
    CHECK(checkImage(app).contains("dmm-radio update"));

    QByteArray stub = toBytes(boot_test::makeImage(plainLayout, 1024, 5, "stub"));
    qToLittleEndian<quint32>(3, stub.data() + layout::ImageInfoOffset + 4);
    const auto loaderLayout = BootLayout{{layout::LoaderBase, layout::LoaderEnd}, layout::BootLayout.slotB,
        {layout::RecordBase, layout::RecordBase + SectorBytes}, layout::BootLayout.ram, layout::ImageInfoOffset};
    QByteArray loader = toBytes(boot_test::makeImage(loaderLayout, 12 * 1024, 6, "loader"));
    qToLittleEndian<quint32>(2, loader.data() + layout::ImageInfoOffset + 4);
    CHECK(checkStubImage(stub).isEmpty());
    CHECK(checkLoaderImage(loader).isEmpty());
    CHECK_FALSE(checkLoaderImage(stub).isEmpty());
    CHECK_FALSE(checkStubImage(app).isEmpty());

    const QByteArray install = makeLoaderInstallImage(stub, app, loader);
    CHECK(install.size() == static_cast<qsizetype>(layout::LoaderEnd - layout::StubBase));
    CHECK(isLoaderInstallImage(install));
    CHECK_FALSE(isLoaderInstallImage(app));
    CHECK(checkImage(install).isEmpty());
    CHECK(install.left(stub.size()) == stub);
    CHECK(install.mid(layout::SlotABase - layout::StubBase, app.size()) == app);
    const qsizetype loaderAt = layout::LoaderBase - layout::StubBase;
    CHECK(install.mid(loaderAt, loader.size()) == loader);
    CHECK(qFromLittleEndian<quint32>(install.constData() + loaderAt + loader.size()) == dmmCrc32(loader));
    // Erased between the app and the loader: slot B, the record, the spare sector.
    const QByteArray gap = install.mid(layout::SlotABase - layout::StubBase + app.size(),
        loaderAt - (layout::SlotABase - layout::StubBase + app.size()));
    CHECK(gap == QByteArray(gap.size(), '\xFF'));

    // Written to a fake radio as update mode would, the loader adopts the app.
    boot_test::Rig rig(layout::BootLayout, layout::FlashBase, layout::FlashEnd - layout::FlashBase);
    std::memcpy(rig.raw.at(layout::StubBase), install.constData(), static_cast<std::size_t>(install.size()));
    REQUIRE(rig.settle().outcome == Outcome::StartImage);
    CHECK(rig.state().a.state == SlotState::Confirmed);
    CHECK(rig.state().a.crc == dmmCrc32(app));
}
