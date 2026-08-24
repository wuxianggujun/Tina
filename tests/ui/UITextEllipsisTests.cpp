#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>
#include <tina/ui/UIAccessibility.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

// The built-in placeholder rasterizer is monospaced, so every drawable
// codepoint advances by logicalSize * advanceScale. The truncation expectations
// below are exact multiples of this width.
constexpr float GlyphAdvance = 16.0F * 0.6F;

// Proportional stub used to prove the planner searches real prefix widths
// instead of dividing the budget by one uniform advance. Unlike the shared
// UITextEditTestSupport rasterizer this one accepts U+2026, which truncation
// must be able to measure.
class ProportionalTextRasterizer final : public UI::IUITextRasterizer {
  public:
    static constexpr float WideAdvance = 24.0F;
    static constexpr float NarrowAdvance = 4.0F;
    static constexpr float EllipsisAdvance = 8.0F;

    [[nodiscard]] Core::Result<UI::UIFontFaceId> openFace(
        std::span<const std::byte> fontBytes, i32 faceIndex) override
    {
        if (!fontBytes.empty() || faceIndex != 0)
        {
            return Core::failure(UI::UIErrorCode::InvalidFont,
                                 "Proportional test rasterizer has only a built-in face");
        }
        m_open = true;
        return Face;
    }

