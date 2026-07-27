#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

class VariableAdvanceTextRasterizer final : public UI::IUITextRasterizer {
  public:
    [[nodiscard]] Core::Result<UI::UIFontFaceId> openFace(
        std::span<const std::byte> fontBytes,
        i32 faceIndex) override
    {
        if (!fontBytes.empty() || faceIndex != 0) {
            return Core::failure(
                UI::UIErrorCode::InvalidFont,
                "Variable-advance test rasterizer accepts only its built-in face");
        }
        m_open = true;
        return Face;
    }

    [[nodiscard]] Core::Status closeFace(UI::UIFontFaceId face) noexcept override
    {
        if (!m_open || face != Face) {
            return Core::failure(
                UI::UIErrorCode::InvalidFont,
                "Variable-advance test face is not open");
        }
        m_open = false;
        return Core::success();
    }

    [[nodiscard]] Core::Result<UI::UITextMetrics> measure(
        UI::UIFontFaceId face,
        std::string_view utf8,
        UI::UITextStyle style) override
    {
        if (!m_open || face != Face) {
            return Core::failure(
                UI::UIErrorCode::InvalidFont,
                "Variable-advance test face is not open");
        }
        auto validated = UI::measurePlaceholderText(utf8, style);
        if (!validated) {
            return Core::failure(validated.error());
        }
        if (validated->codepointCount != utf8.size()) {
            return Core::failure(
                UI::UIErrorCode::InvalidText,
                "Variable-advance test rasterizer accepts ASCII only");
        }
        float width = 0.0F;
        for (const char character : utf8) {
            width += advanceFor(character);
        }
        return UI::UITextMetrics{
            .measuredSize = {
                .width = width,
                .height = utf8.empty()
                    ? 0.0F
                    : style.logicalSize * style.lineHeightScale,
            },
            .codepointCount = static_cast<u32>(utf8.size()),
            .lineCount = utf8.empty() ? 0U : 1U,
        };
    }

    [[nodiscard]] Core::Result<UI::UITextRasterBatch> raster(
        UI::UIFontFaceId face,
        std::string_view utf8,
        UI::UITextStyle style) override
    {
        auto metrics = measure(face, utf8, style);
        if (!metrics) {
            return Core::failure(metrics.error());
        }
        if (utf8.size() > m_glyphs.size()) {
            return Core::failure(
                UI::UIErrorCode::CapacityExceeded,
                "Variable-advance test raster capacity exhausted");
        }
        for (usize index = 0; index < utf8.size(); ++index) {
            m_glyphs[index] = UI::UITextGlyphRaster{
                .codepoint = static_cast<u32>(
                    static_cast<unsigned char>(utf8[index])),
                .advance = advanceFor(utf8[index]),
            };
        }
        return UI::UITextRasterBatch{
            .metrics = *metrics,
            .glyphs = std::span<const UI::UITextGlyphRaster>(
                m_glyphs.data(),
                utf8.size()),
            .coverage = {},
        };
    }

    [[nodiscard]] UI::UITextRasterizerCapacity capacity() const noexcept override
    {
        return {
            .faceCapacity = 1,
            .maxGlyphsPerRaster = static_cast<u32>(m_glyphs.size()),
            .coverageByteCapacity = 1,
        };
    }

  private:
    [[nodiscard]] static float advanceFor(char character) noexcept
    {
        return character == 'W' ? 24.0F : 4.0F;
    }

    static constexpr UI::UIFontFaceId Face{.index = 0, .generation = 1};
    std::array<UI::UITextGlyphRaster, 8> m_glyphs{};
    bool m_open = false;
};

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacityConfig)
{
    capacityConfig.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(
        window,
        capacityConfig);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window)
{
    return createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 32,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 32,
            .routePathCapacity = 16,
            .textByteCapacity = 512,
        });
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIPointerInputEvent makePrimaryPointerDown(
    Platform::WindowId window,
    u64 sourceSequence,
    float x = 10.0F,
    float y = 10.0F) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sourceSequence},
        .transitionOrdinal = 0,
        .sourceSequence = sourceSequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = UI::UIRoutedPointerEventKind::ButtonDown,
        .position = {.x = x, .y = y},
        .button = Platform::PointerButton::Primary,
    };
}

[[nodiscard]] UI::UIPointerInputEvent makePrimaryPointerInput(
    Platform::WindowId window,
    UI::UIRoutedPointerEventKind kind,
    u64 sourceSequence,
    float x = 10.0F,
    float y = 10.0F) noexcept
{
    UI::UIPointerInputEvent input = makePrimaryPointerDown(
        window,
        sourceSequence,
        x,
        y);
    input.kind = kind;
    input.transitionOrdinal = static_cast<usize>(sourceSequence - 1);
    return input;
}

class UITextEditTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows.emplace(std::move(*windowsResult));

        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;

        context = createContext(window);
        ASSERT_NE(context, nullptr);
        root = createRoot(*context);
        ASSERT_TRUE(root.hasValue());
        updater = createUpdater(*context, root);
        assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(320.0F, 120.0F)));
    }

    [[nodiscard]] UI::UINodeId createTextEdit(
        float width = 240.0F,
        float height = 32.0F)
    {
        auto result = updater.createTextEdit(root.rootNodeId());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        EXPECT_TRUE(updater.setLayoutStyle(*result, fixedSize(width, height)).has_value());
        return *result;
    }

    void publishLayout()
    {
        assertOk(context->commitLayout({.width = 320.0F, .height = 120.0F}));
    }

    void focusWithTab(UI::UINodeId expectedFocus)
    {
        publishLayout();
        auto result = context->routeDefaultActionFocusStep(false);
        ASSERT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        EXPECT_TRUE(result->consumed);
        EXPECT_EQ(result->focus, expectedFocus);
        EXPECT_EQ(context->imeFocus(), expectedFocus);
    }

    std::optional<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
};

TEST_F(UITextEditTest, DefaultsTargetableWhileLabelRemainsReadOnly)
{
    auto labelResult = updater.createLabel(root.rootNodeId());
    ASSERT_TRUE(labelResult.has_value());
    const UI::UINodeId label = *labelResult;
    assertOk(updater.setLayoutStyle(label, fixedSize(100.0F, 24.0F)));

    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    publishLayout();

    bool sawLabelHit = false;
    bool sawTextEditHit = false;
    for (const UI::UICommittedHitEntry& entry : context->committedHit().entries()) {
        if (entry.node == label) {
            sawLabelHit = true;
            EXPECT_EQ(entry.policy, UI::UIPointerHitPolicy::Ignore);
        }
        if (entry.node == textEdit) {
            sawTextEditHit = true;
            EXPECT_EQ(entry.policy, UI::UIPointerHitPolicy::Targetable);
        }
    }
    EXPECT_TRUE(sawLabelHit);
    EXPECT_TRUE(sawTextEditHit);

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_TRUE(focus->consumed);
    EXPECT_EQ(focus->focus, textEdit);
    EXPECT_EQ(context->defaultActionFocus(), textEdit);
    EXPECT_EQ(context->imeFocus(), textEdit);

    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
    EXPECT_EQ(*selection, (UI::UITextSelection{}));
}

TEST_F(UITextEditTest, EnforcesSingleLineUtf8AndCodepointSelectionBounds)
{
    constexpr std::string_view MixedUtf8 = "A" "\xE4\xBD\xA0" "B";
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());

    assertOk(updater.setText(textEdit, MixedUtf8));
    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, MixedUtf8);

    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 1, .caretCodepoint = 2}));

    const Core::Status pastEnd = updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 4});
    ASSERT_FALSE(pastEnd.has_value());
    EXPECT_EQ(pastEnd.error().code, UI::UIErrorCode::InvalidText);

    for (const std::string_view invalid : {std::string_view{"A\nB"}, std::string_view{"A\rB"}}) {
        const Core::Status status = updater.setText(textEdit, invalid);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidText);
    }
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, MixedUtf8);

    assertOk(updater.setText(textEdit, "A"));
    selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 1, .caretCodepoint = 1}));
}

TEST_F(UITextEditTest, InvalidTextInputAndCompositionLeaveActivePreeditUnchanged)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "AB"));
    focusWithTab(textEdit);

    auto started = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "old",
        2,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(started.has_value())
        << (started ? "" : started.error().message);

    const auto expectCompositionStateUnchanged = [&] {
        EXPECT_TRUE(context->imeCompositionActive());
        EXPECT_EQ(context->imePreeditUtf8(), "old");
        EXPECT_EQ(context->imePreeditCursorCodepoint(), 2U);
    };
    const auto expectInvalidComposition = [&](u64 sequence,
                                              std::string_view preedit,
                                              Platform::TextCompositionStage stage,
                                              Core::ErrorCode expectedError) {
        auto result = context->routeTextComposition(
            window,
            Platform::PlatformFrameId{sequence},
            sequence,
            preedit,
            0,
            stage);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, expectedError);
        expectCompositionStateUnchanged();
    };

    expectInvalidComposition(
        2,
        "new",
        static_cast<Platform::TextCompositionStage>(255),
        UI::UIErrorCode::InvalidText);
    expectInvalidComposition(
        3,
        std::string_view{"\xC3\x28", 2},
        Platform::TextCompositionStage::Updated,
        UI::UIErrorCode::InvalidText);
    expectInvalidComposition(
        4,
        std::string_view{"A\0B", 3},
        Platform::TextCompositionStage::Updated,
        UI::UIErrorCode::InvalidText);

    const std::string oversizedPreedit(513, 'x');
    expectInvalidComposition(
        5,
        oversizedPreedit,
        Platform::TextCompositionStage::Updated,
        UI::UIErrorCode::CapacityExceeded);

    for (const auto [sequence, invalidInput] : {
             std::pair{6ULL, std::string_view{"\xE2\x82", 2}},
             std::pair{7ULL, std::string_view{"A\0B", 3}},
         }) {
        auto input = context->routeTextInput(
            window,
            Platform::PlatformFrameId{sequence},
            sequence,
            invalidInput);
        ASSERT_FALSE(input.has_value());
        EXPECT_EQ(input.error().code, UI::UIErrorCode::InvalidText);
        expectCompositionStateUnchanged();
    }
}

