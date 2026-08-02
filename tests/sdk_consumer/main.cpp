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

#include <array>
#include <iostream>
#include <memory>
#include <utility>

namespace {

class ConsumerState final : public Tina::IGameState {
  public:
    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        if (!context.hasPrimaryWindowUI())
        {
            return Tina::Core::success();
        }

        auto builder = context.primaryWindowUIRootBuilder();
        if (!builder)
        {
            return Tina::Core::failure(std::move(builder.error()));
        }
        auto styleClass = builder->registerStyleClass();
        if (!styleClass)
        {
            return Tina::Core::failure(std::move(styleClass.error()));
        }
        auto colorToken = builder->registerStyleColorToken(Tina::UI::rgb(0x2463A5));
        if (!colorToken)
        {
            return Tina::Core::failure(std::move(colorToken.error()));
        }
        const std::array rules{
            Tina::UI::UIStyleBoxFillRule{
                .role = Tina::UI::UIStyleRoleId::PanelSurface,
                .styleClass = *styleClass,
                .requiredStates = Tina::UI::UIStyleState::Disabled,
                .colorToken = *colorToken,
            },
        };
        if (Tina::Core::Status status = builder->installStyleSheet(rules); !status)
        {
            return status;
        }
        auto root = builder->createRoot();
        if (!root)
        {
            return Tina::Core::failure(std::move(root.error()));
        }
        auto tree = builder->treeUpdater(*root);
        if (!tree)
        {
            return Tina::Core::failure(std::move(tree.error()));
        }
        const std::array classes{*styleClass};
        Tina::UI::UIElementDescriptor panel = Tina::UI::makePanelElement();
        panel.visual.styleRole = Tina::UI::UIStyleRoleId::PanelSurface;
        panel.visual.styleClasses = classes;
        if (auto created = tree->createElement(root->rootNodeId(), panel); !created)
        {
            return Tina::Core::failure(std::move(created.error()));
        }
        root_ = std::move(*root);
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        root_.reset();
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

  private:
    Tina::UI::UIRootOwner root_{};
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
