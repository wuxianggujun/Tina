#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/platform/Input.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>
#include <tina/ui/UINodeId.hpp>
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

  private:
    friend class UIContext;

    explicit UITextSystem(UIContext& context) noexcept : m_context(&context) {}

    UIContext* m_context = nullptr;
};

} // namespace Tina::UI