TEST_F(UITextEditTest, CompositionByteLimitAndProgrammaticTextClampState)
{
    constexpr std::string_view InitialUtf8 = "A" "\xE4\xBD\xA0" "BC";
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, InitialUtf8));
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 4}));
    focusWithTab(textEdit);

    const std::string maximumPreedit(512, 'x');
    auto composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        maximumPreedit,
        999,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value())
        << (composition ? "" : composition.error().message);
    EXPECT_TRUE(composition->consumed);
    EXPECT_TRUE(composition->applied);
    EXPECT_EQ(context->imePreeditUtf8(), maximumPreedit);
    EXPECT_EQ(context->imePreeditCursorCodepoint(), 512U);

    const Core::Status invalidReplacement = updater.setText(
        textEdit,
        std::string_view{"\xED\xA0\x80", 3});
    ASSERT_FALSE(invalidReplacement.has_value());
    EXPECT_EQ(invalidReplacement.error().code, UI::UIErrorCode::InvalidText);
    EXPECT_TRUE(context->imeCompositionActive());
    EXPECT_EQ(context->imePreeditUtf8(), maximumPreedit);

    assertOk(updater.setText(textEdit, "X"));
    EXPECT_FALSE(context->imeCompositionActive());
    EXPECT_TRUE(context->imePreeditUtf8().empty());
    EXPECT_EQ(context->imePreeditCursorCodepoint(), 0U);
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 1, .caretCodepoint = 1}));

    composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{2},
        2,
        "again",
        5,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value())
        << (composition ? "" : composition.error().message);
    ASSERT_TRUE(context->imeCompositionActive());

    assertOk(updater.setText(textEdit, "X"));
    EXPECT_FALSE(context->imeCompositionActive());
    EXPECT_TRUE(context->imePreeditUtf8().empty());
    EXPECT_EQ(context->imePreeditCursorCodepoint(), 0U);
}

TEST_F(UITextEditTest, TextInputAndImeCommitReplaceUnicodeSelection)
{
    constexpr std::string_view InitialUtf8 = "A" "\xE4\xBD\xA0" "B";
    constexpr std::string_view ReplacedUtf8 = "A" "\xE5\xA5\xBD" "B";
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, InitialUtf8));
    focusWithTab(textEdit);
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));

    auto composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "ni",
        1,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value()) << (composition ? "" : composition.error().message);
    EXPECT_TRUE(composition->consumed);
    EXPECT_TRUE(composition->applied);
    EXPECT_TRUE(context->imeCompositionActive());
    EXPECT_EQ(context->imePreeditUtf8(), "ni");
    EXPECT_EQ(context->imePreeditCursorCodepoint(), 1U);

    auto beforeCommit = updater.text(textEdit);
    ASSERT_TRUE(beforeCommit.has_value());
    EXPECT_EQ(*beforeCommit, InitialUtf8);

    auto commit = context->routeTextInput(
        window,
        Platform::PlatformFrameId{2},
        2,
        "\xE5\xA5\xBD");
    ASSERT_TRUE(commit.has_value()) << (commit ? "" : commit.error().message);
    EXPECT_TRUE(commit->consumed);
    EXPECT_TRUE(commit->applied);
    EXPECT_FALSE(context->imeCompositionActive());

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, ReplacedUtf8);
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value());
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 2, .caretCodepoint = 2}));

    auto lineBreak = context->routeTextInput(
        window,
        Platform::PlatformFrameId{3},
        3,
        "\n");
    ASSERT_TRUE(lineBreak.has_value());
    EXPECT_TRUE(lineBreak->consumed);
    EXPECT_FALSE(lineBreak->applied);
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, ReplacedUtf8);
}

