#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/Clipboard.hpp>
#include <tina/platform/Input.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace Tina::UI {

class UIContext;

struct UITextInputRouteResult final {
    bool consumed = false;
    bool applied = false;
};

class UITextSystem final {
  public:
    // Logical line-box size, using the same font/fallback chain, shaping and
    // constrained wrapping as retained text. Excludes widget padding/margin.
    // No nodes, glyph images, paint, or layout publication are
    // produced. Missing fonts use measurePlaceholderText; other errors propagate.
    // Owner-thread only. The result owns its metrics; shaping caches may change.
    [[nodiscard]] Core::Result<UITextMetrics> measureText(
        std::string_view utf8, const UITextStyle& style,
        UITextMeasureOptions options = {}) const;

    // Fonts and optional seeds are startup-only: configure before creating any
    // nodes. Live snapshots retain atlas UVs until their next successful commit.
    [[nodiscard]] Core::Status openTextFont(
        std::span<const std::byte> fontBytes, i32 faceIndex = 0);
    // Configure font fallback/optional baked seed before authoring any nodes.
    [[nodiscard]] Core::Status addFallbackFont(std::span<const std::byte> fontBytes, i32 faceIndex = 0);
    [[nodiscard]] Core::Status primeFontGlyphCache(std::span<const std::byte> cooked);
    [[nodiscard]] Core::Status setRasterScale(UITextRasterScale scale);
    [[nodiscard]] UINodeId imeFocus() const noexcept;
    [[nodiscard]] bool imeCompositionActive() const noexcept;
    [[nodiscard]] std::string_view imePreeditUtf8() const noexcept;
    [[nodiscard]] u32 imePreeditCursorCodepoint() const noexcept;
    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextComposition(Platform::WindowId window,
                         Platform::PlatformFrameId platformFrame,
                         u64 sourceSequence, std::string_view preeditUtf8,
                         u32 cursorCodepoint,
                         Platform::TextCompositionStage stage);
    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextInput(Platform::WindowId window,
                   Platform::PlatformFrameId platformFrame,
                   u64 sourceSequence, std::string_view committedUtf8);
    [[nodiscard]] Core::Result<UITextInputRouteResult>
    routeTextEditCommand(Platform::WindowId window,
                         Platform::PlatformFrameId platformFrame,
                         u64 sourceSequence, UITextEditCommand command,
                         bool extendSelection = false);
    // Routes a clipboard command against the focused TextEdit.
    //
    // The clipboard is a parameter rather than context state on purpose. The
    // UIContext and the platform backend that owns the clipboard are siblings
    // under EngineHost, so a stored pointer would add an invisible "backend must
    // outlive context" ordering rule enforced only by a crash. Passing it at the
    // call site means the caller, which owns both, proves liveness where the call
    // happens.
    //
    // Returns Unsupported when the platform has no clipboard, so a product can
    // tell a missing capability from a failed operation.
    [[nodiscard]] Core::Result<UITextClipboardRouteResult>
    routeTextClipboardCommand(Platform::WindowId window,
                              Platform::PlatformFrameId platformFrame,
                              u64 sourceSequence,
                              UITextClipboardCommand command,
                              Platform::IClipboard& clipboard);

  private:
    friend class UIContext;

    explicit UITextSystem(UIContext& context) noexcept : m_context(&context) {}

    UIContext* m_context = nullptr;
};

} // namespace Tina::UI
