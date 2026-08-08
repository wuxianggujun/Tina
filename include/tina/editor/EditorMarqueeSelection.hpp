#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <array>
#include <span>

namespace Tina::Editor {

inline constexpr Core::usize EditorMarqueeSelectionCapacity = 512;

struct EditorMarqueeScreenRect final {
    float x0 = 0.0F;
    float y0 = 0.0F;
    float x1 = 0.0F;
    float y1 = 0.0F;

    friend bool operator==(const EditorMarqueeScreenRect&,
                           const EditorMarqueeScreenRect&) = default;
};

struct EditorMarqueeCandidate final {
    Core::u64 stableId = 0;
    EditorMarqueeScreenRect screenBounds{};
};

enum class EditorMarqueeSelectionMode : Core::u8 {
    Replace,
    Add,
    Toggle,
};

struct EditorMarqueeSelectionModifiers final {
    bool shift = false;
    bool control = false;
};

// Control takes precedence when both modifiers are held, matching the toggle
// operation used by desktop editor selection models.
[[nodiscard]] constexpr EditorMarqueeSelectionMode editorMarqueeSelectionMode(
    EditorMarqueeSelectionModifiers modifiers) noexcept
{
    if (modifiers.control) {
        return EditorMarqueeSelectionMode::Toggle;
    }
    return modifiers.shift ? EditorMarqueeSelectionMode::Add
                           : EditorMarqueeSelectionMode::Replace;
}

// Fixed-capacity selection publication. Stable ids are non-zero. All returned
// spans are sorted by stableId and remain valid for the lifetime of this value.
class EditorMarqueeSelection final {
  public:
    [[nodiscard]] static Core::Result<EditorMarqueeSelection> Evaluate(
        EditorMarqueeScreenRect marquee,
        std::span<const EditorMarqueeCandidate> candidates,
        std::span<const Core::u64> currentSelection,
        EditorMarqueeSelectionMode mode);

    [[nodiscard]] std::span<const Core::u64> selection() const noexcept
    {
        return std::span(m_selection.data(), m_selectionCount);
    }

    [[nodiscard]] std::span<const Core::u64> added() const noexcept
    {
        return std::span(m_added.data(), m_addedCount);
    }

    [[nodiscard]] std::span<const Core::u64> removed() const noexcept
    {
        return std::span(m_removed.data(), m_removedCount);
    }

    [[nodiscard]] bool changed() const noexcept
    {
        return m_addedCount != 0U || m_removedCount != 0U;
    }

  private:
    std::array<Core::u64, EditorMarqueeSelectionCapacity> m_selection{};
    std::array<Core::u64, EditorMarqueeSelectionCapacity> m_added{};
    std::array<Core::u64, EditorMarqueeSelectionCapacity> m_removed{};
    Core::usize m_selectionCount = 0;
    Core::usize m_addedCount = 0;
    Core::usize m_removedCount = 0;
};

} // namespace Tina::Editor
