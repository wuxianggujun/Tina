#pragma once

#include <tina/asset/AssetHandle.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/editor/SpriteAnimationAuthoringDocument.hpp>
#include <tina/scene/SpriteAnimator2D.hpp>

#include <functional>
#include <memory_resource>
#include <optional>

namespace Tina::EditorApp::Detail {

// Owner of the Editor's SpriteAnimationClip preview playback state: the
// isolated SpriteAnimator2D, play/pause intent, resolved-sprite availability,
// timeline selection, and the queued frame-slot click. The preview world
// binding (writing the resolved sprite into an entity) stays with the caller.
class EditorAnimationPreview final {
  public:
    using ResolveSprite =
        std::function<Asset::AssetHandle(Core::AssetId)>;

    // Rebuilds the animator from the authoring document. resolveSprite must
    // return an empty handle for unresolved sprites; any unresolved frame
    // downgrades the preview (animator reset, not an error). On success the
    // animator starts paused and the selection is clamped to the frame count.
    [[nodiscard]] Core::Status rebuild(
        const Tina::Editor::SpriteAnimationAuthoringDocument& document,
        std::pmr::memory_resource& memory,
        const ResolveSprite& resolveSprite);

    void resetAnimator() noexcept
    {
        m_animator.reset();
        m_playing = false;
        m_previewAvailable = false;
    }

    [[nodiscard]] bool hasAnimator() const noexcept { return m_animator.has_value(); }
    [[nodiscard]] Scene::SpriteAnimator2D& animator() noexcept { return *m_animator; }

    [[nodiscard]] bool playing() const noexcept { return m_playing; }
    void setPlaying(bool playing) noexcept
    {
        m_playing = playing && m_animator.has_value();
        if (m_animator.has_value()) {
            if (m_playing) {
                m_animator->play();
            } else {
                m_animator->pause();
            }
        }
    }

    [[nodiscard]] bool previewAvailable() const noexcept { return m_previewAvailable; }
    void setPreviewAvailable(bool available) noexcept { m_previewAvailable = available; }

    [[nodiscard]] Core::u32 selectedFrameIndex() const noexcept { return m_selectedFrameIndex; }
    void setSelectedFrameIndex(Core::u32 index) noexcept { m_selectedFrameIndex = index; }
    void clampSelection(Core::u32 frameCount) noexcept
    {
        if (frameCount == 0U) {
            m_selectedFrameIndex = 0U;
            return;
        }
        m_selectedFrameIndex = (std::min)(m_selectedFrameIndex, frameCount - 1U);
    }

    [[nodiscard]] Core::u32 visibleFrameStart() const noexcept { return m_visibleFrameStart; }
    void setVisibleFrameStart(Core::u32 start) noexcept { m_visibleFrameStart = start; }

    void queueFrameSelection(Core::u32 slot) noexcept { m_pendingFrameSelection = slot; }
    [[nodiscard]] std::optional<Core::u32> takePendingFrameSelection() noexcept
    {
        auto pending = m_pendingFrameSelection;
        m_pendingFrameSelection.reset();
        return pending;
    }

  private:
    std::optional<Scene::SpriteAnimator2D> m_animator{};
    bool m_playing = false;
    bool m_previewAvailable = false;
    Core::u32 m_selectedFrameIndex = 0;
    Core::u32 m_visibleFrameStart = 0;
    std::optional<Core::u32> m_pendingFrameSelection{};
};

} // namespace Tina::EditorApp::Detail