TEST_F(UITextEditTest, SameTextReplacementCollapsesSelectionAndRepublishesPaint)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "ABA"));
    focusWithTab(textEdit);
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));
    publishLayout();

    const UI::UICommittedPaintView selectedPaint = context->committedPaint();
    const u64 selectedPaintRevision = selectedPaint.paintRevision();
    const UI::UICommittedPaintEntry* selectionHighlight = nullptr;
    const UI::UICommittedPaintEntry* selectedCaret = nullptr;
    for (const UI::UICommittedPaintEntry& entry : selectedPaint.entries()) {
        if (!entry.isGlyph && entry.solidFill.alpha == 190) {
            selectionHighlight = &entry;
        }
        if (!entry.isGlyph && entry.solidFill.red == 255
            && entry.solidFill.green == 255 && entry.solidFill.blue == 255
            && entry.worldRect.width == 2.0F) {
            selectedCaret = &entry;
        }
    }
    ASSERT_NE(selectionHighlight, nullptr);
    ASSERT_NE(selectedCaret, nullptr);
    EXPECT_FLOAT_EQ(
        selectedCaret->worldRect.x,
        selectionHighlight->worldRect.x + selectionHighlight->worldRect.width);
    const float expectedCaretX = selectedCaret->worldRect.x;

    auto replacement = context->routeTextInput(
        window,
        Platform::PlatformFrameId{1},
        1,
        "B");
    ASSERT_TRUE(replacement.has_value())
        << (replacement ? "" : replacement.error().message);
    EXPECT_TRUE(replacement->consumed);
    EXPECT_TRUE(replacement->applied);

    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value()) << (text ? "" : text.error().message);
    EXPECT_EQ(*text, "ABA");
    auto selection = updater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(
        *selection,
        (UI::UITextSelection{.anchorCodepoint = 2, .caretCodepoint = 2}));

    publishLayout();
    const UI::UICommittedPaintView collapsedPaint = context->committedPaint();
    EXPECT_EQ(collapsedPaint.paintRevision(), selectedPaintRevision + 1U);
    const UI::UICommittedPaintEntry* collapsedCaret = nullptr;
    for (const UI::UICommittedPaintEntry& entry : collapsedPaint.entries()) {
        EXPECT_FALSE(!entry.isGlyph && entry.solidFill.alpha == 190);
        if (!entry.isGlyph && entry.solidFill.red == 255
            && entry.solidFill.green == 255 && entry.solidFill.blue == 255
            && entry.worldRect.width == 2.0F) {
            collapsedCaret = &entry;
        }
    }
    ASSERT_NE(collapsedCaret, nullptr);
    EXPECT_FLOAT_EQ(collapsedCaret->worldRect.x, expectedCaretX);
}

TEST_F(UITextEditTest, CommandsNavigateSelectAndDeleteUnicodeScalars)
{
    constexpr std::string_view InitialUtf8 = "A" "\xE4\xBD\xA0" "BC";
    constexpr std::string_view WithoutLastUtf8 = "A" "\xE4\xBD\xA0" "B";
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, InitialUtf8));
    focusWithTab(textEdit);
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 4, .caretCodepoint = 4}));

    const auto routeCommand = [&](u64 sequence,
                                  UI::UITextEditCommand command,
                                  bool extendSelection = false) {
        auto result = context->routeTextEditCommand(
            window,
            Platform::PlatformFrameId{sequence},
            sequence,
            command,
            extendSelection);
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (result) {
            EXPECT_TRUE(result->consumed);
        }
        return result;
    };
    const auto expectSelection = [&](u32 anchor, u32 caret) {
        auto selection = updater.textSelection(textEdit);
        ASSERT_TRUE(selection.has_value()) << (selection ? "" : selection.error().message);
        EXPECT_EQ(selection->anchorCodepoint, anchor);
        EXPECT_EQ(selection->caretCodepoint, caret);
    };

    ASSERT_TRUE(routeCommand(1, UI::UITextEditCommand::MoveLeft).has_value());
    expectSelection(3, 3);
    ASSERT_TRUE(routeCommand(2, UI::UITextEditCommand::MoveLeft, true).has_value());
    expectSelection(3, 2);
    ASSERT_TRUE(routeCommand(3, UI::UITextEditCommand::MoveHome, true).has_value());
    expectSelection(3, 0);
    ASSERT_TRUE(routeCommand(4, UI::UITextEditCommand::MoveRight).has_value());
    expectSelection(3, 3);
    ASSERT_TRUE(routeCommand(5, UI::UITextEditCommand::MoveEnd).has_value());
    expectSelection(4, 4);

    auto backspace = routeCommand(6, UI::UITextEditCommand::Backspace);
    ASSERT_TRUE(backspace.has_value());
    EXPECT_TRUE(backspace->applied);
    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, WithoutLastUtf8);
    expectSelection(3, 3);

    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));
    auto deleteSelection = routeCommand(7, UI::UITextEditCommand::Delete);
    ASSERT_TRUE(deleteSelection.has_value());
    EXPECT_TRUE(deleteSelection->applied);
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "AB");
    expectSelection(1, 1);

    ASSERT_TRUE(routeCommand(8, UI::UITextEditCommand::SelectAll).has_value());
    expectSelection(0, 2);
    auto replaceAll = context->routeTextInput(
        window,
        Platform::PlatformFrameId{9},
        9,
        "Z");
    ASSERT_TRUE(replaceAll.has_value());
    EXPECT_TRUE(replaceAll->consumed);
    EXPECT_TRUE(replaceAll->applied);
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Z");
    expectSelection(1, 1);
}

