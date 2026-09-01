#pragma once

#include <tina/core/error/Result.hpp>

namespace Tina {

class GameStateEnterContext;
class GameStateExitContext;
class FixedUpdateContext;
class FrameUpdateContext;
class RenderSceneExtractionContext;
class UIUpdateContext;

struct GameStatePolicy final {
    bool blocksGameplayInputBelow = false;
    // Gates lower-layer IGameState::updateUI only (not the current-frame UI route,
    // which runs before stack dispatch). Name matches behavior; do not read as
    // "blocks UI pointer/IME routing".
    bool blocksUIUpdateBelow = false;
    bool blocksFixedUpdateBelow = false;
    bool blocksFrameUpdateBelow = false;
    bool blocksRenderBelow = false;
};

class IGameState {
public:
    virtual ~IGameState() noexcept = default;

    // Every hook is defaulted, so the smallest usable state overrides only the one it
    // cares about. A defaulted policy blocks nothing, which is the answer for a game
    // whose stack never holds two states at once.
    virtual Core::Status onEnter(GameStateEnterContext&)
    {
        return Core::success();
    }

    virtual void onExit(GameStateExitContext&) noexcept {}

    [[nodiscard]] virtual GameStatePolicy initialPolicy() const noexcept
    {
        return GameStatePolicy{};
    }

    virtual Core::Status fixedUpdate(FixedUpdateContext&)
    {
        return Core::success();
    }

    virtual Core::Status updateFrame(FrameUpdateContext&)
    {
        return Core::success();
    }

    virtual Core::Status extractRenderScene(RenderSceneExtractionContext&) const
    {
        return Core::success();
    }

    virtual Core::Status updateUI(UIUpdateContext&)
    {
        return Core::success();
    }
};

} // namespace Tina