    [[nodiscard]] Core::Status closeFace(UI::UIFontFaceId face) noexcept override
    {
        if (!m_open || face != Face)
        {
            return Core::failure(UI::UIErrorCode::InvalidFont, "Proportional test face is not open");
        }
        m_open = false;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UI::UITextMetrics> measure(
        UI::UIFontFaceId face, std::string_view utf8, UI::UITextStyle style) override
    {
        auto validated = UI::measurePlaceholderText(utf8, style);
        if (!m_open || face != Face)
        {
            return Core::failure(UI::UIErrorCode::InvalidFont, "Proportional test face is not open");
        }
        if (!validated)
        {
            return Core::failure(validated.error());
        }
        float width = 0.0F;
        forEachCodepoint(utf8, [&width](u32 codepoint) { width += advanceFor(codepoint); });
        return UI::UITextMetrics{
            .measuredSize = {.width = width,
                             .height = utf8.empty() ? 0.0F
                                                    : style.logicalSize * style.lineHeightScale},
            .codepointCount = validated->codepointCount,
            .lineCount = validated->lineCount,
        };
    }

    [[nodiscard]] Core::Result<UI::UITextRasterBatch> raster(
        UI::UIFontFaceId face, std::string_view utf8, UI::UITextStyle style) override
    {
        auto metrics = measure(face, utf8, style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }
        m_glyphs.clear();
        // A 1x1 opaque cell per codepoint keeps the atlas paint path active, so
        // each drawable codepoint still produces exactly one paint entry.
        forEachCodepoint(utf8, [this](u32 codepoint) {
            m_glyphs.push_back(UI::UITextGlyphRaster{
                .codepoint = codepoint,
                .advance = advanceFor(codepoint),
                .width = 1,
                .height = 1,
                .coverageOffset = 0,
                .coveragePitch = 1,
            });
        });
        return UI::UITextRasterBatch{
            .metrics = *metrics,
            .baselineFromLineTop = style.logicalSize * style.lineHeightScale,
            .glyphs = std::span<const UI::UITextGlyphRaster>(m_glyphs.data(), m_glyphs.size()),
            .coverage = std::span<const u8>(m_coverage.data(), m_coverage.size()),
        };
    }

    [[nodiscard]] UI::UITextRasterizerCapacity capacity() const noexcept override
    {
        return {.faceCapacity = 1, .maxGlyphsPerRaster = 256, .coverageByteCapacity = 1};
    }

  private:
    template <typename Visit>
    static void forEachCodepoint(std::string_view utf8, Visit&& visit) noexcept
    {
        usize index = 0;
        while (index < utf8.size())
        {
            const auto first = static_cast<unsigned char>(utf8[index]);
            usize length = 1;
            u32 codepoint = first;
            if ((first & 0xE0U) == 0xC0U)
            {
                length = 2;
                codepoint = first & 0x1FU;
            }
            else if ((first & 0xF0U) == 0xE0U)
            {
                length = 3;
                codepoint = first & 0x0FU;
            }
            else if ((first & 0xF8U) == 0xF0U)
            {
                length = 4;
                codepoint = first & 0x07U;
            }
            if (length > utf8.size() - index)
            {
                return;
            }
            for (usize offset = 1; offset < length; ++offset)
            {
                codepoint = (codepoint << 6U) |
                            (static_cast<unsigned char>(utf8[index + offset]) & 0x3FU);
            }
            visit(codepoint);
            index += length;
        }
    }

    [[nodiscard]] static float advanceFor(u32 codepoint) noexcept
    {
        if (codepoint == 0x2026U)
        {
            return EllipsisAdvance;
        }
        return codepoint == U'W' ? WideAdvance : NarrowAdvance;
    }

    static constexpr UI::UIFontFaceId Face{.index = 0, .generation = 1};
    std::vector<UI::UITextGlyphRaster> m_glyphs{};
    std::array<u8, 1> m_coverage{u8{255}};
    bool m_open = false;
};

[[nodiscard]] UI::UIContextCapacityConfig testCapacities() noexcept
{
    UI::UIContextCapacityConfig capacities{};
    capacities.applyDefaultProductChrome = false;
    capacities.nodeCapacity = 8;
    capacities.rootCapacity = 1;
    // Explicit so glyph-count expectations never fail as a capacity error.
    capacities.paintSnapshotCapacity = 64;
    capacities.textByteCapacity = 256;
    return capacities;
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(Platform::WindowId window)
{
    auto result = UI::UIContext::Create(window, testCapacities());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

void commit(UI::UIContext& context)
{
    const Core::Status status =
        context.publication().commitLayout(UI::UILogicalSize{.width = 320.0F, .height = 180.0F});
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

// No product chrome is applied and the Label has no box paint, so it
// contributes exactly one paint entry per emitted glyph.
[[nodiscard]] std::vector<UI::UICommittedPaintEntry> glyphEntries(
    UI::UIContext& context, UI::UINodeId node)
{
    std::vector<UI::UICommittedPaintEntry> entries;
    for (const UI::UICommittedPaintEntry& entry : context.publication().committedPaint().entries())
    {
        if (entry.node == node)
        {
            entries.push_back(entry);
        }
    }
    return entries;
}

[[nodiscard]] usize glyphEntryCount(UI::UIContext& context, UI::UINodeId node)
{
    return glyphEntries(context, node).size();
}

// WindowPool, UIRootOwner and UITreeUpdater are move-only with no move
// assignment, so the scene is built as locals and handed to the body instead of
// being returned from a fixture struct.
template <typename Body>
void withContext(std::unique_ptr<UI::UIContext> context, Body&& body)
{
    ASSERT_NE(context, nullptr);
    auto rootResult = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value()) << (rootResult ? "" : rootResult.error().message);
    UI::UIRootOwner root = std::move(*rootResult);

    auto updaterResult = context->authoring().treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value()) << (updaterResult ? "" : updaterResult.error().message);
    UI::UITreeUpdater updater = std::move(*updaterResult);

    body(*context, updater, root);
}

template <typename Body>
void withLabel(float width, float height, Body&& body)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    withContext(createContext(*windowResult),
                [&](UI::UIContext& context, UI::UITreeUpdater& updater, UI::UIRootOwner& root) {
                    UI::UIElementDescriptor descriptor = UI::makeLabelElement();
                    descriptor.textWrapMode = UI::UITextWrapMode::NoWrap;
                    auto labelResult =
                        updater.createElement(root.rootNodeId(), descriptor);
                    ASSERT_TRUE(labelResult.has_value())
                        << (labelResult ? "" : labelResult.error().message);
                    const UI::UINodeId label = *labelResult;
                    if (width > 0.0F)
                    {
                        assertOk(updater.setLayoutStyle(label, fixedSize(width, height)));
                    }
                    body(context, updater, root, label);
                });
}

} // namespace

