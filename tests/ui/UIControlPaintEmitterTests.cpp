#include <gtest/gtest.h>

#include "detail/UIControlPaintEmitter.hpp"

#include <memory_resource>
#include <vector>

namespace Tina::Tests {
namespace {

TEST(UIControlPaintEmitterTests, IgnoresEmptyAndTransparentPrimitives)
{
    UI::Detail::UIControlPaintBatch batch;

    EXPECT_TRUE(batch.add({.width = 0.0F, .height = 10.0F}, UI::premultiply(UI::rgb(0x112233))));
    EXPECT_TRUE(batch.add({.width = 10.0F, .height = 10.0F}, {}));
    EXPECT_EQ(batch.size(), 0U);
}

TEST(UIControlPaintEmitterTests, AppendsStableOrdinalsInInsertionOrder)
{
    UI::Detail::UIControlPaintBatch batch;
    ASSERT_TRUE(batch.add({.x = 1.0F, .y = 2.0F, .width = 3.0F, .height = 4.0F}, UI::premultiply(UI::rgb(0x112233))));
    ASSERT_TRUE(batch.add({.x = 5.0F, .y = 6.0F, .width = 7.0F, .height = 8.0F}, UI::premultiply(UI::rgb(0x445566))));

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(2);
    u32 nextPaintOrdinal = 7;
    const UI::UILogicalRect clip{.x = 10.0F, .y = 20.0F, .width = 30.0F, .height = 40.0F};
    batch.appendTo(output, {}, clip, nextPaintOrdinal);

    ASSERT_EQ(output.size(), 2U);
    EXPECT_FLOAT_EQ(output[0].worldRect.x, 1.0F);
    EXPECT_EQ(output[0].paintOrdinal, 7U);
    EXPECT_EQ(output[0].effectiveClip, clip);
    EXPECT_FLOAT_EQ(output[1].worldRect.x, 5.0F);
    EXPECT_EQ(output[1].paintOrdinal, 8U);
    EXPECT_EQ(nextPaintOrdinal, 9U);
}

TEST(UIControlPaintEmitterTests, RejectsMoreThanTheFixedPrimitiveCapacity)
{
    UI::Detail::UIControlPaintBatch batch;
    for (usize index = 0; index < UI::Detail::UIControlPaintBatch::Capacity; ++index)
    {
        ASSERT_TRUE(batch.add({.width = 1.0F, .height = 1.0F}, UI::premultiply(UI::rgb(0xFFFFFF))));
    }

    EXPECT_FALSE(batch.add({.width = 1.0F, .height = 1.0F}, UI::premultiply(UI::rgb(0xFFFFFF))));
    EXPECT_EQ(batch.size(), UI::Detail::UIControlPaintBatch::Capacity);
}

} // namespace
} // namespace Tina::Tests
