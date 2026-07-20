#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>

#include <span>

namespace Tina::Tests {

TEST(FreeTypeTextRasterizerTests, CreateRejectsEmptyFontBytesAndInvalidCapacity)
{
    auto rasterizerResult = UI::createFreeTypeTextRasterizer(
        UI::UITextRasterizerCapacity{.faceCapacity = 1});
    ASSERT_TRUE(rasterizerResult.has_value())
        << (rasterizerResult ? "" : rasterizerResult.error().message);
    std::unique_ptr<UI::IUITextRasterizer> rasterizer = std::move(*rasterizerResult);

    auto emptyFace = rasterizer->openFace({});
    ASSERT_FALSE(emptyFace.has_value());
    EXPECT_EQ(emptyFace.error().code, UI::UIErrorCode::InvalidFont);

    const std::byte junk[4]{
        std::byte{0},
        std::byte{1},
        std::byte{2},
        std::byte{3},
    };
    auto badFace = rasterizer->openFace(std::span<const std::byte>(junk, 4));
    ASSERT_FALSE(badFace.has_value());
    EXPECT_EQ(badFace.error().code, UI::UIErrorCode::InvalidFont);

    auto invalidCapacity = UI::createFreeTypeTextRasterizer(
        UI::UITextRasterizerCapacity{.faceCapacity = 0});
    ASSERT_FALSE(invalidCapacity.has_value());
    EXPECT_EQ(invalidCapacity.error().code, UI::UIErrorCode::InvalidContextConfig);
}

} // namespace Tina::Tests
