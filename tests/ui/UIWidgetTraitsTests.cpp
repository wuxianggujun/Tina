#include <gtest/gtest.h>

#include "detail/UIWidgetTraits.hpp"

#include <array>

namespace Tina::Tests {
namespace {

struct ExpectedWidgetTraits final {
    UI::UIWidgetKind kind = UI::UIWidgetKind::Panel;
    UI::Detail::UIWidgetTraits traits{};
};

TEST(UIWidgetTraitsTests, ClassifiesEveryWidgetKindThroughOneCanonicalTable)
{
    constexpr std::array Cases{
        ExpectedWidgetTraits{UI::UIWidgetKind::Root, {}},
        ExpectedWidgetTraits{UI::UIWidgetKind::Panel, {}},
        ExpectedWidgetTraits{UI::UIWidgetKind::Label, {.supportsText = true}},
        ExpectedWidgetTraits{UI::UIWidgetKind::Button, {true, true, true, true}},
        ExpectedWidgetTraits{UI::UIWidgetKind::Checkbox, {false, true, true, false}},
        ExpectedWidgetTraits{UI::UIWidgetKind::Slider, {}},
        ExpectedWidgetTraits{UI::UIWidgetKind::TextEdit, {false, false, true, true}},
        ExpectedWidgetTraits{UI::UIWidgetKind::ProgressBar, {}},
        ExpectedWidgetTraits{UI::UIWidgetKind::RadioButton, {false, true, true, true}},
        ExpectedWidgetTraits{UI::UIWidgetKind::Modal, {}},
        ExpectedWidgetTraits{UI::UIWidgetKind::ScrollView, {}},
        ExpectedWidgetTraits{UI::UIWidgetKind::Dropdown, {true, true, true, true}},
        ExpectedWidgetTraits{UI::UIWidgetKind::Popup, {}},
        ExpectedWidgetTraits{UI::UIWidgetKind::DropdownItem, {true, true, true, true}},
        ExpectedWidgetTraits{UI::UIWidgetKind::ListView, {false, false, true, false}},
        ExpectedWidgetTraits{UI::UIWidgetKind::ListViewItem, {true, true, true, true}},
        ExpectedWidgetTraits{UI::UIWidgetKind::TreeView, {false, false, true, false}},
        ExpectedWidgetTraits{UI::UIWidgetKind::TreeViewItem, {true, true, true, true}},
    };

    for (const auto& testCase : Cases)
    {
        const auto actual = UI::Detail::widgetTraits(testCase.kind);
        EXPECT_EQ(actual.supportsButtonChrome,
                  testCase.traits.supportsButtonChrome);
        EXPECT_EQ(actual.defaultActivatable, testCase.traits.defaultActivatable);
        EXPECT_EQ(actual.keyboardFocusable, testCase.traits.keyboardFocusable);
        EXPECT_EQ(actual.supportsText, testCase.traits.supportsText);
        EXPECT_EQ(UI::Detail::isButtonChromeKind(testCase.kind),
                  testCase.traits.supportsButtonChrome);
        EXPECT_EQ(UI::Detail::isDefaultActivatableKind(testCase.kind),
                  testCase.traits.defaultActivatable);
        EXPECT_EQ(UI::Detail::isKeyboardFocusableKind(testCase.kind),
                  testCase.traits.keyboardFocusable);
        EXPECT_EQ(UI::Detail::supportsWidgetText(testCase.kind),
                  testCase.traits.supportsText);
    }
}

TEST(UIWidgetTraitsTests, UnknownKindHasNoCapabilities)
{
    const auto unknown = static_cast<UI::UIWidgetKind>(255);
    const auto traits = UI::Detail::widgetTraits(unknown);

    EXPECT_FALSE(traits.supportsButtonChrome);
    EXPECT_FALSE(traits.defaultActivatable);
    EXPECT_FALSE(traits.keyboardFocusable);
    EXPECT_FALSE(traits.supportsText);
}

} // namespace
} // namespace Tina::Tests
