#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/core/text/ParseInteger.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/asset/AssetGpuShader.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/Mesh3DBindingRegistry.hpp>
#include <tina/asset/Mesh3DShaderOverride.hpp>
#include <tina/asset/ShaderBindingRegistry.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/gameplay3d/Scene3DRuntime.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/scene/PrefabInstantiate.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory_resource>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace Tina;
constexpr InputActionId Forward{1}, Backward{2}, Left{3}, Right{4}, Jump{5}, Exit{6};
constexpr Core::usize AssetCapacity = 8192;
constexpr Core::usize MeshCapacity = 1024;

void writeError(const Core::Error& error)
{
    Core::JsonWriter writer(std::cerr);
    writer.beginObject();
    writer.member("status", "error");
    writer.member("sample", "tina_sample_3d_authored_level");
    writer.member("message", error.message);
    writer.endObject();
    std::cerr << '\n';
}

struct Options final {
    std::string catalog;
    Core::AssetId prefab;
    Core::u64 frames = 0;
    Core::u32 delayMilliseconds = 0;
};

class LevelState final : public IGameState {
  public:
    explicit LevelState(Options options) : options_(std::move(options)) {}
    ~LevelState() override { closeOrTerminate(); }

    Core::Status onEnter(GameStateEnterContext& context) override
    {
        device_ = &context.renderDevice();
        auto rollback = Core::makeScopeExit([&]() noexcept { closeOrTerminate(); });
        auto assets = Asset::AssetSystem::Create({.storeCapacity = AssetCapacity, .memoryResource = &memory_});
        if (!assets) return Core::failure(std::move(assets.error()));
        assets_.emplace(std::move(*assets));
        if (auto status = assets_->openAndBindCatalog(context.engineConfig().contentRoot.baseDirectory()); !status) return status;
        auto root = assets_->loadOne(options_.prefab);
        if (!root) return Core::failure(std::move(root.error()));
        auto prefab = Asset::parsePrefabFromCooked(*assets_->tryGet(*root));
        if (!prefab) return Core::failure(std::move(prefab.error()));
        if (std::count_if(prefab->nodes.begin(), prefab->nodes.end(), [](const auto& node) {
                return node.camera && node.camera->active;
            }) != 1)
            return Core::failure(Scene::SceneErrorCode::InvalidComponent,
                                 "Authored level requires exactly one active Camera3D");
        auto registry = Asset::Mesh3DBindingRegistry::Create(*assets_, *device_,
            {.meshCapacity = MeshCapacity, .materialCapacity = MeshCapacity,
             .textureCapacity = MeshCapacity * AssetFormat::MaterialWire::TextureRoleCount, .memoryResource = &memory_});
        if (!registry) return Core::failure(std::move(registry.error()));
        bindings_.emplace(std::move(*registry));
        auto shaders = Asset::ShaderBindingRegistry::Create(*assets_, *device_, {.memoryResource = &memory_});
        if (!shaders) return Core::failure(std::move(shaders.error()));
        shaders_.emplace(std::move(*shaders));
        std::vector<Asset::AssetHandle> closure{*root};
        for (Core::usize index = 0; index < closure.size(); ++index) {
            const auto* file = assets_->tryGet(closure[index]);
            if (file == nullptr) return Core::failure(Asset::AssetErrorCode::AssetNotReady, "Level dependency is not resident");
            for (Core::u32 slot = 0; slot < file->header().dependencyCount; ++slot) {
                const auto dependency = file->dependency(slot);
                if (!dependency) return Core::failure(Asset::AssetErrorCode::CatalogEntryMismatch, "Invalid level dependency");
                const auto handle = assets_->find(dependency->assetId);
                if (!handle) return Core::failure(Asset::AssetErrorCode::AssetNotReady, "Missing level dependency");
                if (std::find(closure.begin(), closure.end(), *handle) == closure.end()) closure.push_back(*handle);
            }
        }
        for (const auto handle : closure) {
            const auto kind = assets_->store().assetKind(handle);
            const auto& file = *assets_->tryGet(handle);
            if (kind == AssetFormat::AssetKind::Texture2D) {
                auto texture = Asset::uploadTexture2DFromCooked(*device_, file);
                if (!texture) return Core::failure(std::move(texture.error()));
                auto release = Core::makeScopeExit([&]() noexcept {
                    if (*texture) require(device_->destroyTexture2D(*texture));
                });
                if (auto status = bindings_->registerMaterialTexture(handle, *texture); !status) return status;
                release.release();
            } else if (kind == AssetFormat::AssetKind::Shader) {
                auto shader = Asset::uploadShaderFromCooked(*device_, file);
                if (!shader) return Core::failure(std::move(shader.error()));
                auto release = Core::makeScopeExit([&]() noexcept {
                    if (*shader) require(device_->destroyShader(*shader));
                });
                auto bound = shaders_->registerShaderBinding(handle, *shader);
                if (!bound) return Core::failure(std::move(bound.error()));
                release.release();
            } else if (kind == AssetFormat::AssetKind::StaticMesh || kind == AssetFormat::AssetKind::SkinnedMesh) {
                const bool skinned = kind == AssetFormat::AssetKind::SkinnedMesh;
                auto gpu = skinned ? Asset::uploadSkinnedMeshFromCooked(*device_, file)
                                   : Asset::uploadStaticMeshFromCooked(*device_, file);
                if (!gpu) return Core::failure(std::move(gpu.error()));
                auto release = Core::makeScopeExit([&]() noexcept {
                    if (*gpu) require(device_->destroyGpuMesh(*gpu));
                });
                auto bound = skinned ? bindings_->registerSkinnedMeshBinding(handle, *gpu)
                                     : bindings_->registerMeshBinding(handle, *gpu);
                if (!bound) return Core::failure(std::move(bound.error()));
                release.release();
                MeshMetadata metadata{.asset = handle};
                if (skinned) {
                    auto mesh = Asset::parseSkinnedMeshFromCooked(file);
                    if (!mesh) return Core::failure(std::move(mesh.error()));
                    metadata.bounds = {.centerX = mesh->boundsCenterX, .centerY = mesh->boundsCenterY,
                                       .centerZ = mesh->boundsCenterZ, .radius = mesh->boundsRadius};
                } else {
                    auto mesh = Asset::parseStaticMeshFromCooked(file);
                    if (!mesh) return Core::failure(std::move(mesh.error()));
                    metadata.bounds = {.centerX = mesh->boundsCenterX, .centerY = mesh->boundsCenterY,
                                       .centerZ = mesh->boundsCenterZ, .radius = mesh->boundsRadius};
                }
                auto overrideId = Asset::readMesh3DShaderOverride(file);
                if (!overrideId) return Core::failure(std::move(overrideId.error()));
                if (*overrideId) metadata.shader = assets_->find(**overrideId).value_or(Asset::AssetHandle{});
                meshes_.push_back(metadata);
            }
        }
        for (const auto handle : closure) {
            if (assets_->store().assetKind(handle) != AssetFormat::AssetKind::Material) continue;
            auto material = Asset::parseMaterialFromCooked(*assets_->tryGet(handle));
            if (!material) return Core::failure(std::move(material.error()));
            auto bound = bindings_->registerMaterialBinding(handle);
            if (!bound) return Core::failure(std::move(bound.error()));
            materials_.push_back({handle, *material});
        }
        for (const auto& node : prefab->nodes) {
            if (node.hasMaterial && materialFor(node.materialId) == nullptr)
                return Core::failure(Asset::AssetErrorCode::CatalogEntryMismatch,
                                     "Level Prefab references a Material missing from its dependency closure");
        }
        auto world = Scene::World::Create({.entityCapacity = AssetFormat::PrefabWire::MaxNodes});
        if (!world) return Core::failure(std::move(world.error()));
        world_.emplace(std::move(*world));
        Scene::PrefabMeshBinding meshBindings;
        meshBindings.resolveMesh = meshBindings.resolveMaterial = [this](Core::AssetId id) {
            return assets_->find(id).value_or(Asset::AssetHandle{});
        };
        meshBindings.resolveLocalBounds = [this](Core::AssetId id) {
            const auto handle = assets_->find(id).value_or(Asset::AssetHandle{});
            for (const auto& mesh : meshes_) if (mesh.asset == handle) return mesh.bounds;
            return Render::RenderBoundingSphereInput{};
        };
        meshBindings.resolveBaseColor = [this](Core::AssetId id) {
            const auto* material = materialFor(id);
            return material == nullptr
                ? Render::RenderLinearColor{}
                : Render::RenderLinearColor{material->baseColorR, material->baseColorG,
                                            material->baseColorB, material->baseColorA};
        };
        meshBindings.resolveAlphaMode = [this](Core::AssetId id) {
            const auto* material = materialFor(id);
            if (material == nullptr) return Render::Mesh3DAlphaMode::Opaque;
            switch (material->alphaMode) {
            case AssetFormat::MaterialAlphaMode::Mask: return Render::Mesh3DAlphaMode::Mask;
            case AssetFormat::MaterialAlphaMode::Blend: return Render::Mesh3DAlphaMode::Blend;
            default: return Render::Mesh3DAlphaMode::Opaque;
            }
        };
        meshBindings.resolveDoubleSided = [this](Core::AssetId id) {
            const auto* material = materialFor(id);
            return material != nullptr && material->doubleSided;
        };
        auto entities = Scene::instantiatePrefab(*world_, prefab->view, std::move(meshBindings));
        if (!entities) return Core::failure(std::move(entities.error()));
        const float delta = static_cast<float>(context.engineConfig().fixedSimulation.fixedDelta.count());
#if defined(TINA_HAS_PHYSICS3D)
        auto physics = Physics3D::PhysicsWorld3D::Create({.fixedDeltaSeconds = delta});
        if (!physics) return Core::failure(std::move(physics.error()));
        physics_.emplace(std::move(*physics));
#endif
        if (auto status = runtime_.build(*world_, prefab->view, *entities, *assets_,
                {.animatorCapacity = MeshCapacity, .fixedDeltaSeconds = delta, .memoryResource = &memory_}
#if defined(TINA_HAS_PHYSICS3D)
                , &*physics_
#endif
                ); !status) return status;
        const auto extent = context.engineConfig().primaryWindow.initialLogicalExtent;
        surface_ = {.pixelWidth = extent.width, .pixelHeight = extent.height};
        auto subscription = context.platformEventSubscriptions().subscribe([this](const PlatformEventNotification& event) {
            if (const auto* metrics = event.primaryWindowMetrics())
                surface_ = {.pixelWidth = metrics->framebufferExtent.width, .pixelHeight = metrics->framebufferExtent.height};
        });
        if (!subscription) return Core::failure(std::move(subscription.error()));
        subscription_.emplace(std::move(*subscription));
        rollback.release();
        return Core::success();
    }

