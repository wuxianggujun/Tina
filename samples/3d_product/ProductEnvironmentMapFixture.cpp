#include "ProductEnvironmentMapFixture.hpp"

#include <tina/asset_format/EnvironmentMapPayload.hpp>

#include <array>

namespace Tina::Sample3D {
namespace {

using Core::u16;
using Core::u32;

void appendHalf(std::vector<std::byte>& pixels, u16 bits)
{
    pixels.push_back(static_cast<std::byte>(bits & 0xFFU));
    pixels.push_back(static_cast<std::byte>((bits >> 8U) & 0xFFU));
}

void appendRgba16Float(std::vector<std::byte>& pixels, const std::array<u16, 4>& rgba)
{
    for (const u16 channel : rgba)
    {
        appendHalf(pixels, channel);
    }
}

void appendRg16Float(std::vector<std::byte>& pixels, const std::array<u16, 2>& rg)
{
    for (const u16 channel : rg)
    {
        appendHalf(pixels, channel);
    }
}

} // namespace

Core::AssetId productEnvironmentMapAssetId() noexcept
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(0x3DU);
    bytes[1] = static_cast<std::byte>(0x49U); // I
    bytes[2] = static_cast<std::byte>(0x42U); // B
    bytes[3] = static_cast<std::byte>(0x4CU); // L
    bytes[14] = static_cast<std::byte>(0xFFU);
    bytes[15] = static_cast<std::byte>(0xFDU);
    return *Core::AssetId::fromBytes(bytes);
}

