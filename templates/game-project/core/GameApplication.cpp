#include "GameApplication.hpp"

#include <tina/asset/AssetSystem.hpp>
#include <tina/core/io/ContentRoot.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/GameState.hpp>

#include <memory>
#include <memory_resource>
#include <new>
#include <utility>

namespace MyGame {
namespace {

// Runtime only consumes a cooked catalog. It does not need source art or write
// access to its install directory; the frontend builds and stages the catalog.
[[nodiscard]] Tina::Core::Result<Tina::Asset::AssetSystem> openContent(const Tina::Core::ContentRoot& root)
{
    auto catalogRoot = root.resolve("content");
    if (!catalogRoot)
    {
        return Tina::Core::failure(std::move(catalogRoot.error()));
    }

    auto system = Tina::Asset::AssetSystem::Create(Tina::Asset::AssetSystemConfig{
        .storeCapacity = 32,
        .memoryResource = std::pmr::get_default_resource(),
    });
    if (!system)
    {
        return Tina::Core::failure(std::move(system.error()));
    }
    if (auto bound = system->openAndBindCatalog(*catalogRoot); !bound)
    {
        return Tina::Core::failure(std::move(bound.error()));
    }
    return system;
}

// The smallest usable state: every IGameState hook is defaulted, so a game overrides only
// what it needs. Add fixedUpdate for simulation, updateUI for interface, and
// extractRenderScene to publish draw intent.
class MainState final : public Tina::IGameState {
  public:
    explicit MainState(Tina::Asset::AssetSystem content) noexcept : m_content(std::move(content)) {}

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext&) override
    {
        return Tina::Core::success();
    }

  private:
    Tina::Asset::AssetSystem m_content;
};

class Application final : public Tina::IGameApplication {
  public:
    [[nodiscard]] Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(
        Tina::GameStartupContext& context) override
    {
        // Assets are opened here rather than in the constructor for two reasons: this is the
        // first hook allowed to fail, and it is the first point where the engine hands back
        // the config holding the content root.
        auto content = openContent(context.engineConfig().contentRoot);
        if (!content)
        {
            return Tina::Core::failure(std::move(content.error()));
        }
        auto state = std::make_unique<MainState>(std::move(*content));
        return std::unique_ptr<Tina::IGameState>{std::move(state)};
    }
};

} // namespace

std::unique_ptr<Tina::IGameApplication> createApplication() noexcept
{
    // nothrow because a frontend has no useful reaction to an exception here: it reports a
    // null result as a startup failure and exits.
    return std::unique_ptr<Tina::IGameApplication>{new (std::nothrow) Application{}};
}

} // namespace MyGame