TEST_F(UITextEditTest, PaintPublishesSelectionPreeditAndCaretAtTheEditPosition)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "ABC"));
    focusWithTab(textEdit);
    assertOk(updater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 2}));
    publishLayout();

    const UI::UICommittedPaintView selectedPaint = context->committedPaint();
    ASSERT_EQ(selectedPaint.size(), 5U);
    const UI::UICommittedPaintEntry* selectionPaint = nullptr;
    const UI::UICommittedPaintEntry* selectedCaret = nullptr;
    for (const UI::UICommittedPaintEntry& entry : selectedPaint.entries()) {
        EXPECT_EQ(entry.node, textEdit);
        if (!entry.isGlyph && entry.solidFill.alpha == 190) {
            selectionPaint = &entry;
        }
        if (!entry.isGlyph && entry.solidFill.red == 255
            && entry.solidFill.green == 255 && entry.solidFill.blue == 255) {
            selectedCaret = &entry;
        }
    }
    ASSERT_NE(selectionPaint, nullptr);
    EXPECT_GT(selectionPaint->worldRect.width, 2.0F);
    ASSERT_NE(selectedCaret, nullptr);
    EXPECT_FLOAT_EQ(selectedCaret->worldRect.width, 2.0F);
    EXPECT_GT(selectedCaret->worldRect.x, selectionPaint->worldRect.x);

    auto composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "XY",
        1,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value());
    publishLayout();

    const UI::UICommittedPaintView preeditPaint = context->committedPaint();
    ASSERT_EQ(preeditPaint.size(), 5U);
    usize preeditGlyphCount = 0;
    float firstPreeditX = 0.0F;
    float secondPreeditX = 0.0F;
    const UI::UICommittedPaintEntry* preeditCaret = nullptr;
    for (const UI::UICommittedPaintEntry& entry : preeditPaint.entries()) {
        if (entry.isGlyph && entry.solidFill.red == 0
            && entry.solidFill.green == 180 && entry.solidFill.blue == 255) {
            if (preeditGlyphCount == 0) {
                firstPreeditX = entry.worldRect.x;
            } else if (preeditGlyphCount == 1) {
                secondPreeditX = entry.worldRect.x;
            }
            ++preeditGlyphCount;
        }
        if (!entry.isGlyph && entry.solidFill.red == 255
            && entry.solidFill.green == 255 && entry.solidFill.blue == 255) {
            preeditCaret = &entry;
        }
        EXPECT_FALSE(!entry.isGlyph && entry.solidFill.alpha == 190);
    }
    EXPECT_EQ(preeditGlyphCount, 2U);
    EXPECT_GT(secondPreeditX, firstPreeditX);
    ASSERT_NE(preeditCaret, nullptr);
    EXPECT_FLOAT_EQ(preeditCaret->worldRect.x, secondPreeditX);
}

TEST_F(UITextEditTest, PointerSelectionUsesRasterizedVariableGlyphAdvances)
{
    auto localContextResult = UI::UIContext::Create(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 8,
            .routePathCapacity = 8,
            .textByteCapacity = 64,
        },
        std::make_unique<VariableAdvanceTextRasterizer>());
    ASSERT_TRUE(localContextResult.has_value())
        << (localContextResult ? "" : localContextResult.error().message);
    auto localContext = std::move(*localContextResult);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);
    assertOk(localUpdater.setLayoutStyle(
        localRoot.rootNodeId(),
        fixedSize(120.0F, 40.0F)));
    auto textEditResult = localUpdater.createTextEdit(localRoot.rootNodeId());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;
    UI::UILayoutStyle textEditStyle = fixedSize(120.0F, 32.0F);
    textEditStyle.padding.left = 12.0F;
    assertOk(localUpdater.setLayoutStyle(textEdit, textEditStyle));
    assertOk(localUpdater.setText(textEdit, "Wi"));
    assertOk(localContext->commitLayout({.width = 120.0F, .height = 40.0F}));

    auto down = localContext->routePointerInput(
        makePrimaryPointerDown(window, 1, 30.0F, 10.0F));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    auto selection = localUpdater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(selection->anchorCodepoint, 1U);
    EXPECT_EQ(selection->caretCodepoint, 1U);
}

TEST_F(UITextEditTest, RejectsUnknownCommandWithoutClearingComposition)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "AB"));
    focusWithTab(textEdit);
    auto composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "x",
        1,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value())
        << (composition ? "" : composition.error().message);
    ASSERT_TRUE(context->imeCompositionActive());

    auto command = context->routeTextEditCommand(
        window,
        Platform::PlatformFrameId{2},
        2,
        static_cast<UI::UITextEditCommand>(255),
        false);
    ASSERT_FALSE(command.has_value());
    EXPECT_EQ(command.error().code, UI::UIErrorCode::InvalidText);
    EXPECT_TRUE(context->imeCompositionActive());
    EXPECT_EQ(context->imePreeditUtf8(), "x");
}

TEST_F(UITextEditTest, ReleasedTextAllocationCannotOverwriteAnotherNode)
{
    const UI::UINodeId first = createTextEdit();
    const UI::UINodeId second = createTextEdit();
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());

    assertOk(updater.setText(first, "AAAA"));
    assertOk(updater.setText(second, "KEEP-ME"));
    auto secondText = updater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "KEEP-ME");

    assertOk(updater.setText(first, ""));
    assertOk(updater.setText(first, "12345678"));
    auto firstText = updater.text(first);
    ASSERT_TRUE(firstText.has_value());
    EXPECT_EQ(*firstText, "12345678");
    secondText = updater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "KEEP-ME");
    EXPECT_EQ(context->statistics().textByteUsed, 15U);

    assertOk(updater.setText(first, "abcdefghijkl"));
    firstText = updater.text(first);
    ASSERT_TRUE(firstText.has_value());
    EXPECT_EQ(*firstText, "abcdefghijkl");
    secondText = updater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "KEEP-ME");
    EXPECT_EQ(context->statistics().textByteUsed, 19U);
}

