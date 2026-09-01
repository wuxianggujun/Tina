#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PhaseContexts.hpp>

#include <memory>

namespace Tina {

class IGameApplication {
public:
    virtual ~IGameApplication() noexcept = default;

    // The one hook with no sensible default: an application that names no starting state
    // has nothing to run.
    [[nodiscard]] virtual Core::Result<std::unique_ptr<IGameState>> createInitialState(
        GameStartupContext& context) = 0;

    virtual void onShutdown(GameShutdownContext&) noexcept {}
};

} // namespace Tina
