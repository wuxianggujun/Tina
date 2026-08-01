#include <tina/core/time/MonotonicClock.hpp>
#include <tina/platform/headless/HeadlessPlatformFactory.hpp>
#include <tina/render/null/NullRenderDeviceFactory.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>
#include <tina/ui/UI.hpp>

#include <iostream>
#include <memory>

namespace {

class ConsumerState final : public Tina::IGameState {
  public:
    Tina::Core::Status onEnter(Tina::GameStateEnterContext&) override
    {
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        context.requestExitAfterFrame();
        return Tina::Core::success();
    }
};

class ConsumerApplication final : public Tina::IGameApplication {
  public:
    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        return std::unique_ptr<Tina::IGameState>{std::make_unique<ConsumerState>()};
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
    }
};

[[nodiscard]] Tina::EngineCompositionFactories createFactories()
{
    return Tina::EngineCompositionFactories{
        .createMonotonicClock = []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
            return std::unique_ptr<Tina::Core::IMonotonicClock>{std::make_unique<Tina::Core::SteadyMonotonicClock>()};
        },
        .createTaskSystem = Tina::Task::createDisabledTaskSystem,
        .platformRender =
            Tina::IndependentPlatformRenderFactories{
                .createPlatformBackend = Tina::Platform::createHeadlessPlatformBackend,
                .createRenderDevice = Tina::Render::createNullRenderDevice,
            },
    };
}

} // namespace

int main()
{
    auto host = Tina::EngineHost::Create(Tina::EngineConfig::Defaults(), createFactories());
    if (!host)
    {
        return 1;
    }

    ConsumerApplication application;
    auto exitReason = (*host)->run(application);
    if (!exitReason || *exitReason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame)
    {
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"consumer\":\"installed-tina-sdk\"}\n";
    return 0;
}
