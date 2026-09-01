#include <tina/asset/TextureMipChain.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <new>

namespace Tina::Asset {
namespace {

inline constexpr Core::u32 ChannelsPerPixel = 4;

// Exact sRGB electro-optical transfer function. The 2.2-power approximation is not
// used: the GPU's sRGB sampler applies this curve, so approximating here would make a
// generated mip disagree with its own base level about what a stored byte means.
[[nodiscard]] float srgbByteToLinear(Core::u8 encoded) noexcept
{
    const float value = static_cast<float>(encoded) / 255.0F;
    if (value <= 0.04045F)
    {
        return value / 12.92F;
    }
    return std::pow((value + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] Core::u8 linearToSrgbByte(float linear) noexcept
{
    const float clamped = linear < 0.0F ? 0.0F : (linear > 1.0F ? 1.0F : linear);
    const float encoded = clamped <= 0.0031308F
                              ? clamped * 12.92F
                              : 1.055F * std::pow(clamped, 1.0F / 2.4F) - 0.055F;
    return static_cast<Core::u8>(encoded * 255.0F + 0.5F);
}

[[nodiscard]] Core::u8 unitToByte(float value) noexcept
{
    const float clamped = value < 0.0F ? 0.0F : (value > 1.0F ? 1.0F : value);
    return static_cast<Core::u8>(clamped * 255.0F + 0.5F);
}

[[nodiscard]] Core::u16 nextMipExtent(Core::u16 extent) noexcept
{
    return extent > 1U ? static_cast<Core::u16>(extent / 2U) : static_cast<Core::u16>(1);
}

// Source pixels a destination pixel covers along one axis. Halving an odd extent floors,
// which would leave the trailing row or column contributing to no destination pixel at
// all and drop that edge content from every smaller level. The last destination pixel
// takes a third tap instead.
struct AxisWindow final {
    Core::u32 start = 0;
    Core::u32 count = 0;
};

[[nodiscard]] AxisWindow axisWindow(Core::u32 destinationIndex, Core::u16 sourceExtent,
                                    Core::u16 destinationExtent) noexcept
{
    if (sourceExtent == destinationExtent)
    {
        return AxisWindow{.start = destinationIndex, .count = 1};
    }
    const Core::u32 start = destinationIndex * 2U;
    const bool isLast = destinationIndex + 1U == static_cast<Core::u32>(destinationExtent);
    const bool sourceIsOdd = (sourceExtent % 2U) != 0U;
    return AxisWindow{.start = start, .count = (isLast && sourceIsOdd) ? 3U : 2U};
}

void downsampleRgba8(std::span<const std::byte> source, Core::u16 sourceWidth,
                     Core::u16 sourceHeight, std::span<std::byte> destination,
                     Core::u16 destinationWidth, Core::u16 destinationHeight,
                     bool isSrgb) noexcept
{
    for (Core::u32 y = 0; y < destinationHeight; ++y)
    {
        const AxisWindow rows = axisWindow(y, sourceHeight, destinationHeight);
        for (Core::u32 x = 0; x < destinationWidth; ++x)
        {
            const AxisWindow columns = axisWindow(x, sourceWidth, destinationWidth);

            float alphaSum = 0.0F;
            float weightedRed = 0.0F;
            float weightedGreen = 0.0F;
            float weightedBlue = 0.0F;
            Core::u32 sampleCount = 0;

            for (Core::u32 row = 0; row < rows.count; ++row)
            {
                const Core::u32 sourceY = rows.start + row;
                for (Core::u32 column = 0; column < columns.count; ++column)
                {
                    const Core::u32 sourceX = columns.start + column;
                    const std::size_t offset =
                        (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) *
                        ChannelsPerPixel;

                    const auto red = static_cast<Core::u8>(source[offset]);
                    const auto green = static_cast<Core::u8>(source[offset + 1U]);
                    const auto blue = static_cast<Core::u8>(source[offset + 2U]);
                    const auto alphaByte = static_cast<Core::u8>(source[offset + 3U]);

                    // Alpha is linear even in an sRGB texture: it is coverage, not colour,
                    // so running it through the transfer function would bend blending.
                    const float alpha = static_cast<float>(alphaByte) / 255.0F;
                    const float linearRed = isSrgb ? srgbByteToLinear(red)
                                                   : static_cast<float>(red) / 255.0F;
                    const float linearGreen = isSrgb ? srgbByteToLinear(green)
                                                     : static_cast<float>(green) / 255.0F;
                    const float linearBlue = isSrgb ? srgbByteToLinear(blue)
                                                    : static_cast<float>(blue) / 255.0F;

                    alphaSum += alpha;
                    weightedRed += linearRed * alpha;
                    weightedGreen += linearGreen * alpha;
                    weightedBlue += linearBlue * alpha;
                    ++sampleCount;
                }
            }

            const std::size_t destinationOffset =
                (static_cast<std::size_t>(y) * destinationWidth + x) * ChannelsPerPixel;
            const float averageAlpha =
                sampleCount == 0 ? 0.0F : alphaSum / static_cast<float>(sampleCount);

            if (alphaSum <= 0.0F)
            {
                // Every covered pixel is fully transparent, so there is no colour to carry.
                // Dividing by the zero alpha would produce a NaN that encodes as noise.
                destination[destinationOffset] = std::byte{0};
                destination[destinationOffset + 1U] = std::byte{0};
                destination[destinationOffset + 2U] = std::byte{0};
                destination[destinationOffset + 3U] = std::byte{0};
                continue;
            }

            const float linearRed = weightedRed / alphaSum;
            const float linearGreen = weightedGreen / alphaSum;
            const float linearBlue = weightedBlue / alphaSum;

            destination[destinationOffset] =
                static_cast<std::byte>(isSrgb ? linearToSrgbByte(linearRed) : unitToByte(linearRed));
            destination[destinationOffset + 1U] = static_cast<std::byte>(
                isSrgb ? linearToSrgbByte(linearGreen) : unitToByte(linearGreen));
            destination[destinationOffset + 2U] = static_cast<std::byte>(
                isSrgb ? linearToSrgbByte(linearBlue) : unitToByte(linearBlue));
            destination[destinationOffset + 3U] = static_cast<std::byte>(unitToByte(averageAlpha));
        }
    }
}

} // namespace

std::span<const AssetFormat::Texture2DLevelDesc> Texture2DMipChainRgba8::fillLevelDescs(
    std::array<AssetFormat::Texture2DLevelDesc, AssetFormat::Texture2DWire::MaxLevelCount>& storage)
    const noexcept
{
    const std::span<const std::byte> owned{bytes};
    for (Core::u8 index = 0; index < levelCount; ++index)
    {
        const Level& level = levels[index];
        storage[index] = AssetFormat::Texture2DLevelDesc{
            .width = level.width,
            .height = level.height,
            .bytes = owned.subspan(level.byteOffset, level.byteSize),
        };
    }
    return std::span<const AssetFormat::Texture2DLevelDesc>{storage}.first(levelCount);
}

Core::Result<Texture2DMipChainRgba8>
buildTexture2DMipChainRgba8(Core::u16 width, Core::u16 height,
                            std::span<const std::byte> rgba8BasePixels,
                            AssetFormat::Texture2DColorSpace colorSpace)
{
    if (width == 0 || height == 0 || width > AssetFormat::Texture2DWire::MaxDimension ||
        height > AssetFormat::Texture2DWire::MaxDimension)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "texture mip chain base extent is out of range");
    }
    if (colorSpace != AssetFormat::Texture2DColorSpace::Linear &&
        colorSpace != AssetFormat::Texture2DColorSpace::Srgb)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "texture mip chain colour space is unknown");
    }

    const auto baseBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                           ChannelsPerPixel;
    if (rgba8BasePixels.size() != baseBytes)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "texture mip chain base pixels do not match the base extent");
    }

    const Core::u8 levelCount = AssetFormat::texture2DFullMipLevelCount(width, height);
    if (levelCount == 0 || levelCount > AssetFormat::Texture2DWire::MaxLevelCount)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "texture mip chain level count is out of range");
    }

    std::size_t totalBytes = 0;
    {
        Core::u16 levelWidth = width;
        Core::u16 levelHeight = height;
        for (Core::u8 index = 0; index < levelCount; ++index)
        {
            totalBytes += static_cast<std::size_t>(levelWidth) *
                          static_cast<std::size_t>(levelHeight) * ChannelsPerPixel;
            levelWidth = nextMipExtent(levelWidth);
            levelHeight = nextMipExtent(levelHeight);
        }
    }

    Texture2DMipChainRgba8 chain{};
    try
    {
        chain.bytes.resize(totalBytes);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "texture mip chain allocation failed");
    }

    const bool isSrgb = colorSpace == AssetFormat::Texture2DColorSpace::Srgb;
    const std::span<std::byte> owned{chain.bytes};

    Core::u16 levelWidth = width;
    Core::u16 levelHeight = height;
    Core::u32 writeOffset = 0;
    Core::u32 previousOffset = 0;
    Core::u16 previousWidth = 0;
    Core::u16 previousHeight = 0;

    for (Core::u8 index = 0; index < levelCount; ++index)
    {
        const auto levelBytes = static_cast<Core::u32>(static_cast<std::size_t>(levelWidth) *
                                                       static_cast<std::size_t>(levelHeight) *
                                                       ChannelsPerPixel);
        const std::span<std::byte> destination = owned.subspan(writeOffset, levelBytes);

        if (index == 0)
        {
            std::copy(rgba8BasePixels.begin(), rgba8BasePixels.end(), destination.begin());
        } else
        {
            // Each level is filtered from the level above rather than from the base, so the
            // cost stays linear in total pixels and every level sees its parent's already
            // averaged coverage.
            const std::span<const std::byte> source =
                std::span<const std::byte>{chain.bytes}.subspan(
                    previousOffset, static_cast<std::size_t>(previousWidth) *
                                        static_cast<std::size_t>(previousHeight) *
                                        ChannelsPerPixel);
            downsampleRgba8(source, previousWidth, previousHeight, destination, levelWidth,
                            levelHeight, isSrgb);
        }

        chain.levels[index] = Texture2DMipChainRgba8::Level{
            .width = levelWidth,
            .height = levelHeight,
            .byteOffset = writeOffset,
            .byteSize = levelBytes,
        };

        previousOffset = writeOffset;
        previousWidth = levelWidth;
        previousHeight = levelHeight;
        writeOffset += levelBytes;
        levelWidth = nextMipExtent(levelWidth);
        levelHeight = nextMipExtent(levelHeight);
    }

    chain.levelCount = levelCount;
    return chain;
}

} // namespace Tina::Asset
