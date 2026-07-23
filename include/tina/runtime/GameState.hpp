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
    bool blocksUIInputBelow = false;
    bool blocksFixedUpdateBelow = false;
    bool blocksFrameUpdateBelow = false;
    bool blocksRenderBelow = false;
};

class IGameState {
public:
    virtual ~IGameState() noexcept = default;

    virtual Core::Status onEnter(GameStateEnterContext& context) = 0;
    virtual void onExit(GameStateExitContext& context) noexcept = 0;
    [[nodiscard]] virtual GameStatePolicy initialPolicy() const noexcept = 0;

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
