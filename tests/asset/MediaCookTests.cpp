#include <tina/asset/MediaCook.hpp>

#include <tina/asset/SourceImportCapture.hpp>
#include <tina/asset_format/AudioClipPayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

// 2x2 RGBA8 PNG: red, green / blue, translucent white.
constexpr std::array<unsigned char, 76> TinyPng{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00,
    0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
    0x1F, 0x0C, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78,
    0x28, 0xA0, 0xDB, 0x77, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
    0xAE, 0x42, 0x60, 0x82};

[[nodiscard]] std::vector<std::byte> tinyPngBytes()
{
    std::vector<std::byte> bytes(TinyPng.size());
    std::memcpy(bytes.data(), TinyPng.data(), TinyPng.size());
    return bytes;
}

// Four-frame 8kHz mono PCM16 RIFF/WAVE file.
[[nodiscard]] std::vector<std::byte> tinyWavBytes()
{
    constexpr std::array<std::int16_t, 4> samples{0, 16384, -16384, 32767};
    std::vector<unsigned char> bytes;
    const auto push32 = [&](Core::u32 value) {
        bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
        bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
        bytes.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
        bytes.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
    };
    const auto push16 = [&](Core::u16 value) {
        bytes.push_back(static_cast<unsigned char>(value & 0xFFU));
        bytes.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    };
    const auto pushTag = [&](const char* tag) {
        bytes.insert(bytes.end(), tag, tag + 4);
    };
    const Core::u32 dataBytes = static_cast<Core::u32>(samples.size() * 2U);
    pushTag("RIFF");
    push32(36U + dataBytes);
    pushTag("WAVE");
    pushTag("fmt ");
    push32(16U);
    push16(1U);      // PCM
    push16(1U);      // mono
    push32(8000U);   // sample rate
    push32(16000U);  // byte rate
    push16(2U);      // block align
    push16(16U);     // bits per sample
    pushTag("data");
    push32(dataBytes);
    for (const std::int16_t sample : samples) {
        push16(static_cast<Core::u16>(sample));
    }
    std::vector<std::byte> out(bytes.size());
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return out;
}

class MediaCookTests : public ::testing::Test {
  protected:
    void SetUp() override
    {
        root_ = std::filesystem::temp_directory_path() /
                ("tina_media_cook_" + std::to_string(::testing::UnitTest::GetInstance()
                                                          ->random_seed()));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_ / "textures");
        std::filesystem::create_directories(root_ / "audio");
    }

    void TearDown() override
    {
        std::error_code cleanupError;
        std::filesystem::remove_all(root_, cleanupError);
    }

    [[nodiscard]] std::string writeSource(const std::filesystem::path& relative,
                                          std::span<const std::byte> bytes)
    {
        const auto path = root_ / relative;
        auto status = Core::writeFile(toUtf8(path), bytes);
        EXPECT_TRUE(status) << status.error().message;
        return toUtf8(path);
    }

    [[nodiscard]] SourceImportCaptureConfig captureConfig() const
    {
        return SourceImportCaptureConfig{.sourceRootUtf8 = rootUtf8_};
    }

    void cacheRootUtf8() { rootUtf8_ = toUtf8(root_); }

    std::filesystem::path root_{};
    std::string rootUtf8_{};
};

TEST_F(MediaCookTests, PngCooksOneTextureWithStableId)
{
    cacheRootUtf8();
    const auto png = tinyPngBytes();
    const auto path = writeSource("textures/albedo.png", png);

    auto first = cookTextureFileToCatalogSourceResult(
        path, AssetFormat::TargetPlatform::WindowsX64, captureConfig());
    ASSERT_TRUE(first) << first.error().message;
    auto second = cookTextureFileToCatalogSourceResult(
        path, AssetFormat::TargetPlatform::WindowsX64, captureConfig());
    ASSERT_TRUE(second) << second.error().message;

    ASSERT_EQ(first->request.assets.size(), 1U);
    const auto& texture = first->request.assets[0];
    EXPECT_EQ(texture.assetKind, AssetFormat::AssetKind::Texture2D);
    EXPECT_TRUE(texture.assetId);
    EXPECT_EQ(texture.assetId, second->request.assets[0].assetId);

    auto texturePayload = AssetFormat::parseTexture2DPayload(texture.payload);
    ASSERT_TRUE(texturePayload) << texturePayload.error().message;
    EXPECT_EQ(texturePayload->width, 2U);
    EXPECT_EQ(texturePayload->height, 2U);
    EXPECT_EQ(texturePayload->pixelFormat, AssetFormat::Texture2DPixelFormat::Rgba8Unorm);
    // An imported image carries a complete chain, so a minified sprite samples a real
    // level instead of aliasing against the base.
    EXPECT_EQ(texturePayload->colorSpace, AssetFormat::Texture2DColorSpace::Srgb);
    ASSERT_EQ(texturePayload->levelCount, AssetFormat::texture2DFullMipLevelCount(2, 2));
    ASSERT_EQ(texturePayload->levelCount, 2U);
    EXPECT_EQ(texturePayload->sampler.mipFilter, AssetFormat::Texture2DMipFilterMode::Linear);
    const auto levels = texturePayload->levels();
    EXPECT_EQ(levels.front().width, 2U);
    EXPECT_EQ(levels.front().height, 2U);
    EXPECT_EQ(levels.back().width, 1U);
    EXPECT_EQ(levels.back().height, 1U);
    EXPECT_EQ(levels.back().bytes.size(), 4U);
    ASSERT_EQ(texturePayload->basePixels().size(), 16U);
    EXPECT_EQ(std::to_integer<Core::u8>(texturePayload->basePixels()[0]), 255U);
    EXPECT_EQ(std::to_integer<Core::u8>(texturePayload->basePixels()[5]), 255U);

    ASSERT_EQ(first->sourceImports.units.size(), 1U);
    const auto& unit = first->sourceImports.units.front();
    EXPECT_EQ(unit.importerKind, SourceImporterKind::Texture);
    EXPECT_EQ(unit.importerVersion, 2U);
    ASSERT_EQ(unit.outputs.size(), 1U);
    ASSERT_EQ(first->sourceImports.sources.size(), 1U);
    EXPECT_EQ(first->sourceImports.sources.front().path, "textures/albedo.png");
}

