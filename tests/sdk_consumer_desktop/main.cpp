#include <tina/desktop/DesktopEngine.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <iostream>
#include <memory>

namespace {

// Overriding nothing but the two hooks that carry this consumer's intent also asserts
// that the installed headers default the rest: were any of them still pure, this would
// not compile.
class ConsumerState final : public Tina::IGameState {
  public:
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
};

} // namespace

int main()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina installed DesktopBootstrap consumer";
    config.primaryWindow.title = "Tina installed DesktopBootstrap consumer";
    config.primaryWindow.initialLogicalExtent = {320, 180};
    config.primaryWindow.initiallyVisible = false;

    auto host = Tina::Desktop::CreateEngine(config);
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

    std::cout << "{\"status\":\"ok\",\"consumer\":\"installed-tina-desktop-bootstrap\"}\n";
    return 0;
}
