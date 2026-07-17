#include "GlfwInputTranslation.hpp"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace Tina::Platform::Detail {
namespace {

using namespace std::literals;

TEST(GlfwInputTranslationTests, MapsRepresentativeKeyboardControls)
{
    constexpr std::array mappings{
        std::pair{GLFW_KEY_SPACE, Key::Space},
        std::pair{GLFW_KEY_0, Key::Digit0},
        std::pair{GLFW_KEY_9, Key::Digit9},
        std::pair{GLFW_KEY_A, Key::A},
        std::pair{GLFW_KEY_Z, Key::Z},
        std::pair{GLFW_KEY_ESCAPE, Key::Escape},
        std::pair{GLFW_KEY_LEFT, Key::Left},
        std::pair{GLFW_KEY_F12, Key::F12},
        std::pair{GLFW_KEY_KP_ENTER, Key::KeypadEnter},
        std::pair{GLFW_KEY_LEFT_CONTROL, Key::LeftControl},
        std::pair{GLFW_KEY_RIGHT_SUPER, Key::RightSuper},
        std::pair{GLFW_KEY_MENU, Key::Menu},
    };

    for (const auto& [nativeKey, tinaKey] : mappings)
    {
        EXPECT_EQ(translateGlfwKey(nativeKey), tinaKey);
    }
}

TEST(GlfwInputTranslationTests, IgnoresUnsupportedKeyboardControls)
{
    EXPECT_EQ(translateGlfwKey(GLFW_KEY_UNKNOWN), Key::Unknown);
    EXPECT_EQ(translateGlfwKey(GLFW_KEY_F13), Key::Unknown);
    EXPECT_EQ(translateGlfwKey((std::numeric_limits<int>::max)()), Key::Unknown);
}

TEST(GlfwInputTranslationTests, MapsAllSupportedPointerButtons)
{
    constexpr std::array expected{
        PointerButton::Primary, PointerButton::Secondary, PointerButton::Middle,  PointerButton::Button4,
        PointerButton::Button5, PointerButton::Button6,   PointerButton::Button7, PointerButton::Button8,
    };
    for (int nativeButton = GLFW_MOUSE_BUTTON_1; nativeButton <= GLFW_MOUSE_BUTTON_8; ++nativeButton)
    {
        ASSERT_TRUE(translateGlfwPointerButton(nativeButton).has_value());
        EXPECT_EQ(*translateGlfwPointerButton(nativeButton), expected[static_cast<usize>(nativeButton)]);
    }
    EXPECT_FALSE(translateGlfwPointerButton(GLFW_MOUSE_BUTTON_8 + 1).has_value());
}

TEST(GlfwInputTranslationTests, EncodesValidUnicodeScalarsAsStrictUtf8)
{
    const auto ascii = encodeUtf8Codepoint(U'$');
    const auto twoBytes = encodeUtf8Codepoint(U'¢');
    const auto chinese = encodeUtf8Codepoint(U'中');
    const auto emoji = encodeUtf8Codepoint(U'🙂');

    ASSERT_TRUE(ascii.has_value());
    ASSERT_TRUE(twoBytes.has_value());
    ASSERT_TRUE(chinese.has_value());
    ASSERT_TRUE(emoji.has_value());
    EXPECT_EQ(ascii->view(), "$"sv);
    EXPECT_EQ(twoBytes->view(), "\xC2\xA2"sv);
    EXPECT_EQ(chinese->view(), "\xE4\xB8\xAD"sv);
    EXPECT_EQ(emoji->view(), "\xF0\x9F\x99\x82"sv);
}

TEST(GlfwInputTranslationTests, RejectsNulSurrogatesAndOutOfRangeCodepoints)
{
    EXPECT_FALSE(encodeUtf8Codepoint(0).has_value());
    EXPECT_FALSE(encodeUtf8Codepoint(0xD800U).has_value());
    EXPECT_FALSE(encodeUtf8Codepoint(0xDFFFU).has_value());
    EXPECT_FALSE(encodeUtf8Codepoint(0x110000U).has_value());
}

TEST(GlfwInputTranslationTests, RejectsOrphanRepeatButAcceptsHeldRepeatAndDigitalEdges)
{
    EXPECT_FALSE(shouldAcceptGlfwKeyAction(GLFW_REPEAT, false));
    EXPECT_TRUE(shouldAcceptGlfwKeyAction(GLFW_REPEAT, true));
    EXPECT_TRUE(shouldAcceptGlfwKeyAction(GLFW_PRESS, false));
    EXPECT_TRUE(shouldAcceptGlfwKeyAction(GLFW_RELEASE, true));
    EXPECT_FALSE(shouldAcceptGlfwKeyAction((std::numeric_limits<int>::max)(), true));
}

} // namespace
} // namespace Tina::Platform::Detail
