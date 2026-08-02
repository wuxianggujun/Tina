#include <gtest/gtest.h>

#include <tina/ui/UIDirty.hpp>
#include <tina/ui/UIStyle.hpp>

namespace Tina::Tests {
namespace {

using UI::UIDirty;
using UI::UIStyleOverride;
using UI::UIStylePropertyKind;

TEST(UIStylePropertyDirtyMetadataTests, ColorAndOpacityArePaintOnly)
{
    constexpr UIDirty flags = UI::dirtyFlagsForStyleProperty(UIStylePropertyKind::ColorOrOpacity);
    EXPECT_TRUE(UI::stylePropertyDirtiesPaint(UIStylePropertyKind::ColorOrOpacity));
    EXPECT_FALSE(UI::stylePropertyDirtiesLayout(UIStylePropertyKind::ColorOrOpacity));
    EXPECT_FALSE(UI::stylePropertyDirtiesHit(UIStylePropertyKind::ColorOrOpacity));
    EXPECT_TRUE(UI::stylePropertyDirtiesSemantics(UIStylePropertyKind::ColorOrOpacity));
    EXPECT_TRUE(UI::hasDirty(flags, UIDirty::Paint));
    EXPECT_TRUE(UI::hasDirty(flags, UIDirty::Semantics));
    EXPECT_FALSE(UI::hasDirty(flags, UIDirty::Measure));
    EXPECT_FALSE(UI::hasDirty(flags, UIDirty::Arrange));
    EXPECT_FALSE(UI::hasDirty(flags, UIDirty::HitTest));
}

TEST(UIStylePropertyDirtyMetadataTests, ColorTokenIsPaintOnlyWithoutSemantics)
{
    constexpr UIDirty flags = UI::dirtyFlagsForStyleProperty(UIStylePropertyKind::ColorToken);
    EXPECT_TRUE(UI::stylePropertyDirtiesPaint(UIStylePropertyKind::ColorToken));
    EXPECT_FALSE(UI::stylePropertyDirtiesLayout(UIStylePropertyKind::ColorToken));
    EXPECT_FALSE(UI::stylePropertyDirtiesHit(UIStylePropertyKind::ColorToken));
    EXPECT_FALSE(UI::stylePropertyDirtiesSemantics(UIStylePropertyKind::ColorToken));
    EXPECT_EQ(flags, UIDirty::Paint);
}

TEST(UIStylePropertyDirtyMetadataTests, TextStyleAndAlignmentDirtyLayoutAndPaint)
{
    EXPECT_TRUE(UI::stylePropertyDirtiesLayout(UIStylePropertyKind::TextStyle));
    EXPECT_TRUE(UI::stylePropertyDirtiesPaint(UIStylePropertyKind::TextStyle));
    EXPECT_TRUE(UI::stylePropertyDirtiesHit(UIStylePropertyKind::TextStyle));
    EXPECT_TRUE(UI::stylePropertyDirtiesLayout(UIStylePropertyKind::ContentAlignment));
    EXPECT_TRUE(UI::stylePropertyDirtiesPaint(UIStylePropertyKind::ContentAlignment));
}

TEST(UIStylePropertyDirtyMetadataTests, PointerHitPolicyIsHitOnly)
{
    constexpr UIDirty flags = UI::dirtyFlagsForStyleProperty(UIStylePropertyKind::PointerHitPolicy);
    EXPECT_TRUE(UI::stylePropertyDirtiesHit(UIStylePropertyKind::PointerHitPolicy));
    EXPECT_FALSE(UI::stylePropertyDirtiesPaint(UIStylePropertyKind::PointerHitPolicy));
    EXPECT_FALSE(UI::stylePropertyDirtiesLayout(UIStylePropertyKind::PointerHitPolicy));
    EXPECT_EQ(flags, UIDirty::HitTest);
}

TEST(UIStylePropertyDirtyMetadataTests, LayoutStyleDirtiesLayoutNotPaint)
{
    EXPECT_TRUE(UI::stylePropertyDirtiesLayout(UIStylePropertyKind::LayoutStyle));
    EXPECT_FALSE(UI::stylePropertyDirtiesPaint(UIStylePropertyKind::LayoutStyle));
    EXPECT_TRUE(UI::stylePropertyDirtiesHit(UIStylePropertyKind::LayoutStyle));
    // markLayoutDirtyBatch also dirties Semantics on the changed node.
    EXPECT_TRUE(UI::stylePropertyDirtiesSemantics(UIStylePropertyKind::LayoutStyle));
}

TEST(UIStylePropertyDirtyMetadataTests, StyleOverrideMapsPaintChromeAndTextStyle)
{
    EXPECT_EQ(UI::dirtyFlagsForStyleOverride(UIStyleOverride::BoxPaint),
              UI::dirtyFlagsForStyleProperty(UIStylePropertyKind::ColorOrOpacity));
    EXPECT_EQ(UI::dirtyFlagsForStyleOverride(UIStyleOverride::ImageTint),
              UI::dirtyFlagsForStyleProperty(UIStylePropertyKind::ColorOrOpacity));
    EXPECT_EQ(UI::dirtyFlagsForStyleOverride(UIStyleOverride::ButtonPaint),
              UI::dirtyFlagsForStyleProperty(UIStylePropertyKind::ColorOrOpacity));
    EXPECT_EQ(UI::dirtyFlagsForStyleOverride(UIStyleOverride::TextStyle),
              UI::dirtyFlagsForStyleProperty(UIStylePropertyKind::TextStyle));
    EXPECT_EQ(UI::dirtyFlagsForStyleOverride(UIStyleOverride::None), UIDirty::None);
}

} // namespace
} // namespace Tina::Tests
