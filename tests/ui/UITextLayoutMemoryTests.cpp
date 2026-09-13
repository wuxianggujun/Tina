#include "detail/UITextEditPaintEmitter.hpp"
#include "detail/UITextWrapping.hpp"
#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>
#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <new>
#include <string>

namespace Tina::Tests {
namespace {

class TextScratchResource final : public std::pmr::memory_resource {
  public:
    usize allocations = 0;
    bool reject = false;
  private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (reject) { throw std::bad_alloc{}; }
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    { std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment); }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};

TEST(UITextLayoutMemoryTests, MoreThan4096RowsGrowOnceAndReuseTheirOwnerStorage)
{
    TextScratchResource resource;
    UI::Detail::UITextLineLayout layout(resource);
    const std::string text(5000, '\n');
    const UI::UITextStyle style{.logicalSize = 10.0F, .lineHeightScale = 1.5F};
    const UI::UITextMeasureOptions options{10.0F, UI::UITextWrapMode::Words};
    const auto warm = layout.measure(text, style, nullptr, {}, options);
    ASSERT_TRUE(warm);
    EXPECT_EQ(warm->lineCount, 5001U);
    EXPECT_EQ(warm->codepointCount, 5000U);
    EXPECT_FLOAT_EQ(warm->measuredSize.height, 75015.0F);
    const auto allocations = resource.allocations;
    resource.reject = true;
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        auto measured = layout.measure(text, style, nullptr, {}, options);
        ASSERT_TRUE(measured);
        EXPECT_EQ(*measured, *warm);
    }
    EXPECT_EQ(resource.allocations, allocations);
}

TEST(UITextLayoutMemoryTests, LineAndEllipsisGrowthFailuresAreExplicitAndRetryable)
{
    TextScratchResource resource;
    UI::Detail::UITextLineLayout layout(resource);
    UI::Detail::UITextTruncationScratch truncation(resource);
    resource.reject = true;
    const auto lines = layout.measure("A\nB", {}, nullptr, {}, {10.0F, UI::UITextWrapMode::Words});
    ASSERT_FALSE(lines);
    EXPECT_EQ(lines.error().code, Core::CoreErrorCode::OutOfMemory);
    const auto clipped = UI::Detail::resolveTextTruncation(
        truncation, {}, "ABCD", {}, UI::UITextOverflow::Ellipsis, 20.0F, 0.0F);
    ASSERT_FALSE(clipped);
    EXPECT_EQ(clipped.error().code, Core::CoreErrorCode::OutOfMemory);
    resource.reject = false;
    EXPECT_TRUE(layout.measure("A\nB", {}, nullptr, {}, {10.0F, UI::UITextWrapMode::Words}));
    EXPECT_TRUE(UI::Detail::resolveTextTruncation(
        truncation, {}, "ABCD", {}, UI::UITextOverflow::Ellipsis, 20.0F, 0.0F));
}

TEST(UITextLayoutMemoryTests, WrapAndClampNeverSplitALigatureSpanningSeveralGraphemes)
{
    const std::array scalars{
        UI::UITextScalarMetrics{.advance = 4, .clusterByteBegin = 0, .clusterByteEnd = 3},
        UI::UITextScalarMetrics{.advance = 4, .clusterByteBegin = 0, .clusterByteEnd = 3},
        UI::UITextScalarMetrics{.advance = 4, .clusterByteBegin = 0, .clusterByteEnd = 3},
        UI::UITextScalarMetrics{.advance = 4, .clusterByteBegin = 3, .clusterByteEnd = 4},
        UI::UITextScalarMetrics{.advance = 4, .clusterByteBegin = 4, .clusterByteEnd = 5}};
    UI::Detail::UITextLineCursor wrapped;
    UI::Detail::UITextVisualLine line;
    ASSERT_TRUE(UI::Detail::nextWrappedTextLine("ffiXY", 10, UI::UITextWrapMode::Words, 4, scalars, wrapped, line));
    EXPECT_EQ(line.byteEnd, 3U);
    EXPECT_EQ(line.scalarEnd, 3U);
    UI::Detail::UITextClampedLineCursor clamped;
    ASSERT_TRUE(UI::Detail::nextClampedTextLine(
        "ffiXY", 10, UI::UITextWrapMode::Words, {1}, 4, 4, scalars, clamped, line));
    EXPECT_EQ(line.byteEnd, 0U);
    EXPECT_TRUE(line.showEllipsis);
    EXPECT_FLOAT_EQ(line.width, 4);
}