    void onExit(GameStateExitContext&) noexcept override { closeOrTerminate(); }

    Core::Status fixedUpdate(FixedUpdateContext& context) override
    {
        const auto& actions = context.simulationActions();
        bool jump = false;
        for (const auto& transition : actions.transitions)
            if (const auto* input = std::get_if<InputActionTransition>(&transition))
                jump = jump || (input->action == Jump && input->kind == InputActionTransitionKind::Started);
        if (auto status = runtime_.setPlayerInput({.moveX = actions.value(Right) - actions.value(Left),
                .moveZ = actions.value(Backward) - actions.value(Forward), .jumpPressed = jump}); !status) return status;
        auto step = runtime_.fixedUpdate(*world_);
        if (!step) return Core::failure(std::move(step.error()));
        if (step->droppedAnimationEvents != 0)
            return Core::failure(Core::CoreErrorCode::CapacityExceeded, "Level animation event budget exceeded");
        for (const auto& event : step->animationEvents) {
            Core::JsonWriter writer(std::cout);
            writer.beginObject();
            writer.member("event", "animation");
            writer.member("node", event.stableNodeId);
            writer.member("tag", event.crossing.eventTag);
            writer.endObject();
            std::cout << '\n';
        }
#if defined(TINA_HAS_PHYSICS3D)
        if (step->droppedContacts != 0)
            return Core::failure(Core::CoreErrorCode::CapacityExceeded, "Level contact event budget exceeded");
        for (const auto& event : step->contacts) {
            if (!event.sensor || event.phase == Physics3D::PhysicsContactPhase3D::Stay) continue;
            Core::JsonWriter writer(std::cout);
            writer.beginObject();
            writer.member("event", event.phase == Physics3D::PhysicsContactPhase3D::Enter ? "trigger-enter" : "trigger-exit");
            writer.member("first", runtime_.index()->stableId(runtime_.physicsBridge()->entity(event.first)));
            writer.member("second", runtime_.index()->stableId(runtime_.physicsBridge()->entity(event.second)));
            writer.endObject();
            std::cout << '\n';
        }
#endif
        return Core::success();
    }

