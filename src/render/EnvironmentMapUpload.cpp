#include <tina/render/RenderDevice.hpp>

#include <limits>

namespace Tina::Render {
namespace {

[[nodiscard]] constexpr u16 fullMipCount(u16 faceSize) noexcept
{
    u16 count = 0;
    while (faceSize != 0)
    {
        ++count;
        faceSize = static_cast<u16>(faceSize / 2U);
    }
    return count;
}

} // namespace

Core::Status validateEnvironmentMapUploadDesc(const EnvironmentMapUploadDesc& desc) noexcept
{
    if (desc.diffuseFaceSize == 0 || desc.specularFaceSize == 0 ||
        desc.brdfWidth == 0 || desc.brdfHeight == 0)
    {
        return Core::failure(RenderErrorCode::InvalidEnvironmentMapUpload,
                             "EnvironmentMap dimensions must be non-zero");
    }
    if (desc.specularMipCount != fullMipCount(desc.specularFaceSize))
    {
        return Core::failure(RenderErrorCode::InvalidEnvironmentMapUpload,
                             "EnvironmentMap specular mip count must describe the complete chain");
    }

    constexpr u64 FaceCount = 6U;
    constexpr u64 Rgba16FloatBytesPerPixel = 8U;
    constexpr u64 Rg16FloatBytesPerPixel = 4U;
    constexpr u64 MaximumUploadBytes = (std::numeric_limits<u32>::max)();

    const u64 diffuseBytes = static_cast<u64>(desc.diffuseFaceSize) *
                             desc.diffuseFaceSize * FaceCount * Rgba16FloatBytesPerPixel;
    u64 specularBytes = 0;
    u16 mipFaceSize = desc.specularFaceSize;
    for (u16 mip = 0; mip < desc.specularMipCount; ++mip)
    {
        specularBytes += static_cast<u64>(mipFaceSize) * mipFaceSize * FaceCount *
                         Rgba16FloatBytesPerPixel;
        mipFaceSize = static_cast<u16>(mipFaceSize / 2U);
    }
    const u64 brdfBytes = static_cast<u64>(desc.brdfWidth) * desc.brdfHeight *
                          Rg16FloatBytesPerPixel;
    if (diffuseBytes > MaximumUploadBytes || specularBytes > MaximumUploadBytes ||
        brdfBytes > MaximumUploadBytes)
    {
        return Core::failure(RenderErrorCode::InvalidEnvironmentMapUpload,
                             "EnvironmentMap image data exceeds backend upload limits");
    }
    if (desc.diffuseRgba16FloatPixels.size() != diffuseBytes ||
        desc.specularRgba16FloatPixels.size() != specularBytes ||
        desc.brdfRg16FloatPixels.size() != brdfBytes)
    {
        return Core::failure(RenderErrorCode::InvalidEnvironmentMapUpload,
                             "EnvironmentMap image byte counts do not match its dimensions");
    }
    return Core::success();
}

} // namespace Tina::Render
