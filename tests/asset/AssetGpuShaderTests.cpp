#include <tina/asset/AssetGpuShader.hpp>
#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CookedAssetFile.hpp>
#include <tina/asset_format/ShaderPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

struct CapturedShaderBinary final {
    Render::GpuShaderBinaryProfile profile = Render::GpuShaderBinaryProfile::Invalid;
    std::vector<std::byte> bytes{};
};

class CapturingShaderDevice final : public Render::IRenderDevice {
  public:
    [[nodiscard]] Core::Result<Render::RenderFrameSubmission> submitFrame(
        const Render::RenderFrame&) override
    {
        return Render::RenderFrameSubmission::SkippedSuspendedSurface();
    }

    [[nodiscard]] Core::Status present() override { return Core::success(); }
    [[nodiscard]] Render::RenderStatistics statistics() const noexcept override { return {}; }
    void shutdown() noexcept override {}

    [[nodiscard]] Core::Result<Render::GpuShaderId> createShader(
        const Render::GpuShaderUploadDesc& desc) override
    {
        // Skip validateShaderUploadDesc on purpose: this fixture pins that AssetGpuShader
        // forwards the cooked kind and blob table as-is. Shared upload policy lives in
        // ShaderUploadDescTest.
        kind = desc.shaderKind;
        binaries.clear();
        for (const Render::GpuShaderBinary& binary : desc.binaries)
        {
            binaries.push_back(CapturedShaderBinary{
                .profile = binary.profile,
                .bytes = std::vector<std::byte>(binary.bytes.begin(), binary.bytes.end()),
            });
        }
        return Render::GpuShaderId{1U, 1U};
    }

    Render::GpuShaderKind kind = Render::GpuShaderKind::Invalid;
    std::vector<CapturedShaderBinary> binaries{};
};

[[nodiscard]] Core::Result<CookedAssetFile> makeShaderFile(
    std::pmr::memory_resource& memory, Core::u8 seed, AssetFormat::ShaderKind kind,
    std::span<const AssetFormat::ShaderBlobDesc> blobs)
{
    auto cooked = AssetFormat::writeCookedShaderAsset(
        *Core::AssetId::fromBytes(idBytes(seed)),
        AssetFormat::ShaderPayloadDesc{.shaderKind = kind, .blobs = blobs});
    if (!cooked)
    {
        return Core::failure(std::move(cooked.error()));
    }
    return makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
}

TEST(AssetGpuShaderTests, UploadsTypedShaderToNullDevice)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array bytes{std::byte{0x01}, std::byte{0x02}};
    const std::array blobs{
        AssetFormat::ShaderBlobDesc{.profile = AssetFormat::ShaderBinaryProfile::Glsl120,
                                    .bytes = bytes},
        AssetFormat::ShaderBlobDesc{.profile = AssetFormat::ShaderBinaryProfile::SpirV,
                                    .bytes = bytes},
    };
    auto file = makeShaderFile(memory, 1U, AssetFormat::ShaderKind::Sprite2D, blobs);
    ASSERT_TRUE(file.has_value()) << file.error().message;

    auto device = Render::createNullRenderDevice(Render::RenderDeviceCreateParams{});
    ASSERT_TRUE(device.has_value());
    auto shader = uploadShaderFromCooked(**device, *file);
    ASSERT_TRUE(shader.has_value()) << shader.error().message;
    EXPECT_TRUE((*device)->validateShader(*shader).has_value());
}