TEST(UITextEllipsisTest, ClipIsTheDefaultAndEmitsEveryGlyph)
{
    // Eight glyphs need 76.8px; the box only offers 50px.
    withLabel(50.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                               UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCDEFGH"));

        auto overflow = updater.textOverflow(label);
        ASSERT_TRUE(overflow.has_value()) << (overflow ? "" : overflow.error().message);
        EXPECT_EQ(*overflow, UI::UITextOverflow::Clip);

        commit(context);
        // Clip keeps the full run and relies on the content-box clip.
        EXPECT_EQ(glyphEntryCount(context, label), 8U);
    });
}

TEST(UITextEllipsisTest, EllipsisKeepsTheLeadingPrefixAndPutsTheMarkerLast)
{
    withLabel(50.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                               UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCDEFGH"));
        commit(context);
        const std::vector<UI::UICommittedPaintEntry> clipped = glyphEntries(context, label);
        ASSERT_EQ(clipped.size(), 8U);

        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));
        commit(context);

        // budget = 50 - 9.6 = 40.4 -> four glyphs (38.4) fit, five (48.0) do not.
        const std::vector<UI::UICommittedPaintEntry> elided = glyphEntries(context, label);
        ASSERT_EQ(elided.size(), 4U + 1U);

        // The kept glyphs must be the leading prefix at unchanged positions; a
        // suffix-keeping or reordered implementation would still match a count.
        for (usize index = 0; index < 4U; ++index)
        {
            EXPECT_FLOAT_EQ(elided[index].worldRect.x, clipped[index].worldRect.x) << index;
            EXPECT_FLOAT_EQ(elided[index].worldRect.y, clipped[index].worldRect.y) << index;
        }
        // The marker trails the prefix.
        EXPECT_GT(elided[4].worldRect.x, elided[3].worldRect.x);
        EXPECT_FLOAT_EQ(elided[4].worldRect.x, clipped[4].worldRect.x);
    });
}

TEST(UITextEllipsisTest, EllipsisDoesNotTruncateTextThatAlreadyFits)
{
    // Four glyphs need 38.4px and the box offers 200px.
    withLabel(200.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                                UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCD"));
        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));

        commit(context);
        EXPECT_EQ(glyphEntryCount(context, label), 4U);
    });
}

TEST(UITextEllipsisTest, EllipsisCutsOnGraphemeClusterBoundaries)
{
    withLabel(30.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                               UI::UIRootOwner&, UI::UINodeId label) {
        // "a" + "e" + U+0301 COMBINING ACUTE + "b": four codepoints, three clusters.
        assertOk(updater.setText(label, "ae\xCC\x81" "b"));
        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));

        commit(context);
        // budget = 30 - 9.6 = 20.4. Cutting by codepoint would keep "ae" (19.2)
        // and orphan the combining mark; cutting by cluster keeps only "a".
        EXPECT_EQ(glyphEntryCount(context, label), 1U + 1U);
    });
}

TEST(UITextEllipsisTest, EllipsisAloneSurvivesWhenNoClusterFits)
{
    // Narrower than one glyph plus the ellipsis.
    withLabel(8.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                              UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCDEFGH"));
        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));

        commit(context);
        // The elision marker still tells the reader the value was cut.
        EXPECT_EQ(glyphEntryCount(context, label), 1U);
    });
}

TEST(UITextEllipsisTest, NonPositiveContentWidthKeepsClipBehaviour)
{
    withLabel(0.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                              UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setLayoutStyle(label, fixedSize(0.0F, 40.0F)));
        assertOk(updater.setText(label, "ABCDEFGH"));
        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));

        commit(context);
        // An unmeasured or collapsed box must not be read as "nothing fits".
        EXPECT_EQ(glyphEntryCount(context, label), 8U);
    });
}

