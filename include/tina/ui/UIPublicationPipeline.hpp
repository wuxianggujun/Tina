#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIAccessibility.hpp>
#include <tina/ui/UICommittedHit.hpp>
#include <tina/ui/UICommittedLayout.hpp>
#include <tina/ui/UICommittedPaint.hpp>
#include <tina/ui/UICommittedStructure.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UILayoutDebugger.hpp>

#include <optional>
#include <span>

namespace Tina::UI {

class UIContext;

class UIPublicationPipeline final {
  public:
    [[nodiscard]] Core::Status commitStructure();
    [[nodiscard]] UICommittedStructureView committedStructure() const noexcept;
    [[nodiscard]] Core::Status commitLayout(UILogicalSize viewportSize);
    [[nodiscard]] UICommittedLayoutView committedLayout() const noexcept;
    [[nodiscard]] UILayoutDebugSnapshotView committedLayoutDebugSnapshot() const noexcept;
    [[nodiscard]] UICommittedHitView committedHit() const noexcept;
    [[nodiscard]] UICommittedPaintView committedPaint() const noexcept;
    [[nodiscard]] std::optional<UILogicalRect>
    committedTextInputCaretRect() const noexcept;
    [[nodiscard]] UICommittedSemanticsView committedSemantics() const noexcept;
    [[nodiscard]] std::span<const u8> glyphAtlasPixels() const noexcept;
    [[nodiscard]] u32 glyphAtlasWidth() const noexcept;
    [[nodiscard]] u32 glyphAtlasHeight() const noexcept;

  private:
    friend class UIContext;

    explicit UIPublicationPipeline(UIContext& context) noexcept : m_context(&context) {}

    UIContext* m_context = nullptr;
};

} // namespace Tina::UI