    Core::Status updateFrame(FrameUpdateContext& context) override
    {
        ++frameCount_;
        if (context.frameActions().isActive(Exit) || (options_.frames != 0 && frameCount_ >= options_.frames))
            context.requestExitAfterFrame();
        if (options_.delayMilliseconds != 0) std::this_thread::sleep_for(std::chrono::milliseconds(options_.delayMilliseconds));
        return Core::success();
    }

    Core::Status extractRenderScene(RenderSceneExtractionContext& context) const override
    {
        if (auto status = context.setPrimaryPostProcess({.enabled = true, .bloom = {.enabled = true}}); !status) return status;
        auto* self = const_cast<LevelState*>(this);
        return Scene::extractRenderSceneFromWorld(*world_, context.renderSceneWriter(), context.frameResourceSink(),
            {.surfaceViewport = surface_,
             .mesh3DDefaultShaderBindingResolver = {.userData = self, .resolve = &resolveDefaultShader<false>},
             .mesh3DDefaultShaderUniformBindingResolver = {.userData = self, .resolve = &resolveDefaultShader<true>},
             .mesh3DBindingResolver = {.userData = self, .resolve = &resolveMesh<false>},
             .material3DBindingResolver = {.userData = self, .resolve = [](void* user, Asset::AssetHandle handle,
                     Render::FrameResourceSink& sink) noexcept { return static_cast<LevelState*>(user)->bindings_->internMaterialFrameResource(handle, sink); }},
             .skinnedMesh3DBindingResolver = {.userData = self, .resolve = &resolveMesh<true>},
             .skinnedPose3DProvider = self->runtime_.poseProvider()});
    }