TEST(UITextEllipsisTest, ExplicitNewlineKeepsClipBehaviour)
{
    withLabel(30.0F, 80.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                               UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCD\nEFGH"));
        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));

        commit(context);
        // An ellipsis has no defined position on a multi-line run, so all eight
        // drawable glyphs are still emitted ('\n' itself never draws).
        EXPECT_EQ(glyphEntryCount(context, label), 8U);
    });
}

TEST(UITextEllipsisTest, TransparentTextEmitsNothingRegardlessOfOverflow)
{
    withLabel(50.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                               UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCDEFGH"));
        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));
        UI::UITextStyle style{};
        style.color.alpha = 0;
        assertOk(updater.setTextStyle(label, style));

        commit(context);
        // The reserved count and the emitted run must both collapse to zero, or
        // the paint snapshot would over-reserve capacity forever.
        EXPECT_EQ(glyphEntryCount(context, label), 0U);
    });
}

TEST(UITextEllipsisTest, OverflowIsPaintOnlyAndDoesNotRerunLayoutOrHit)
{
    withLabel(50.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                               UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCDEFGH"));
        commit(context);

        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));

        // Truncation is resolved against the committed content box, so the
        // setter marks Paint only and leaves layout/hit/structure clean.
        const UI::UIContextStatistics dirty = context.statistics();
        EXPECT_TRUE(dirty.paintDirty);
        EXPECT_FALSE(dirty.layoutDirty);
        EXPECT_FALSE(dirty.hitDirty);
        EXPECT_FALSE(dirty.structureDirty);

        commit(context);
        const UI::UIContextStatistics after = context.statistics();
        // A paint-only commit does no measure/arrange/hit work at all.
        EXPECT_EQ(after.lastLayoutMeasuredNodeCount, 0U);
        EXPECT_EQ(after.lastLayoutArrangedNodeCount, 0U);
        EXPECT_EQ(after.lastHitRebuildCount, 0U);
        EXPECT_EQ(after.lastPaintSnapshotRebuildCount, 1U);
    });
}

TEST(UITextEllipsisTest, AutoWidthIntrinsicSizeIgnoresTruncation)
{
    // No fixed width: the committed box comes from the intrinsic text measure.
    withLabel(0.0F, 0.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                             UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCDEFGH"));
        commit(context);

        const auto findLabel = [&](UI::UINodeId node) {
            const UI::UICommittedLayoutView layout = context.publication().committedLayout();
            return std::ranges::find_if(
                layout.entries(),
                [node](const UI::UICommittedLayoutEntry& entry) { return entry.node == node; });
        };

        auto before = findLabel(label);
        ASSERT_NE(before, context.publication().committedLayout().entries().end());
        const float intrinsicWidth = before->contentPlacement.intrinsicSize.width;
        EXPECT_FLOAT_EQ(intrinsicWidth, GlyphAdvance * 8.0F);

        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));
        commit(context);

        auto after = findLabel(label);
        ASSERT_NE(after, context.publication().committedLayout().entries().end());
        // Intrinsic measure keeps describing the untruncated text, so an
        // Auto-sized element never shrinks itself into its own ellipsis.
        EXPECT_FLOAT_EQ(after->contentPlacement.intrinsicSize.width, intrinsicWidth);
        EXPECT_EQ(glyphEntryCount(context, label), 8U);
    });
}