TEST_F(UITextEditTest, SmallTextArenaReusesReleasedLargeBlockAtRequestedSize)
{
    auto localContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .textByteCapacity = 16,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);

    auto firstResult = localUpdater.createTextEdit(localRoot.rootNodeId());
    ASSERT_TRUE(firstResult.has_value()) << (firstResult ? "" : firstResult.error().message);
    const UI::UINodeId first = *firstResult;
    auto secondResult = localUpdater.createTextEdit(localRoot.rootNodeId());
    ASSERT_TRUE(secondResult.has_value()) << (secondResult ? "" : secondResult.error().message);
    const UI::UINodeId second = *secondResult;

    assertOk(localUpdater.setText(first, "abcdefghijkl"));
    assertOk(localUpdater.setText(second, "xy"));
    EXPECT_EQ(localContext->statistics().textByteUsed, 14U);

    assertOk(localUpdater.setText(first, ""));
    EXPECT_EQ(localContext->statistics().textByteUsed, 2U);

    assertOk(localUpdater.setText(first, "Z"));
    EXPECT_EQ(localContext->statistics().textByteUsed, 3U);

    assertOk(localUpdater.setText(second, "0123456789"));
    auto secondText = localUpdater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "0123456789");
    EXPECT_EQ(localContext->statistics().textByteUsed, 11U);

    assertOk(localUpdater.setText(first, "PQRS"));
    auto firstText = localUpdater.text(first);
    ASSERT_TRUE(firstText.has_value());
    EXPECT_EQ(*firstText, "PQRS");
    secondText = localUpdater.text(second);
    ASSERT_TRUE(secondText.has_value());
    EXPECT_EQ(*secondText, "0123456789");
    EXPECT_EQ(localContext->statistics().textByteUsed, 14U);
}

TEST_F(UITextEditTest, CancelPointerInteractionClearsTextEditFocusAndAccept)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "ABC"));
    publishLayout();

    auto down = context->routePointerInput(makePrimaryPointerDown(window, 1));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->defaultActionFocus(), textEdit);
    EXPECT_EQ(context->imeFocus(), textEdit);

    assertOk(context->cancelPointerInteraction(window));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_FALSE(context->imeFocus().hasValue());

    auto accept = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{2},
        2,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(accept.has_value()) << (accept ? "" : accept.error().message);
    EXPECT_FALSE(accept->consumed);
    EXPECT_FALSE(accept->activated);
}

TEST_F(UITextEditTest, CommitClearsTextEditFocusWhenCollapsedOrIgnored)
{
    const UI::UINodeId textEdit = createTextEdit();
    ASSERT_TRUE(textEdit.hasValue());
    assertOk(updater.setText(textEdit, "Stable"));
    focusWithTab(textEdit);

    UI::UILayoutStyle collapsedStyle = fixedSize(240.0F, 32.0F);
    collapsedStyle.visibility = UI::UIVisibility::Collapsed;
    assertOk(updater.setLayoutStyle(textEdit, collapsedStyle));
    publishLayout();
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_FALSE(context->imeFocus().hasValue());

    auto inputAfterCollapse = context->routeTextInput(
        window,
        Platform::PlatformFrameId{1},
        1,
        "X");
    ASSERT_TRUE(inputAfterCollapse.has_value())
        << (inputAfterCollapse ? "" : inputAfterCollapse.error().message);
    EXPECT_FALSE(inputAfterCollapse->consumed);
    EXPECT_FALSE(inputAfterCollapse->applied);
    auto text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Stable");

    assertOk(updater.setLayoutStyle(textEdit, fixedSize(240.0F, 32.0F)));
    assertOk(updater.setPointerHitPolicy(textEdit, UI::UIPointerHitPolicy::Targetable));
    focusWithTab(textEdit);

    assertOk(updater.setPointerHitPolicy(textEdit, UI::UIPointerHitPolicy::Ignore));
    publishLayout();
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_FALSE(context->imeFocus().hasValue());

    auto inputAfterIgnore = context->routeTextInput(
        window,
        Platform::PlatformFrameId{2},
        2,
        "Y");
    ASSERT_TRUE(inputAfterIgnore.has_value())
        << (inputAfterIgnore ? "" : inputAfterIgnore.error().message);
    EXPECT_FALSE(inputAfterIgnore->consumed);
    EXPECT_FALSE(inputAfterIgnore->applied);
    text = updater.text(textEdit);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Stable");
}

