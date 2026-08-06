#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/SpriteAnimationClipPayload.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/SpriteAnimationAuthoringDocument.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace Tina::Editor {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 marker)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(marker);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] AssetFormat::SpriteAnimationFrameDesc frame(
    Core::u8 marker, float durationSeconds = 0.1F)
{
    return {
        .spriteId = assetId(marker),
        .durationSeconds = durationSeconds,
    };
}

[[nodiscard]] SpriteAnimationAuthoringDesc makeClip()
{
    return SpriteAnimationAuthoringDesc{
        .clipId = assetId(0x50U),
        .playbackMode = AssetFormat::SpriteAnimationPlaybackMode::Loop,
        .frames = {frame(0x11U, 0.1F), frame(0x22U, 0.2F), frame(0x11U, 0.3F)},
    };
}

[[nodiscard]] SpriteAnimationAuthoringDocument createDocument()
{
    auto document = SpriteAnimationAuthoringDocument::Create(makeClip());
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

TEST(SpriteAnimationAuthoringDocumentTests, OwnsCanonicalPayloadAndCookPreview)
{
    auto document = createDocument();
    EXPECT_EQ(document.clipId(), assetId(0x50U));
    EXPECT_EQ(document.frameCount(), 3U);
    EXPECT_NEAR(document.totalDurationSeconds(), 0.6, 0.0001);

    auto payload = AssetFormat::parseSpriteAnimationClipPayload(document.payloadBytes());
    ASSERT_TRUE(payload);
    EXPECT_EQ(payload->schemaVersion, AssetFormat::SpriteAnimationClipWire::SchemaVersion);
    EXPECT_EQ(payload->playbackMode, AssetFormat::SpriteAnimationPlaybackMode::Loop);
    EXPECT_EQ(payload->frameCount, 3U);
    EXPECT_EQ(payload->spriteDependencyCount, 2U);
    ASSERT_TRUE(document.frameAt(1));
    EXPECT_EQ(document.frameAt(1)->spriteId, assetId(0x22U));

    auto preview = document.cookPreview(AssetFormat::TargetPlatform::LinuxX64);
    ASSERT_TRUE(preview);
    EXPECT_EQ(preview->documentRevision, document.revision());
    EXPECT_EQ(preview->targetPlatform, AssetFormat::TargetPlatform::LinuxX64);
    EXPECT_EQ(preview->assetId, document.clipId());
    EXPECT_FALSE(preview->path.view().empty());
    auto cooked = AssetFormat::parseCookedAssetView(preview->cookedBytes);
    ASSERT_TRUE(cooked);
    EXPECT_EQ(cooked->header().assetKind, AssetFormat::AssetKind::SpriteAnimationClip);
    EXPECT_EQ(cooked->header().assetTypeVersion,
              AssetFormat::SpriteAnimationClipWire::SchemaVersion);
    EXPECT_EQ(cooked->header().dependencyCount, 2U);
    EXPECT_TRUE(AssetFormat::verifyCookedAssetContentHash(*cooked));
}

TEST(SpriteAnimationAuthoringDocumentTests, EditsFramesModeAndOrderAsAtomicRevisions)
{
    auto document = createDocument();
    const auto baselineRevision = document.revision();
    ASSERT_TRUE(document.setPlaybackMode(AssetFormat::SpriteAnimationPlaybackMode::Once));
    ASSERT_TRUE(document.insertFrame(1, frame(0x33U, 0.4F)));
    ASSERT_TRUE(document.setFrameDuration(1, 0.5F));
    ASSERT_TRUE(document.duplicateFrame(1));
    ASSERT_TRUE(document.moveFrame(4, 1));
    ASSERT_TRUE(document.eraseFrame(3));
    EXPECT_EQ(document.revision(), baselineRevision + 6U);
    EXPECT_EQ(document.playbackMode(), AssetFormat::SpriteAnimationPlaybackMode::Once);
    ASSERT_EQ(document.frameCount(), 4U);

    auto authored = document.snapshot();
    ASSERT_TRUE(authored);
    const std::array expectedMarkers{0x11U, 0x11U, 0x33U, 0x22U};
    for (Core::usize index = 0; index < expectedMarkers.size(); ++index)
    {
        EXPECT_EQ(authored->frames[index].spriteId,
                  assetId(static_cast<Core::u8>(expectedMarkers[index])));
    }
    EXPECT_FLOAT_EQ(authored->frames[2].durationSeconds, 0.5F);

    ASSERT_TRUE(document.undo());
    EXPECT_EQ(document.frameCount(), 5U);
    ASSERT_TRUE(document.redo());
    EXPECT_EQ(document.frameCount(), 4U);
}

TEST(SpriteAnimationAuthoringDocumentTests, FailedEditsPreserveCurrentAndRedoBranch)
{
    auto document = createDocument();
    ASSERT_TRUE(document.appendFrame(frame(0x44U)));
    ASSERT_TRUE(document.undo());
    ASSERT_TRUE(document.canRedo());
    const auto beforeRevision = document.revision();
    const auto beforePayload =
        std::vector(document.payloadBytes().begin(), document.payloadBytes().end());

    const auto missing = document.setFrameDuration(99U, 0.2F);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, EditorErrorCode::FrameNotFound);
    const auto invalidDuration = document.setFrameDuration(0U, 0.0F);
    ASSERT_FALSE(invalidDuration);
    EXPECT_EQ(document.revision(), beforeRevision);
    EXPECT_EQ(std::vector(document.payloadBytes().begin(), document.payloadBytes().end()),
              beforePayload);
    EXPECT_TRUE(document.canRedo());
}

