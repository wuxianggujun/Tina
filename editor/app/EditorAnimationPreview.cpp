#include "EditorAnimationPreview.hpp"

#include <tina/core/error/Error.hpp>

#include <algorithm>
#include <new>
#include <vector>

namespace Tina::EditorApp::Detail {
namespace {

[[nodiscard]] Scene::SpriteAnimationPlaybackMode
sceneAnimationMode(AssetFormat::SpriteAnimationPlaybackMode mode) noexcept
{
    switch (mode) {
    case AssetFormat::SpriteAnimationPlaybackMode::Once:
        return Scene::SpriteAnimationPlaybackMode::Once;
    case AssetFormat::SpriteAnimationPlaybackMode::PingPong:
        return Scene::SpriteAnimationPlaybackMode::PingPong;
    case AssetFormat::SpriteAnimationPlaybackMode::Loop:
    default:
        return Scene::SpriteAnimationPlaybackMode::Loop;
    }
}

} // namespace

Core::Status EditorAnimationPreview::rebuild(
    const Tina::Editor::SpriteAnimationAuthoringDocument& document,
    std::pmr::memory_resource& memory,
    const ResolveSprite& resolveSprite)
{
    std::vector<Scene::SpriteAnimationFrame2D> resolvedFrames;
    try {
        resolvedFrames.reserve(document.frameCount());
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Animation preview frame allocation failed");
    }
    for (Core::u32 frameIndex = 0; frameIndex < document.frameCount(); ++frameIndex) {
        const auto frame = document.frameAt(frameIndex);
        if (!frame) {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "Animation document frame disappeared during preview rebuild");
        }
        const Asset::AssetHandle sprite =
            resolveSprite ? resolveSprite(frame->spriteId) : Asset::AssetHandle{};
        if (!sprite) {
            resetAnimator();
            return Core::success();
        }
        resolvedFrames.push_back(Scene::SpriteAnimationFrame2D{
            .sprite = Scene::SpriteRenderer2D{.sprite = sprite},
            .duration = Core::Duration{frame->durationSeconds},
        });
    }
    auto animator = Scene::SpriteAnimator2D::Create(
        Scene::SpriteAnimationClip2D{
            .frames = resolvedFrames,
            .playbackMode = sceneAnimationMode(document.playbackMode()),
        },
        memory);
    if (!animator) {
        return Core::failure(std::move(animator.error()));
    }
    animator->pause();
    m_animator.reset();
    m_animator.emplace(std::move(*animator));
    m_playing = false;
    m_previewAvailable = true;
    clampSelection(static_cast<Core::u32>(document.frameCount()));
    return Core::success();
}

} // namespace Tina::EditorApp::Detail