  private:
    struct MeshMetadata final { Asset::AssetHandle asset; Render::RenderBoundingSphereInput bounds; Asset::AssetHandle shader; };
    struct MaterialMetadata final { Asset::AssetHandle asset; AssetFormat::MaterialPayloadView material; };
    static void require(Core::Status status) noexcept
    {
        if (!status) { writeError(status.error()); std::terminate(); }
    }
    void closeOrTerminate() noexcept
    {
        require(runtime_.shutdown());
        physicsReset();
        subscription_.reset();
        world_.reset();
        if (bindings_) require(bindings_->retireAllBindings());
        if (shaders_) require(shaders_->retireAllShaderBindings());
        if (assets_) require(assets_->drainGpuRetirements());
        shaders_.reset();
        bindings_.reset();
        materials_.clear();
        meshes_.clear();
        assets_.reset();
        device_ = nullptr;
    }
    void physicsReset() noexcept
    {
#if defined(TINA_HAS_PHYSICS3D)
        if (physics_) require(physics_->shutdown());
        physics_.reset();
#endif
    }
    const AssetFormat::MaterialPayloadView* materialFor(Core::AssetId id) const noexcept
    {
        const auto handle = assets_->find(id).value_or(Asset::AssetHandle{});
        for (const auto& entry : materials_) if (entry.asset == handle) return &entry.material;
        return nullptr;
    }
    template<bool Skinned> static Core::Result<Render::FrameResourceRef> resolveMesh(
        void* user, Asset::AssetHandle handle, Render::FrameResourceSink& sink) noexcept
    {
        auto& registry = *static_cast<LevelState*>(user)->bindings_;
        if constexpr (Skinned) return registry.internSkinnedMeshFrameResource(handle, sink);
        else return registry.internMeshFrameResource(handle, sink);
    }
    template<bool Uniform> static Core::Result<Render::FrameResourceRef> resolveDefaultShader(
        void* user, Asset::AssetHandle handle, Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<LevelState*>(user);
        for (const auto& mesh : self.meshes_) if (mesh.asset == handle && mesh.shader) {
            if constexpr (Uniform) return self.shaders_->internShaderUniformFrameResource(mesh.shader, sink);
            else return self.shaders_->internShaderFrameResource(mesh.shader, sink);
        }
        return Render::FrameResourceRef{};
    }
    Options options_;
    std::pmr::unsynchronized_pool_resource memory_;
    Render::IRenderDevice* device_ = nullptr;
    std::optional<Asset::AssetSystem> assets_;
    std::optional<Asset::Mesh3DBindingRegistry> bindings_;
    std::optional<Asset::ShaderBindingRegistry> shaders_;
    mutable std::optional<Scene::World> world_;
#if defined(TINA_HAS_PHYSICS3D)
    std::optional<Physics3D::PhysicsWorld3D> physics_;
#endif
    Gameplay3D::Scene3DRuntime runtime_;
    std::vector<MeshMetadata> meshes_;
    std::vector<MaterialMetadata> materials_;
    std::optional<PlatformEventSubscription> subscription_;
    Render::Camera2DSurfaceViewport surface_;
    Core::u64 frameCount_ = 0;
};

