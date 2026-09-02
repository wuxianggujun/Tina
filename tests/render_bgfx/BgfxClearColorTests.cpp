#include "BgfxClearColor.hpp"

#include <tina/render/RenderScene.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

namespace Tina::Render::Bgfx {
namespace {

// The reference sRGB transfer function, written out independently of the implementation so
// this is a check against the standard rather than against itself.
[[nodiscard]] float referenceLinearFromSrgbByte(int byteValue)
{
    const float channel = static_cast<float>(byteValue) / 255.0F;
    return channel <= 0.04045F ? channel / 12.92F
                               : std::pow((channel + 0.055F) / 1.055F, 2.4F);
}

// ADR 0042 promises the engine default survives a linear -> sRGB -> u8 round trip
// byte-exactly, which is the whole reason existing samples and gates do not shift.
TEST(BgfxClearColorTest, TheDefaultSceneClearColorEncodesBackToItsOriginalSrgbBytes)
{
    EXPECT_EQ(packClearRgba(DefaultSceneClearColor), 0x102a43ffU);
}

TEST(BgfxClearColorTest, EverySrgbByteSurvivesTheRoundTrip)
{
    for (int byteValue = 0; byteValue <= 255; ++byteValue)
    {
        const float linear = referenceLinearFromSrgbByte(byteValue);
        EXPECT_EQ(static_cast<int>(encodeSrgbComponent(linear)), byteValue)
            << "byte " << byteValue << " did not survive";
    }
}

TEST(BgfxClearColorTest, TheLinearSegmentIsUsedBelowTheTransferFunctionKnee)
{
    // Below the knee the curve is linear, so the power form would return a visibly
    // different byte. 0.0F and full black must both land exactly on 0.
    EXPECT_EQ(static_cast<int>(encodeSrgbComponent(0.0F)), 0);
    EXPECT_EQ(static_cast<int>(encodeSrgbComponent(1.0F)), 255);
    // 0.0031308 is the knee; just inside it the linear segment gives 12.92 * x.
    EXPECT_EQ(static_cast<int>(encodeSrgbComponent(0.001F)),
              static_cast<int>(std::lround(0.001F * 12.92F * 255.0F)));
}

TEST(BgfxClearColorTest, OutOfRangeAndNonFiniteComponentsClampInsteadOfWrapping)
{
    // The scene layer rejects these, so reaching the backend means a future caller
    // bypassed validation. Clamping keeps that a wrong colour rather than a wrapped byte.
    constexpr RenderLinearColor overRange{
        .red = 4.0F, .green = -2.0F, .blue = 1.0F, .alpha = 9.0F};
    EXPECT_EQ(packClearRgba(overRange), 0xff00ffffU);

    const RenderLinearColor notFinite{
        .red = std::numeric_limits<float>::quiet_NaN(),
        .green = std::numeric_limits<float>::infinity(),
        .blue = -std::numeric_limits<float>::infinity(),
        .alpha = 1.0F,
    };
    // std::clamp with NaN returns the value itself, so only the finite channels are
    // asserted; the point is that the call is defined and does not trap.
    const u32 packed = packClearRgba(notFinite);
    EXPECT_EQ((packed >> 16U) & 0xffU, 255U);
    EXPECT_EQ((packed >> 8U) & 0xffU, 0U);
    EXPECT_EQ(packed & 0xffU, 255U);
}

TEST(BgfxClearColorTest, ChannelsLandInTheOrderBgfxExpects)
{
    // 0xRRGGBBAA. A transposed pair here would tint every background in the engine.
    constexpr RenderLinearColor red{.red = 1.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};
    constexpr RenderLinearColor green{.red = 0.0F, .green = 1.0F, .blue = 0.0F, .alpha = 1.0F};
    constexpr RenderLinearColor blue{.red = 0.0F, .green = 0.0F, .blue = 1.0F, .alpha = 1.0F};
    EXPECT_EQ(packClearRgba(red), 0xff0000ffU);
    EXPECT_EQ(packClearRgba(green), 0x00ff00ffU);
    EXPECT_EQ(packClearRgba(blue), 0x0000ffffU);
}

TEST(BgfxClearColorTest, AlphaIsQuantizedWithoutTheTransferFunction)
{
    // Alpha is coverage, not light. Encoding it would push 0.5 to 188 instead of 128.
    constexpr RenderLinearColor halfAlpha{
        .red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.5F};
    EXPECT_EQ(packClearRgba(halfAlpha) & 0xffU, 128U);
}

} // namespace
} // namespace Tina::Render::Bgfx
