#include "EditorSettingsText.hpp"

#include <tina/core/text/ParseFloat.hpp>
#include <tina/core/text/ParseInteger.hpp>

#include <gtest/gtest.h>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

TEST(EditorSettingsTextTest, ReadsLfCrLfAndTheUnterminatedLastLine)
{
    constexpr std::string_view text = "version=2\r\nleftDock=0.25\nbottom=0.75";
    EXPECT_EQ(findEditorSettingValue(text, "version"), "2");
    EXPECT_EQ(findEditorSettingValue(text, "leftDock"), "0.25");
    EXPECT_EQ(findEditorSettingValue(text, "bottom"), "0.75");
    EXPECT_TRUE(findEditorSettingValue(text, "absent").empty());
    EXPECT_TRUE(findEditorSettingValue({}, "version").empty());
    EXPECT_TRUE(findEditorSettingValue("=value", {}).empty());
}

TEST(EditorSettingsTextTest, RequiresAnExactKeyAtTheBeginningOfALine)
{
    constexpr std::string_view text =
        "notleftDock=0.8\nrecent0=D:/games/leftDock=0.9\nleftDockExtra=0.7\nleftDock=0.25\n";
    EXPECT_EQ(findEditorSettingValue(text, "leftDock"), "0.25");
    EXPECT_TRUE(findEditorSettingValue("leftDockExtra=0.7", "leftDock").empty());
    EXPECT_EQ(findEditorSettingValue(text, "recent0"), "D:/games/leftDock=0.9");
}

TEST(EditorSettingsTextTest, ReturnsBorrowedViewsWithoutChangingUtf8OrEmbeddedEquals)
{
    constexpr std::string_view text = "recent0=D:/游戏/地图=01\r\nempty=\n";
    const auto path = findEditorSettingValue(text, "recent0");
    EXPECT_EQ(path, "D:/游戏/地图=01");
    EXPECT_EQ(path.data(), text.data() + std::string_view{"recent0="}.size());
    EXPECT_TRUE(findEditorSettingValue(text, "empty").empty());
}

TEST(EditorSettingsTextTest, ValuesUseTheCoreStrictNumericGrammar)
{
    constexpr std::string_view text = "version=2\nleftDock=0.25\nbottom=0.75garbage\n";
    Core::u32 version = 0;
    ASSERT_TRUE(Core::parseUnsigned(findEditorSettingValue(text, "version"), version));
    EXPECT_EQ(version, 2U);
    const auto fraction = Core::parseStrictFloat(findEditorSettingValue(text, "leftDock"));
    ASSERT_TRUE(fraction);
    EXPECT_FLOAT_EQ(*fraction, 0.25F);
    EXPECT_FALSE(Core::parseStrictFloat(findEditorSettingValue(text, "bottom")));
}

} // namespace
} // namespace Tina::EditorApp::WorkspaceInternal