TEST(AssetGpuShaderTests, CookedFieldsReachRenderWithoutLoss)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array glsl{std::byte{0x11}, std::byte{0x12}};
    const std::array spirv{std::byte{0x21}, std::byte{0x22}, std::byte{0x23}};
    const std::array dxbc{std::byte{0x31}};
    const std::array essl{std::byte{0x41}, std::byte{0x42}};
    const std::array blobs{
        AssetFormat::ShaderBlobDesc{.profile = AssetFormat::ShaderBinaryProfile::Glsl120,
                                    .bytes = glsl},
        AssetFormat::ShaderBlobDesc{.profile = AssetFormat::ShaderBinaryProfile::SpirV,
                                    .bytes = spirv},
        AssetFormat::ShaderBlobDesc{.profile = AssetFormat::ShaderBinaryProfile::Dxbc50,
                                    .bytes = dxbc},
        AssetFormat::ShaderBlobDesc{.profile = AssetFormat::ShaderBinaryProfile::Essl300,
                                    .bytes = essl},
    };
    // Mesh3D on purpose: this pins that the asset layer forwards whatever kind the payload names
    // instead of hardcoding Sprite2D. The capturing device deliberately skips
    // validateShaderUploadDesc, so what is measured here is translation fidelity, not upload policy.
    auto file = makeShaderFile(memory, 2U, AssetFormat::ShaderKind::Mesh3D, blobs);
    ASSERT_TRUE(file.has_value()) << file.error().message;

    CapturingShaderDevice device;
    auto shader = uploadShaderFromCooked(device, *file);
    ASSERT_TRUE(shader.has_value()) << shader.error().message;
    EXPECT_EQ(device.kind, Render::GpuShaderKind::Mesh3D);
    ASSERT_EQ(device.binaries.size(), blobs.size());
    EXPECT_EQ(device.binaries[0].profile, Render::GpuShaderBinaryProfile::Glsl120);
    EXPECT_EQ(device.binaries[0].bytes, std::vector<std::byte>(glsl.begin(), glsl.end()));
    EXPECT_EQ(device.binaries[1].profile, Render::GpuShaderBinaryProfile::SpirV);
    EXPECT_EQ(device.binaries[1].bytes, std::vector<std::byte>(spirv.begin(), spirv.end()));
    EXPECT_EQ(device.binaries[2].profile, Render::GpuShaderBinaryProfile::Dxbc50);
    EXPECT_EQ(device.binaries[2].bytes, std::vector<std::byte>(dxbc.begin(), dxbc.end()));
    EXPECT_EQ(device.binaries[3].profile, Render::GpuShaderBinaryProfile::Essl300);
    EXPECT_EQ(device.binaries[3].bytes, std::vector<std::byte>(essl.begin(), essl.end()));
}

TEST(AssetGpuShaderTests, RejectsMismatchedCookedAssetTypeVersion)
{
    const std::array bytes{std::byte{0x01}};
    const std::array blobs{
        AssetFormat::ShaderBlobDesc{.profile = AssetFormat::ShaderBinaryProfile::Glsl120,
                                    .bytes = bytes},
    };
    auto payload = AssetFormat::writeShaderPayloadBytes(
        AssetFormat::ShaderPayloadDesc{.shaderKind = AssetFormat::ShaderKind::Sprite2D, .blobs = blobs});
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    // The next version, not the previous one: Shader schema is still v1, and v0 is rejected as an
    // invalid identity by both writeCookedAssetBytes and parseCookedAssetHeader, so it can never
    // reach this gate. A future version is the reachable mismatch -- an asset cooked by a newer
    // engine and read by this one -- and it exercises the same header check.
    auto cooked = AssetFormat::writeCookedAssetBytes(AssetFormat::CookedAssetWriteDesc{
        .assetKind = AssetFormat::AssetKind::Shader,
        .assetTypeVersion = AssetFormat::ShaderWire::SchemaVersion + 1U,
        .assetId = *Core::AssetId::fromBytes(idBytes(3U)),
        .payload = *payload,
    });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    std::pmr::unsynchronized_pool_resource memory;
    auto file = makeCookedAssetFileFromBytes(
        std::pmr::vector<std::byte>(cooked->begin(), cooked->end(), &memory),
        CookedAssetFileLoadConfig{.memoryResource = &memory});
    ASSERT_TRUE(file.has_value()) << file.error().message;

    CapturingShaderDevice device;
    auto shader = uploadShaderFromCooked(device, *file);
    ASSERT_FALSE(shader.has_value());
    EXPECT_EQ(shader.error().code, AssetErrorCode::CatalogEntryMismatch);
    EXPECT_TRUE(device.binaries.empty());
}

} // namespace
} // namespace Tina::Asset
