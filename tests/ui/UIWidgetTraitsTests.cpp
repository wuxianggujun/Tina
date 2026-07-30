#include <gtest/gtest.h>

#include "detail/UIWidgetTraits.hpp"

#include <array>

namespace Tina::Tests {
namespace {

struct ExpectedWidgetTraits final {
    UI::Detail::BuiltinElementKind kind = UI::Detail::BuiltinElementKind::Panel;
    UI::Detail::UIWidgetTraits traits{};
};

TEST(UIWidgetTraitsTests, ClassifiesEveryWidgetKindThroughOneCanonicalTable)
{
    constexpr std::array Cases{
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::Root, {}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::Panel, {}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::Label, {.supportsText = true}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::Button, {true, true, true, true}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::Checkbox, {false, true, true, false}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::Slider, {false, false, true, false}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::TextEdit, {false, false, true, true}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::ProgressBar, {}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::RadioButton, {false, true, true, true}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::Modal, {}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::ScrollView, {}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::Dropdown, {true, true, true, true}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::Popup, {}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::DropdownItem, {true, true, true, true}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::ListView, {false, false, true, false}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::ListViewItem, {true, true, true, true}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::TreeView, {false, false, true, false}},
        ExpectedWidgetTraits{UI::Detail::BuiltinElementKind::TreeViewItem, {true, true, true, true}},
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
    const auto unknown = static_cast<UI::Detail::BuiltinElementKind>(255);
    const auto traits = UI::Detail::widgetTraits(unknown);

    EXPECT_FALSE(traits.supportsButtonChrome);
    EXPECT_FALSE(traits.defaultActivatable);
    EXPECT_FALSE(traits.keyboardFocusable);
    EXPECT_FALSE(traits.supportsText);
}

} // namespace
} // namespace Tina::Tests
