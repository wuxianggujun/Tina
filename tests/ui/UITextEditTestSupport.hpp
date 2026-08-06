#pragma once

#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace Tina::Tests::UITextEditTestSupport {

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
            .baselineFromLineTop = style.logicalSize * style.lineHeightScale,
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

[[nodiscard]] inline std::unique_ptr<UI::UIContext> createContext(
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

[[nodiscard]] inline std::unique_ptr<UI::UIContext> createContext(
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

[[nodiscard]] inline UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] inline UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

[[nodiscard]] inline UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

inline void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] inline UI::UIPointerInputEvent makePrimaryPointerDown(
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

[[nodiscard]] inline UI::UIPointerInputEvent makePrimaryPointerInput(
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
        auto result = updater.createElement(root.rootNodeId(), UI::makeTextEditElement());
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

} // namespace Tina::Tests::UITextEditTestSupport
