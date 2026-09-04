#include <tina/asset/AssetGpuShader.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetTypedViews.hpp>

#include <array>
#include <span>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::Result<Render::GpuShaderKind>
toGpuShaderKind(AssetFormat::ShaderKind kind) noexcept
{
    switch (kind)
    {
    case AssetFormat::ShaderKind::Sprite2D:
        return Render::GpuShaderKind::Sprite2D;
    case AssetFormat::ShaderKind::Mesh3D:
        return Render::GpuShaderKind::Mesh3D;
    case AssetFormat::ShaderKind::Invalid:
        break;
    }
    return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                         "cooked shader kind has no GPU equivalent");
}

[[nodiscard]] Core::Result<Render::GpuShaderBinaryProfile>
toGpuShaderProfile(AssetFormat::ShaderBinaryProfile profile) noexcept
{
    switch (profile)
    {
    case AssetFormat::ShaderBinaryProfile::Glsl120:
        return Render::GpuShaderBinaryProfile::Glsl120;
    case AssetFormat::ShaderBinaryProfile::SpirV:
        return Render::GpuShaderBinaryProfile::SpirV;
    case AssetFormat::ShaderBinaryProfile::Dxbc50:
        return Render::GpuShaderBinaryProfile::Dxbc50;
    case AssetFormat::ShaderBinaryProfile::Essl300:
        return Render::GpuShaderBinaryProfile::Essl300;
    case AssetFormat::ShaderBinaryProfile::Invalid:
        break;
    }
    return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                         "cooked shader binary profile has no GPU equivalent");
}

} // namespace

Core::Result<Render::GpuShaderId>
uploadShaderFromCooked(Render::IRenderDevice& device, const CookedAssetFile& shaderAsset)
{
    auto view = parseShaderFromCooked(shaderAsset);
    if (!view)
    {
        return Core::failure(std::move(view.error()).withContext("uploadShaderFromCooked", "parse"));
    }
    if (view->stage != AssetFormat::ShaderStage::Fragment)
    {
        return Core::failure(AssetErrorCode::CatalogEntryMismatch,
                             "cooked shader stage is not a supported fragment stage");
    }

    auto shaderKind = toGpuShaderKind(view->shaderKind);
    if (!shaderKind)
    {
        return Core::failure(std::move(shaderKind.error()));
    }

    std::array<Render::GpuShaderBinary, AssetFormat::ShaderWire::MaxBlobCount> binaries{};
    const auto blobs = view->blobs();
    for (std::size_t index = 0; index < blobs.size(); ++index)
    {
        auto profile = toGpuShaderProfile(blobs[index].profile);
        if (!profile)
        {
            return Core::failure(std::move(profile.error()));
        }
        binaries[index] = Render::GpuShaderBinary{
            .profile = *profile,
            .bytes = blobs[index].bytes,
        };
    }

    return device.createShader(Render::GpuShaderUploadDesc{
        .shaderKind = *shaderKind,
        .binaries = std::span<const Render::GpuShaderBinary>{binaries}.first(blobs.size()),
    });
}

} // namespace Tina::Asset