TEST_F(UITextEditTest, ImePreeditDirtyQueueFailureLeavesPreviousComposition)
{
    auto localContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
            .textByteCapacity = 64,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);

    auto textEditResult = localUpdater.createTextEdit(localRoot.rootNodeId());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;
    auto blockerResult = localUpdater.createPanel(localRoot.rootNodeId());
    ASSERT_TRUE(blockerResult.has_value())
        << (blockerResult ? "" : blockerResult.error().message);
    const UI::UINodeId blocker = *blockerResult;
    assertOk(localContext->commitLayout({.width = 320.0F, .height = 120.0F}));

    auto focus = localContext->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_TRUE(focus->consumed);
    EXPECT_EQ(focus->focus, textEdit);
    EXPECT_EQ(localContext->imeFocus(), textEdit);

    auto started = localContext->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "old",
        2,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(started.has_value()) << (started ? "" : started.error().message);
    EXPECT_TRUE(started->consumed);
    EXPECT_TRUE(started->applied);
    EXPECT_TRUE(localContext->imeCompositionActive());
    EXPECT_EQ(localContext->imePreeditUtf8(), "old");
    EXPECT_EQ(localContext->imePreeditCursorCodepoint(), 2U);
    assertOk(localContext->commitLayout({.width = 320.0F, .height = 120.0F}));

    assertOk(localUpdater.setPointerHitPolicy(
        blocker,
        UI::UIPointerHitPolicy::Targetable));
    EXPECT_EQ(localContext->statistics().dirtyQueuePendingCount, 1U);

    auto rejected = localContext->routeTextComposition(
        window,
        Platform::PlatformFrameId{2},
        2,
        "new",
        1,
        Platform::TextCompositionStage::Updated);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_TRUE(localContext->imeCompositionActive());
    EXPECT_EQ(localContext->imePreeditUtf8(), "old");
    EXPECT_EQ(localContext->imePreeditCursorCodepoint(), 2U);
}

TEST_F(UITextEditTest, PointerSelectionDirtyQueueFailurePreservesSelectionAndFocus)
{
    auto localContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 5,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 2,
            .textByteCapacity = 64,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);

    auto textEditResult = localUpdater.createTextEdit(localRoot.rootNodeId());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;
    auto firstBlockerResult = localUpdater.createPanel(localRoot.rootNodeId());
    ASSERT_TRUE(firstBlockerResult.has_value())
        << (firstBlockerResult ? "" : firstBlockerResult.error().message);
    const UI::UINodeId firstBlocker = *firstBlockerResult;
    auto secondBlockerResult = localUpdater.createPanel(localRoot.rootNodeId());
    ASSERT_TRUE(secondBlockerResult.has_value())
        << (secondBlockerResult ? "" : secondBlockerResult.error().message);
    const UI::UINodeId secondBlocker = *secondBlockerResult;

    assertOk(localUpdater.setLayoutStyle(
        localRoot.rootNodeId(),
        fixedSize(320.0F, 120.0F)));
    assertOk(localUpdater.setLayoutStyle(textEdit, fixedSize(240.0F, 32.0F)));
    assertOk(localUpdater.setText(textEdit, "ABCD"));
    assertOk(localContext->commitLayout({.width = 320.0F, .height = 120.0F}));

    auto down = localContext->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        1.0F,
        10.0F));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(localContext->defaultActionFocus(), textEdit);
    EXPECT_EQ(localContext->imeFocus(), textEdit);
    auto selection = localUpdater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(*selection, (UI::UITextSelection{}));
    assertOk(localContext->commitLayout({.width = 320.0F, .height = 120.0F}));

    assertOk(localUpdater.setPointerHitPolicy(
        firstBlocker,
        UI::UIPointerHitPolicy::Targetable));
    assertOk(localUpdater.setPointerHitPolicy(
        secondBlocker,
        UI::UIPointerHitPolicy::Targetable));
    EXPECT_EQ(localContext->statistics().dirtyQueuePendingCount, 2U);

    auto rejected = localContext->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::Move,
        2,
        100.0F,
        10.0F));
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    selection = localUpdater.textSelection(textEdit);
    ASSERT_TRUE(selection.has_value())
        << (selection ? "" : selection.error().message);
    EXPECT_EQ(*selection, (UI::UITextSelection{}));
    EXPECT_EQ(localContext->defaultActionFocus(), textEdit);
    EXPECT_EQ(localContext->imeFocus(), textEdit);
    EXPECT_EQ(localContext->statistics().dirtyQueuePendingCount, 2U);
}

TEST_F(UITextEditTest, PaintSnapshotCapacityFiveAcceptsSelectionAndPreedit)
{
    auto localContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 5,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 5,
            .textByteCapacity = 64,
        });
    ASSERT_NE(localContext, nullptr);
    auto localRoot = createRoot(*localContext);
    ASSERT_TRUE(localRoot.hasValue());
    auto localUpdater = createUpdater(*localContext, localRoot);

    assertOk(localUpdater.setLayoutStyle(
        localRoot.rootNodeId(),
        fixedSize(200.0F, 40.0F)));
    auto textEditResult = localUpdater.createTextEdit(localRoot.rootNodeId());
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;
    assertOk(localUpdater.setLayoutStyle(textEdit, fixedSize(120.0F, 32.0F)));
    assertOk(localUpdater.setText(textEdit, "ABCD"));
    assertOk(localContext->commitLayout({.width = 200.0F, .height = 40.0F}));

    auto focus = localContext->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_EQ(focus->focus, textEdit);

    assertOk(localUpdater.setTextSelection(
        textEdit,
        {.anchorCodepoint = 1, .caretCodepoint = 3}));

    auto composition = localContext->routeTextComposition(
        window,
        Platform::PlatformFrameId{1},
        1,
        "XY",
        2,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value())
        << (composition ? "" : composition.error().message);
    EXPECT_TRUE(composition->consumed);
    EXPECT_TRUE(composition->applied);
    assertOk(localContext->commitLayout({.width = 200.0F, .height = 40.0F}));

    const UI::UICommittedPaintView paint = localContext->committedPaint();
    ASSERT_EQ(paint.size(), 5U);
    EXPECT_EQ(paint.entries()[0].node, textEdit);
    EXPECT_EQ(paint.entries()[1].node, textEdit);
    EXPECT_EQ(paint.entries()[2].node, textEdit);
    EXPECT_EQ(paint.entries()[3].node, textEdit);
    EXPECT_EQ(paint.entries()[4].node, textEdit);
    EXPECT_TRUE(localContext->imeCompositionActive());
    EXPECT_EQ(localContext->imePreeditUtf8(), "XY");
}