class LevelApplication final : public IGameApplication {
  public:
    explicit LevelApplication(Options options) : options_(std::move(options)) {}
    Core::Result<std::unique_ptr<IGameState>> createInitialState(GameStartupContext&) override
    {
        std::unique_ptr<IGameState> state = std::make_unique<LevelState>(options_);
        return state;
    }
  private:
    Options options_;
};

Core::Status run(int argc, char** argv)
{
    Options options;
    Core::ArgScanner arguments(argc, argv);
    while (arguments.next()) {
        if (auto value = arguments.value("--catalog")) { options.catalog = *value; continue; }
        if (auto value = arguments.value("--prefab")) {
            auto id = Core::AssetId::parseCanonical(*value);
            if (!id) return Core::failure(Core::CoreErrorCode::InvalidArgument, "Invalid --prefab AssetId");
            options.prefab = *id;
            continue;
        }
        if (auto value = arguments.value("--frames")) {
            if (!Core::parseUnsigned(*value, options.frames) || options.frames == 0)
                return Core::failure(Core::CoreErrorCode::InvalidArgument, "Invalid --frames");
            continue;
        }
        if (auto value = arguments.value("--frame-delay-ms")) {
            if (!Core::parseUnsigned(*value, options.delayMilliseconds) || options.delayMilliseconds > 1000)
                return Core::failure(Core::CoreErrorCode::InvalidArgument, "Invalid --frame-delay-ms");
            continue;
        }
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "Unknown or missing authored-level option");
    }
    if (options.catalog.empty() || !options.prefab)
        return Core::failure(Core::CoreErrorCode::InvalidArgument, "Required: --catalog <directory> --prefab <AssetId>");
    auto content = Core::ContentRoot::Create(options.catalog);
    if (!content) return Core::failure(std::move(content.error()));
    auto config = EngineConfig::Defaults();
    config.applicationName = "Tina Authored Level";
    config.contentRoot = std::move(*content);
    config.primaryWindow.title = "Tina - Authored 3D Level";
    config.primaryWindow.initialLogicalExtent = {1280, 720};
    config.renderSceneCapacities.mesh3DItemCapacity = MeshCapacity;
    config.renderSceneCapacities.mesh3DBatchCapacity = MeshCapacity;
    config.renderSceneCapacities.skinnedMesh3DItemCapacity = 256;
    config.renderSceneCapacities.skinnedMesh3DPaletteJointCapacity = 256 * 128;
    config.renderFrameResourceCapacity = MeshCapacity * 4;
    const std::array bindings{std::pair{Platform::Key::W, Forward}, std::pair{Platform::Key::S, Backward},
        std::pair{Platform::Key::A, Left}, std::pair{Platform::Key::D, Right}, std::pair{Platform::Key::Space, Jump}};
    for (const auto& [key, action] : bindings)
        config.inputActions.bindings.push_back({.input = PrimaryWindowKeyBinding{.key = key},
            .action = action, .domain = InputActionDomain::Simulation});
    config.inputActions.bindings.push_back({.input = PrimaryWindowKeyBinding{.key = Platform::Key::Escape},
        .action = Exit, .domain = InputActionDomain::Frame});
    auto host = Desktop::CreateEngine(config);
    if (!host) return Core::failure(std::move(host.error()));
    LevelApplication application(options);
    auto result = (*host)->run(application);
    if (!result) return Core::failure(std::move(result.error()));
    return Core::success();
}
} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** wideArguments)
#else
int main(int argc, char** argv)
#endif
try {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> storage;
    std::vector<char*> arguments;
    storage.reserve(argc);
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index) {
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wideArguments[index], -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0) {
            const Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument, "Invalid Unicode argument"};
            writeError(error);
            return 1;
        }
        std::string text(static_cast<Tina::Core::usize>(length), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wideArguments[index], -1,
                text.data(), length, nullptr, nullptr) != length) {
            const Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                          "UTF-8 argument conversion failed"};
            writeError(error);
            return 1;
        }
        text.pop_back();
        storage.push_back(std::move(text));
    }
    for (auto& text : storage) arguments.push_back(text.data());
    char** argv = arguments.data();
#endif
    if (auto status = run(argc, argv); !status) { writeError(status.error()); return 1; }
    return 0;
} catch (const std::exception& error) {
    const Tina::Core::Error failure{Tina::Core::CoreErrorCode::Internal, error.what()};
    writeError(failure);
    return 1;
} catch (...) {
    const Tina::Core::Error failure{Tina::Core::CoreErrorCode::Internal, "Unknown authored-level failure"};
    writeError(failure);
    return 1;
}