// A per-position XOR digest overwrote byte 0 with the media tag, so equal-length
// locators differing only in their first character produced one AssetId. Two such
// images in a single batch were then rejected as "output has multiple owners".
TEST_F(MediaCookTests, DistinctImagePathsNeverShareAnAssetId)
{
    cacheRootUtf8();
    const auto png = tinyPngBytes();
    const std::array locators{
        "a.png", "b.png",                        // differ only at index 0
        "textures/a.png", "textures/b.png",      // same, behind a directory
        "ab.png", "ba.png",                      // transposed characters
        "textures/albedo.png", "textures/albedq.png",
    };

    std::vector<Core::AssetId> seen;
    for (const std::string_view locator : locators) {
        const auto path = writeSource(locator, png);
        auto cooked = cookTextureFileToCatalogSourceResult(
            path, AssetFormat::TargetPlatform::WindowsX64, captureConfig());
        ASSERT_TRUE(cooked) << locator << ": " << cooked.error().message;
        ASSERT_EQ(cooked->request.assets.size(), 1U) << locator;
        const Core::AssetId assetId = cooked->request.assets.front().assetId;
        ASSERT_TRUE(assetId) << locator;
        EXPECT_EQ(std::find(seen.begin(), seen.end(), assetId), seen.end())
            << "AssetId collision for " << locator;
        seen.push_back(assetId);
    }
    EXPECT_EQ(seen.size(), locators.size());
}

// Cross-unit corpus: one importer's outputs must not collide with another's for the
// same locator. Media and glTF share one derivation function, and two role-tag
// values are reused across them -- TextureMedia and the glTF metallic-roughness
// texture are both 0x75, AudioMedia and the glTF animation clip are both 0x77.
// Since the tag is also the leading byte of the AssetId, those pairs are separated
// only by the remaining hash inputs (AssetKind for the 0x77 pair, channel for the
// 0x75 pair, where both sides are Texture2D). Media's own two tags differ, so
// texture-vs-audio was never the fragile case; the reuse against glTF is.
TEST_F(MediaCookTests, MediaIdsDoNotCollideWithGltfOutputsSharingTheirRoleTag)
{
    cacheRootUtf8();

    // Same locator, cooked as each media kind: distinct because the tags differ.
    const auto pngPath = writeSource("shared/name.png", tinyPngBytes());
    const auto wavPath = writeSource("shared/name.wav", tinyWavBytes());
    auto texture = cookTextureFileToCatalogSourceResult(
        pngPath, AssetFormat::TargetPlatform::WindowsX64, captureConfig());
    ASSERT_TRUE(texture) << texture.error().message;
    ASSERT_EQ(texture->request.assets.size(), 1U);
    auto audio = cookAudioFileToCatalogSourceResult(
        wavPath, AssetFormat::TargetPlatform::WindowsX64, captureConfig());
    ASSERT_TRUE(audio) << audio.error().message;
    ASSERT_EQ(audio->request.assets.size(), 1U);
    EXPECT_NE(texture->request.assets.front().assetId, audio->request.assets.front().assetId);

    // The identity must also survive being reached by the two public entry points on
    // a byte-for-byte identical locator, which the cook path cannot express because
    // the extension is part of the locator. The glTF side of the tag reuse is pinned
    // in GltfCookTests, which can reach that cooker; here the point is that the media
    // derivation is a pure function of (locator, kind) and never of call order.
    constexpr std::string_view locator = "shared/name";
    const auto mediaTexture = deriveTextureMediaAssetId(locator);
    const auto mediaAudio = deriveAudioMediaAssetId(locator);
    ASSERT_TRUE(mediaTexture) << mediaTexture.error().message;
    ASSERT_TRUE(mediaAudio) << mediaAudio.error().message;
    EXPECT_NE(*mediaTexture, *mediaAudio)
        << "one locator produced the same id for both media kinds";

    // Stable across repeated calls: a project reopened later must resolve the same
    // ids, so the derivation cannot depend on anything but its inputs.
    const auto textureAgain = deriveTextureMediaAssetId(locator);
    const auto audioAgain = deriveAudioMediaAssetId(locator);
    ASSERT_TRUE(textureAgain) << textureAgain.error().message;
    ASSERT_TRUE(audioAgain) << audioAgain.error().message;
    EXPECT_EQ(*mediaTexture, *textureAgain);
    EXPECT_EQ(*mediaAudio, *audioAgain);
}