TEST_F(UITextEditTest, NestedInButtonPrimaryUpAndStrayUpDoNotActivateParent)
{
    auto buttonResult = updater.createButton(root.rootNodeId());
    ASSERT_TRUE(buttonResult.has_value()) << (buttonResult ? "" : buttonResult.error().message);
    const UI::UINodeId button = *buttonResult;
    auto textEditResult = updater.createTextEdit(button);
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;

    int activations = 0;
    assertOk(updater.setLayoutStyle(button, fixedSize(160.0F, 48.0F)));
    assertOk(updater.setLayoutStyle(textEdit, fixedSize(120.0F, 32.0F)));
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout();

    auto down = context->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_EQ(down->pointQuery.target.node, textEdit);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->defaultActionFocus(), textEdit);
    EXPECT_EQ(context->imeFocus(), textEdit);
    auto pressed = updater.isButtonPressed(button);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    EXPECT_FALSE(*pressed);

    auto up = context->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_EQ(up->pointQuery.target.node, textEdit);
    EXPECT_TRUE(up->consumed);
    pressed = updater.isButtonPressed(button);
    ASSERT_TRUE(pressed.has_value());
    EXPECT_FALSE(*pressed);
    EXPECT_EQ(activations, 0);

    auto strayUp = context->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        3));
    ASSERT_TRUE(strayUp.has_value()) << (strayUp ? "" : strayUp.error().message);
    EXPECT_EQ(strayUp->pointQuery.target.node, textEdit);
    EXPECT_FALSE(strayUp->consumed);
    EXPECT_EQ(activations, 0);
    EXPECT_EQ(context->defaultActionFocus(), textEdit);
    EXPECT_EQ(context->imeFocus(), textEdit);
}

TEST_F(UITextEditTest, NestedInSliderPrimaryUpAndStrayUpDoNotDragParent)
{
    auto sliderResult = updater.createSlider(root.rootNodeId());
    ASSERT_TRUE(sliderResult.has_value()) << (sliderResult ? "" : sliderResult.error().message);
    const UI::UINodeId slider = *sliderResult;
    auto textEditResult = updater.createTextEdit(slider);
    ASSERT_TRUE(textEditResult.has_value())
        << (textEditResult ? "" : textEditResult.error().message);
    const UI::UINodeId textEdit = *textEditResult;

    int changes = 0;
    assertOk(updater.setLayoutStyle(slider, fixedSize(160.0F, 48.0F)));
    assertOk(updater.setLayoutStyle(textEdit, fixedSize(120.0F, 32.0F)));
    assertOk(updater.setSliderRange(slider, 0.0F, 100.0F, 1.0F));
    assertOk(updater.setSliderValue(slider, 25.0F));
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{
            [&changes](const UI::UISliderChangeEvent&) noexcept {
                ++changes;
            }}));
    publishLayout();

    auto down = context->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        10.0F,
        10.0F));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_EQ(down->pointQuery.target.node, textEdit);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->defaultActionFocus(), textEdit);
    EXPECT_EQ(context->imeFocus(), textEdit);
    auto dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value()) << (dragging ? "" : dragging.error().message);
    EXPECT_FALSE(*dragging);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value()) << (value ? "" : value.error().message);
    EXPECT_FLOAT_EQ(*value, 25.0F);
    EXPECT_EQ(changes, 0);

    auto up = context->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        100.0F,
        10.0F));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_EQ(up->pointQuery.target.node, textEdit);
    EXPECT_TRUE(up->consumed);
    dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    EXPECT_FALSE(*dragging);
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 25.0F);
    EXPECT_EQ(changes, 0);

    auto strayUp = context->routePointerInput(makePrimaryPointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        3,
        100.0F,
        10.0F));
    ASSERT_TRUE(strayUp.has_value()) << (strayUp ? "" : strayUp.error().message);
    EXPECT_EQ(strayUp->pointQuery.target.node, textEdit);
    EXPECT_FALSE(strayUp->consumed);
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 25.0F);
    EXPECT_EQ(changes, 0);
    EXPECT_EQ(context->defaultActionFocus(), textEdit);
    EXPECT_EQ(context->imeFocus(), textEdit);
}

} // namespace
} // namespace Tina::Tests
