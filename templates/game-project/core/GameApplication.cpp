#include "GameApplication.hpp"

#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/core/io/ContentRoot.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/GameState.hpp>

#include <filesystem>
#include <memory>
#include <memory_resource>
#include <new>
#include <utility>

namespace MyGame {
namespace {

// Cooks the recipe into a catalog and binds it. This is the whole asset path, and it is
// worth reading once because the shape is not obvious:
//
//   recipe text  ->  cook  ->  catalog (manifest + objects/)  ->  AssetSystem  ->  handle
//
// Content loads from the *catalog*, never from the recipe: the recipe is authoring input
// that a release does not have to carry. Cooking happens here at startup only because it is
// the option that always works -- it needs nothing but the recipe beside the executable.
// Moving it into the build is one tina_cook_catalog() call in the frontend's CMakeLists,
// after which everything down to the AssetSystem::Create below can go and this function is
// just the bind. See README.md.
//
// Both paths come from the root rather than from anything this file knows, which is what
// makes the same code work from a desktop install directory, a preloaded browser filesystem
// and an extracted APK.
[[nodiscard]] Tina::Core::Result<Tina::Asset::AssetSystem> openContent(const Tina::Core::ContentRoot& root)
{
    // Staged beside the executable by this project's CMakeLists via tina_product_data_file.
    auto recipeFile = root.resolve("assets/game.recipe");
    if (!recipeFile)
    {
        return Tina::Core::failure(std::move(recipeFile.error()));
    }
    // The cooked catalog is a build output, not authored content, so it gets its own
    // directory rather than going into assets/.
    auto catalogRoot = root.resolve("content");
    if (!catalogRoot)
    {
        return Tina::Core::failure(std::move(catalogRoot.error()));
    }

    auto request = Tina::Asset::loadCatalogCookRecipeFile(*recipeFile);
    if (!request)
    {
        return Tina::Core::failure(std::move(request.error()));
    }

    std::error_code directoryError;
    std::filesystem::create_directories(std::filesystem::path(*catalogRoot), directoryError);
    if (directoryError)
    {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                   "the catalog directory could not be created");
    }
    if (auto cooked = Tina::Asset::cookAndPublishCatalogPackage(*catalogRoot, *request); !cooked)
    {
        return Tina::Core::failure(std::move(cooked.error()));
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