TEST(UITextLayoutMemoryTests, PreeditBeyond64KiBGrowsAndReusesScratchWithoutAStackLimit)
{
    TextScratchResource resource;
    UI::Detail::UITextEditPaintScratch scratch(resource);
    const std::string preedit(70000, 'x');
    const UI::Detail::UITextEditPaintState state{
        .focused = true, .preeditActive = true, .committedText = "AB",
        .selection = {1, 1}, .preeditText = preedit, .preeditCursorCodepoint = 70000,
        .style = {.logicalSize = 10.0F, .advanceScale = 0.5F, .lineHeightScale = 1.5F}};
    const auto count = UI::Detail::UITextEditPaintEmitter::countEntries(scratch, state);
    ASSERT_TRUE(count);
    EXPECT_EQ(*count, 70001U);
    EXPECT_EQ(scratch.composition.size(), 70002U);
    const auto allocations = resource.allocations;
    resource.reject = true;
    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(*count);
    u32 ordinal = 0;
    UI::UICommittedLayoutEntry layout;
    layout.contentPlacement.contentBox = {0, 0, 400000, 30};
    layout.effectiveClip = layout.contentPlacement.contentBox;
    ASSERT_TRUE(UI::Detail::UITextEditPaintEmitter::countEntries(scratch, state));
    const auto caret = UI::Detail::UITextEditPaintEmitter::append(scratch, output, layout, ordinal, state);
    ASSERT_TRUE(caret);
    ASSERT_TRUE(caret->has_value());
    EXPECT_EQ(output.size(), *count);
    EXPECT_FLOAT_EQ((*caret)->worldRect.x, 350005.0F);
    EXPECT_EQ(resource.allocations, allocations);
}

TEST(UITextLayoutMemoryTests, FailedWrappedCommitPreservesPreviousLayoutAndPaint)
{
    TextScratchResource resource;
    auto windows = Core::GenerationPool<int, Platform::WindowRegistryTag>::Create(1).value();
    auto context = UI::UIContext::Create(windows.tryEmplace(1).value(),
        {.applyDefaultProductChrome = false}, resource).value();
    auto root = context->authoring().rootBuilder().createRoot().value();
    auto updater = context->authoring().treeUpdater(root).value();
    auto descriptor = UI::makeLabelElement();
    descriptor.text = "A";
    descriptor.textWrapMode = UI::UITextWrapMode::Words;
    descriptor.layout.size.width = UI::UILayoutLength::Px(100);
    const auto label = updater.createElement(root.rootNodeId(), descriptor).value();
    ASSERT_TRUE(context->publication().commitLayout({200, 100}));
    const auto before = context->statistics();
    const auto oldPaint = context->publication().committedPaint().entries().front();
    ASSERT_TRUE(updater.setText(label, std::string(5000, '\n') + "A"));
    resource.reject = true;
    const auto committed = context->publication().commitLayout({200, 100});
    ASSERT_FALSE(committed);
    EXPECT_EQ(context->statistics().layoutRevision, before.layoutRevision);
    EXPECT_EQ(context->statistics().paintRevision, before.paintRevision);
    EXPECT_EQ(context->publication().committedPaint().entries().front().worldRect, oldPaint.worldRect);
    resource.reject = false;
    EXPECT_TRUE(context->publication().commitLayout({200, 100}));
}

} // namespace
} // namespace Tina::Tests