Core::Result<std::vector<std::byte>> makeProductEnvironmentMapPayload()
{
    // Exact IEEE-754 binary16 values keep the fixture deterministic without a
    // source-image decoder or runtime convolution path.
    constexpr u16 HOne = 0x3C00U;
    constexpr u16 HSevenEighths = 0x3B00U;
    constexpr u16 HThreeQuarters = 0x3A00U;
    constexpr u16 HOneHalf = 0x3800U;
    constexpr u16 HThreeEighths = 0x3600U;
    constexpr u16 HOneQuarter = 0x3400U;
    constexpr u16 HOneEighth = 0x3000U;
    constexpr u16 HOneSixteenth = 0x2C00U;
    constexpr u16 HOneThirtySecond = 0x2800U;

    constexpr std::array<std::array<u16, 4>, 6> DiffuseFaces{{
        {HOneQuarter, HOneSixteenth, HOneThirtySecond, HOne},
        {HOneThirtySecond, HOneSixteenth, HOneQuarter, HOne},
        {HThreeEighths, HThreeEighths, HOneHalf, HOne},
        {HOneThirtySecond, HOneThirtySecond, HOneThirtySecond, HOne},
        {HOneEighth, HOneQuarter, HOneEighth, HOne},
        {HOneQuarter, HOneSixteenth, HOneQuarter, HOne},
    }};
    constexpr std::array<std::array<std::array<u16, 4>, 6>, 3> SpecularMipFaces{{
        {{
            {HOne, HOneQuarter, HOneEighth, HOne},
            {HOneEighth, HOneQuarter, HOne, HOne},
            {HThreeQuarters, HSevenEighths, HOne, HOne},
            {HOneSixteenth, HOneSixteenth, HOneSixteenth, HOne},
            {HOneQuarter, HOne, HThreeEighths, HOne},
            {HSevenEighths, HOneQuarter, HSevenEighths, HOne},
        }},
        {{
            {HThreeQuarters, HOneQuarter, HOneEighth, HOne},
            {HOneEighth, HOneQuarter, HThreeQuarters, HOne},
            {HOneHalf, HThreeQuarters, HSevenEighths, HOne},
            {HOneEighth, HOneEighth, HOneEighth, HOne},
            {HOneQuarter, HThreeQuarters, HThreeEighths, HOne},
            {HThreeQuarters, HOneQuarter, HThreeQuarters, HOne},
        }},
        {{
            {HThreeEighths, HOneQuarter, HOneQuarter, HOne},
            {HOneQuarter, HOneQuarter, HThreeEighths, HOne},
            {HThreeEighths, HOneHalf, HOneHalf, HOne},
            {HOneQuarter, HOneQuarter, HOneQuarter, HOne},
            {HOneQuarter, HThreeEighths, HOneQuarter, HOne},
            {HThreeEighths, HOneQuarter, HThreeEighths, HOne},
        }},
    }};
    constexpr std::array<std::array<u16, 2>, 16> BrdfLut{{
        {HOneHalf, HOneHalf}, {HThreeEighths, HOneHalf},
        {HOneQuarter, HThreeEighths}, {HOneEighth, HOneQuarter},
        {HThreeQuarters, HOneQuarter}, {HOneHalf, HOneQuarter},
        {HThreeEighths, HOneEighth}, {HOneQuarter, HOneEighth},
        {HSevenEighths, HOneEighth}, {HThreeQuarters, HOneEighth},
        {HOneHalf, HOneSixteenth}, {HThreeEighths, HOneSixteenth},
        {HOne, HOneThirtySecond}, {HSevenEighths, HOneThirtySecond},
        {HThreeQuarters, HOneThirtySecond}, {HOneHalf, HOneThirtySecond},
    }};
    static_assert(SpecularMipFaces.size() == ProductEnvironmentSpecularMipCount);
    static_assert(BrdfLut.size() == ProductEnvironmentBrdfSize * ProductEnvironmentBrdfSize);

    std::vector<std::byte> diffuse;
    diffuse.reserve(static_cast<std::size_t>(ProductEnvironmentDiffuseFaceSize) *
                    ProductEnvironmentDiffuseFaceSize * AssetFormat::EnvironmentMapWire::FaceCount *
                    AssetFormat::EnvironmentMapWire::Rgba16FloatBytesPerPixel);
    const u32 diffuseFaceTexels =
        static_cast<u32>(ProductEnvironmentDiffuseFaceSize) * ProductEnvironmentDiffuseFaceSize;
    for (const auto& face : DiffuseFaces)
    {
        for (u32 pixel = 0; pixel < diffuseFaceTexels; ++pixel)
        {
            appendRgba16Float(diffuse, face);
        }
    }

    std::vector<std::byte> specular;
    u16 mipFaceSize = ProductEnvironmentSpecularFaceSize;
    for (const auto& mipFaces : SpecularMipFaces)
    {
        const u32 mipFaceTexels = static_cast<u32>(mipFaceSize) * mipFaceSize;
        for (const auto& face : mipFaces)
        {
            for (u32 pixel = 0; pixel < mipFaceTexels; ++pixel)
            {
                appendRgba16Float(specular, face);
            }
        }
        mipFaceSize = static_cast<u16>(mipFaceSize / 2U);
    }

    std::vector<std::byte> brdf;
    brdf.reserve(BrdfLut.size() * AssetFormat::EnvironmentMapWire::Rg16FloatBytesPerPixel);
    for (const auto& texel : BrdfLut)
    {
        appendRg16Float(brdf, texel);
    }

    return AssetFormat::writeEnvironmentMapPayloadBytes(AssetFormat::EnvironmentMapPayloadDesc{
        .radiancePixelFormat = AssetFormat::EnvironmentMapRadiancePixelFormat::Rgba16Float,
        .brdfPixelFormat = AssetFormat::EnvironmentMapBrdfPixelFormat::Rg16Float,
        .diffuseFaceSize = ProductEnvironmentDiffuseFaceSize,
        .specularFaceSize = ProductEnvironmentSpecularFaceSize,
        .specularMipCount = ProductEnvironmentSpecularMipCount,
        .brdfWidth = ProductEnvironmentBrdfSize,
        .brdfHeight = ProductEnvironmentBrdfSize,
        .diffusePixels = diffuse,
        .specularPixels = specular,
        .brdfPixels = brdf,
    });
}

} // namespace Tina::Sample3D