TEST(UITextEllipsisTest, AccessibilityNamePublishesTheFullTextWhilePaintIsTruncated)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    withContext(createContext(*windowResult), [](UI::UIContext& context,
                                                 UI::UITreeUpdater& updater,
                                                 UI::UIRootOwner& root) {
        auto button = updater.createElement(root.rootNodeId(), UI::makeButtonElement());
        ASSERT_TRUE(button.has_value()) << (button ? "" : button.error().message);
        assertOk(updater.setLayoutStyle(*button, fixedSize(50.0F, 40.0F)));
        assertOk(updater.setText(*button, "ABCDEFGH"));
        assertOk(updater.setTextOverflow(*button, UI::UITextOverflow::Ellipsis));
        commit(context);

        EXPECT_EQ(glyphEntryCount(context, *button), 4U + 1U);

        UI::UIAccessibilityTree tree;
        assertOk(tree.rebuildFrom(context.publication().committedSemantics()));
        UI::UIAccessibilityProbeProvider probe;
        assertOk(probe.publish(tree));
        auto node = probe.readNode(*button);
        ASSERT_TRUE(node.has_value()) << node.error().message;
        // Assistive technology reads the value, not the elided rendering.
        EXPECT_EQ(node->name, "ABCDEFGH");
    });
}

TEST(UITextEllipsisTest, FocusedTextEditIsExemptFromTruncation)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    withContext(createContext(*windowResult), [](UI::UIContext& context,
                                                 UI::UITreeUpdater& updater,
                                                 UI::UIRootOwner& root) {
        auto edit = updater.createElement(root.rootNodeId(), UI::makeTextEditElement());
        ASSERT_TRUE(edit.has_value()) << (edit ? "" : edit.error().message);
        assertOk(updater.setLayoutStyle(*edit, fixedSize(50.0F, 40.0F)));
        assertOk(updater.setText(*edit, "ABCDEFGH"));
        assertOk(updater.setTextOverflow(*edit, UI::UITextOverflow::Ellipsis));
        commit(context);
        // Unfocused it elides like any other single-line text.
        EXPECT_EQ(glyphEntryCount(context, *edit), 4U + 1U);

        assertOk(updater.requestFocus(*edit));
        commit(context);
        // Focused, the caret and selection need the whole run, so truncation is
        // suppressed. The extra entries are the selection/caret quads.
        const std::vector<UI::UICommittedPaintEntry> focused = glyphEntries(context, *edit);
        EXPECT_GE(focused.size(), 8U);
    });
}

TEST(UITextEllipsisTest, MultilineTextEditIsExemptFromTruncation)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    withContext(createContext(*windowResult), [](UI::UIContext& context,
                                                 UI::UITreeUpdater& updater,
                                                 UI::UIRootOwner& root) {
        // Multiline is a create-time descriptor option; there is no runtime setter.
        UI::UIElementDescriptor descriptor = UI::makeTextEditElement();
        descriptor.textEditMultiline = UI::UITextEditMultilineConfig{
            .enabled = true,
            .wrapMode = UI::UITextEditWrapMode::SoftWrap,
            .maximumVisualLines = 8,
        };
        auto edit = updater.createElement(root.rootNodeId(), descriptor);
        ASSERT_TRUE(edit.has_value()) << (edit ? "" : edit.error().message);
        assertOk(updater.setLayoutStyle(*edit, fixedSize(50.0F, 80.0F)));
        assertOk(updater.setText(*edit, "ABCDEFGH"));
        assertOk(updater.setTextOverflow(*edit, UI::UITextOverflow::Ellipsis));

        commit(context);
        // A wrapped box reflows instead of eliding, so every glyph survives.
        EXPECT_EQ(glyphEntryCount(context, *edit), 8U);
    });
}

TEST(UITextEllipsisTest, TruncationSearchesRealPrefixWidthsNotAUniformAdvance)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    auto contextResult = UI::UIContext::Create(
        *windowResult, testCapacities(), std::make_unique<ProportionalTextRasterizer>());
    ASSERT_TRUE(contextResult.has_value())
        << (contextResult ? "" : contextResult.error().message);

    withContext(std::move(*contextResult), [](UI::UIContext& context, UI::UITreeUpdater& updater,
                                              UI::UIRootOwner& root) {
        auto labelResult = updater.createElement(root.rootNodeId(), UI::makeLabelElement());
        ASSERT_TRUE(labelResult.has_value()) << (labelResult ? "" : labelResult.error().message);
        const UI::UINodeId label = *labelResult;
        assertOk(updater.setLayoutStyle(label, fixedSize(40.0F, 40.0F)));
        // Advances are 24, 24, 4, 4, 4, 4 -> the full run is 64 wide.
        assertOk(updater.setText(label, "WWAAAA"));
        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));

        commit(context);
        // budget = 40 - 8 = 32. Only "W" (24) fits; "WW" is already 48. Dividing
        // the budget by any single advance would keep a different count.
        EXPECT_EQ(glyphEntryCount(context, label), 1U + 1U);
    });
}