TEST(SpriteAnimationAuthoringDocumentTests, LoadsOnlyCurrentCookedSchemaAsCleanBaseline)
{
    auto source = createDocument();
    ASSERT_TRUE(source.setPlaybackMode(AssetFormat::SpriteAnimationPlaybackMode::PingPong));
    auto preview = source.cookPreview();
    ASSERT_TRUE(preview);

    auto destination = SpriteAnimationAuthoringDocument::Create(SpriteAnimationAuthoringDesc{
        .clipId = assetId(0x60U),
        .frames = {frame(0x61U)},
    });
    ASSERT_TRUE(destination);
    ASSERT_TRUE(destination->appendFrame(frame(0x62U)));
    ASSERT_TRUE(destination->loadCookedAsset(preview->cookedBytes));
    EXPECT_EQ(destination->clipId(), source.clipId());
    EXPECT_EQ(destination->playbackMode(),
              AssetFormat::SpriteAnimationPlaybackMode::PingPong);
    EXPECT_EQ(destination->frameCount(), source.frameCount());
    EXPECT_FALSE(destination->canUndo());
    EXPECT_FALSE(destination->canRedo());

    auto sourceDesc = source.snapshot();
    ASSERT_TRUE(sourceDesc);
    auto dependencies = AssetFormat::makeSpriteAnimationClipDependencies({
        .playbackMode = sourceDesc->playbackMode,
        .frames = sourceDesc->frames,
    });
    ASSERT_TRUE(dependencies);
    auto unsupportedVersion = AssetFormat::writeCookedAssetBytes({
        .assetKind = AssetFormat::AssetKind::SpriteAnimationClip,
        .assetTypeVersion = AssetFormat::SpriteAnimationClipWire::SchemaVersion + 1U,
        .targetPlatform = AssetFormat::TargetPlatform::WindowsX64,
        .assetId = sourceDesc->clipId,
        .dependencies = *dependencies,
        .payload = source.payloadBytes(),
        .payloadAlignment = 4,
        .computeContentHash = true,
    });
    ASSERT_TRUE(unsupportedVersion);
    const auto rejected = destination->loadCookedAsset(*unsupportedVersion);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, AssetFormat::AssetFormatErrorCode::UnsupportedSchema);
}

TEST(SpriteAnimationAuthoringDocumentTests, EnforcesFrameAndHistoryBudgetsWithoutPublishing)
{
    auto invalid = SpriteAnimationAuthoringDocument::Create(
        makeClip(), SpriteAnimationAuthoringDocumentConfig{.frameCapacity = 0});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, EditorErrorCode::InvalidConfiguration);

    auto frameBounded = SpriteAnimationAuthoringDocument::Create(
        makeClip(), SpriteAnimationAuthoringDocumentConfig{
                        .frameCapacity = 3,
                        .historyEntryCapacity = 4,
                        .historyByteCapacity = 4096,
                    });
    ASSERT_TRUE(frameBounded);
    const auto capacityStatus = frameBounded->appendFrame(frame(0x44U));
    ASSERT_FALSE(capacityStatus);
    EXPECT_EQ(capacityStatus.error().code, EditorErrorCode::DocumentCapacityExceeded);

    auto historyBounded = SpriteAnimationAuthoringDocument::Create(
        SpriteAnimationAuthoringDesc{
            .clipId = assetId(0x70U),
            .frames = {frame(0x71U)},
        },
        SpriteAnimationAuthoringDocumentConfig{
            .frameCapacity = 2,
            .historyEntryCapacity = 4,
            .historyByteCapacity = 96,
        });
    ASSERT_TRUE(historyBounded);
    const auto beforeRevision = historyBounded->revision();
    const auto historyStatus = historyBounded->appendFrame(frame(0x71U));
    ASSERT_FALSE(historyStatus);
    EXPECT_EQ(historyStatus.error().code, EditorErrorCode::HistoryCapacityExceeded);
    EXPECT_EQ(historyBounded->revision(), beforeRevision);
    EXPECT_EQ(historyBounded->frameCount(), 1U);
}

} // namespace
} // namespace Tina::Editor