// Long locators must stay distinct where they differ only past the point a fixed
// buffer would cut. A derivation that hashed a truncated prefix would return the
// same id for both, and the collision would surface only on deep project trees --
// the ones least likely to appear in a fixture.
//
// Exercised through the derivation entry point rather than by cooking real files:
// a locator long enough to be interesting exceeds Windows MAX_PATH for the fixture
// root, so a file-based version would only ever prove that the filesystem refused
// the path. Locator length is what the derivation consumes, and the source-import
// limit (MaxPathBytes) allows locators far longer than MAX_PATH permits on disk.
TEST_F(MediaCookTests, LongLocatorsDifferingOnlyNearTheEndStayDistinct)
{
    const std::string prefix = "textures/" + std::string(240, 'p') + "/asset_name_";
    const auto left = deriveTextureMediaAssetId(prefix + "a.png");
    const auto right = deriveTextureMediaAssetId(prefix + "b.png");
    ASSERT_TRUE(left) << left.error().message;
    ASSERT_TRUE(right) << right.error().message;
    EXPECT_NE(*left, *right)
        << "two long locators differing only near the end collided; the derivation "
           "is hashing a truncated prefix";

    // The same locator must also be stable, so the inequality above is a real
    // difference rather than the derivation returning something unrepeatable.
    const auto repeated = deriveTextureMediaAssetId(prefix + "a.png");
    ASSERT_TRUE(repeated) << repeated.error().message;
    EXPECT_EQ(*left, *repeated);
}

TEST_F(MediaCookTests, WavCooksAudioClipAndRejectsNonWavBytes)
{
    cacheRootUtf8();
    const auto wav = tinyWavBytes();
    const auto path = writeSource("audio/step.wav", wav);

    auto cooked = cookAudioFileToCatalogSourceResult(
        path, AssetFormat::TargetPlatform::WindowsX64, captureConfig());
    ASSERT_TRUE(cooked) << cooked.error().message;
    ASSERT_EQ(cooked->request.assets.size(), 1U);
    const auto& clip = cooked->request.assets.front();
    EXPECT_EQ(clip.assetKind, AssetFormat::AssetKind::AudioClip);
    auto payload = AssetFormat::parseAudioClipPayload(clip.payload);
    ASSERT_TRUE(payload) << payload.error().message;
    EXPECT_EQ(payload->channels, 1U);
    EXPECT_EQ(payload->sampleRate, 8000U);
    EXPECT_EQ(payload->frameCount, 4U);

    ASSERT_EQ(cooked->sourceImports.units.size(), 1U);
    EXPECT_EQ(cooked->sourceImports.units.front().importerKind, SourceImporterKind::Audio);
    EXPECT_EQ(cooked->sourceImports.units.front().importerVersion, 2U);

    const auto png = tinyPngBytes();
    const auto bogus = writeSource("audio/not_audio.wav", png);
    auto rejected = cookAudioFileToCatalogSourceResult(
        bogus, AssetFormat::TargetPlatform::WindowsX64, captureConfig());
    EXPECT_FALSE(rejected);
}

TEST_F(MediaCookTests, SourceOutsideRootFailsClosed)
{
    cacheRootUtf8();
    const auto png = tinyPngBytes();
    const auto outside =
        std::filesystem::temp_directory_path() / "tina_media_cook_outside.png";
    auto status = Core::writeFile(toUtf8(outside), png);
    ASSERT_TRUE(status) << status.error().message;

    auto rejected = cookTextureFileToCatalogSourceResult(
        toUtf8(outside), AssetFormat::TargetPlatform::WindowsX64, captureConfig());
    EXPECT_FALSE(rejected);

    std::error_code cleanupError;
    std::filesystem::remove(outside, cleanupError);
}

} // namespace
} // namespace Tina::Asset