TEST(UITextEllipsisTest, SettingTheSameOverflowIsANoOp)
{
    withLabel(50.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                               UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCDEFGH"));
        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));
        commit(context);

        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));
        EXPECT_EQ(context.statistics().dirtyQueuePendingCount, 0U);
    });
}

TEST(UITextEllipsisTest, RejectsElementsWithoutIntrinsicText)
{
    withLabel(50.0F, 40.0F, [](UI::UIContext&, UI::UITreeUpdater& updater,
                               UI::UIRootOwner& root, UI::UINodeId) {
        auto panel = updater.createElement(root.rootNodeId(), UI::makePanelElement());
        ASSERT_TRUE(panel.has_value()) << (panel ? "" : panel.error().message);

        const Core::Status status = updater.setTextOverflow(*panel, UI::UITextOverflow::Ellipsis);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidText);

        const auto query = updater.textOverflow(*panel);
        ASSERT_FALSE(query.has_value());
        EXPECT_EQ(query.error().code, UI::UIErrorCode::InvalidText);
    });
}

TEST(UITextEllipsisTest, RejectsUnknownOverflowValue)
{
    withLabel(50.0F, 40.0F, [](UI::UIContext&, UI::UITreeUpdater& updater,
                               UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCDEFGH"));

        const Core::Status status =
            updater.setTextOverflow(label, static_cast<UI::UITextOverflow>(7));
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidText);

        // The rejected value must not have been stored.
        auto overflow = updater.textOverflow(label);
        ASSERT_TRUE(overflow.has_value()) << (overflow ? "" : overflow.error().message);
        EXPECT_EQ(*overflow, UI::UITextOverflow::Clip);
    });
}

TEST(UITextEllipsisTest, TruncationTracksContentBoxWidthChanges)
{
    withLabel(200.0F, 40.0F, [](UI::UIContext& context, UI::UITreeUpdater& updater,
                                UI::UIRootOwner&, UI::UINodeId label) {
        assertOk(updater.setText(label, "ABCDEFGH"));
        assertOk(updater.setTextOverflow(label, UI::UITextOverflow::Ellipsis));
        commit(context);
        EXPECT_EQ(glyphEntryCount(context, label), 8U);

        // A layout commit always rebuilds the paint snapshot, so shrinking the
        // box re-resolves truncation without touching the retained text.
        assertOk(updater.setLayoutStyle(label, fixedSize(50.0F, 40.0F)));
        commit(context);
        EXPECT_EQ(glyphEntryCount(context, label), 4U + 1U);

        assertOk(updater.setLayoutStyle(label, fixedSize(200.0F, 40.0F)));
        commit(context);
        EXPECT_EQ(glyphEntryCount(context, label), 8U);
    });
}

TEST(UITextEllipsisTest, PlaceholderAdvanceMatchesTheTruncationBudget)
{
    // Guards the constant the expectations above are derived from.
    const auto single = UI::measurePlaceholderText("A", {});
    ASSERT_TRUE(single.has_value()) << (single ? "" : single.error().message);
    EXPECT_FLOAT_EQ(single->measuredSize.width, GlyphAdvance);

    const auto ellipsis = UI::measurePlaceholderText(UI::UITextEllipsisUtf8, {});
    ASSERT_TRUE(ellipsis.has_value()) << (ellipsis ? "" : ellipsis.error().message);
    EXPECT_EQ(ellipsis->codepointCount, 1U);
    EXPECT_FLOAT_EQ(ellipsis->measuredSize.width, GlyphAdvance);
}

} // namespace Tina::Tests
