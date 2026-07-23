#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PhaseContexts.hpp>

#include <memory>

namespace Tina {

class IGameApplication {
public:
    virtual ~IGameApplication() noexcept = default;

    [[nodiscard]] virtual Core::Result<std::unique_ptr<IGameState>> createInitialState(
        GameStartupContext& context) = 0;

    virtual void onShutdown(GameShutdownContext& context) noexcept = 0;
};

} // namespace Tina
