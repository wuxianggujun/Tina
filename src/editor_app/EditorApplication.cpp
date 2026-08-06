// Tina Editor desktop composition: shared retained tool chrome backed by
// validated World2D and World3D authoring documents and Scene GPU previews.

#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/Mesh3DBindingRegistry.hpp>
#include <tina/asset/Sprite2DBindingRegistry.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/SpriteAnimationAuthoringDocument.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>
#include <tina/editor/World2DAuthoringFile.hpp>
#include <tina/editor/World3DAuthoringDocument.hpp>
#include <tina/editor/World3DAuthoringFile.hpp>
#include <tina/editor/TileMapAuthoringDocument.hpp>
#include <tina/editor_app/EditorApplication.hpp>
#include <tina/render/FramePin.hpp>
#include <tina/render/RenderScene.hpp>
#include <tina/runtime/GameApplication.hpp>
#include <tina/runtime/GameState.hpp>
#include <tina/runtime/PrimaryWindowUI.hpp>
#include <tina/runtime/RunExitReason.hpp>
#include <tina/scene/Camera2D.hpp>
#include <tina/scene/ExtractRenderScene.hpp>
#include <tina/scene/MeshRenderer3D.hpp>
#include <tina/scene/PerspectiveCamera3D.hpp>
#include <tina/scene/PrefabInstantiate.hpp>
#include <tina/scene/SpriteAnimator2D.hpp>
#include <tina/scene/World.hpp>
#include <tina/scene/World2DSnapshot.hpp>
#include <tina/ui/UIElement.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UITreeView.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace UI = Tina::UI;

using Tina::Core::u8;
using Tina::Core::u32;
using Tina::Core::u64;

inline constexpr u64 DefaultFrameCount = 60;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr u32 WindowLogicalWidth = 1280;
inline constexpr u32 WindowLogicalHeight = 800;
inline constexpr u32 HierarchyMaterializedCapacity = 16;
inline constexpr u32 AuthoringEntityCapacity = 16;
inline constexpr u32 InitialAuthoringEntityCount = 5;
inline constexpr u32 AnimationVisibleFrameSlots = 6;
inline constexpr u32 EditorActionCount = 40;
inline constexpr u32 EditorLayoutRegionCount = 7;
inline constexpr u32 GpuViewportSpriteCount = 1;
inline constexpr u32 GpuViewportMeshCount = 3;
inline constexpr u32 InitialTileMapWidthCells = 8;
inline constexpr u32 InitialTileMapHeightCells = 4;
inline constexpr u32 TileMapAuthoringLayerCapacity = 16;
inline constexpr u32 InitialTileMapCellCount = 12;
inline constexpr u32 InitialTileMapChunkCount = 2;
inline constexpr u32 EditorViewportSpriteCapacity =
    GpuViewportSpriteCount + InitialTileMapWidthCells * InitialTileMapHeightCells *
                                 TileMapAuthoringLayerCapacity;
inline constexpr Tina::AssetFormat::TileMapLayerId InitialTileMapLayerId = 1;
inline constexpr float PreviewWorldWidth = 16.0F;
inline constexpr float PreviewWorldHeight = 9.0F;
inline constexpr float DegreesToRadians = 0.01745329251994329577F;
inline constexpr float RadiansToDegrees = 57.295779513082320876F;

[[nodiscard]] Tina::Core::AssetId editorAssetId(u8 marker)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(marker);
    return *Tina::Core::AssetId::fromBytes(bytes);
}

enum class WorkspaceMode : u8 {
    World2D,
    World3D,
};

enum class ViewportToolMode : u8 {
    Select,
    Move,
    TilePaint,
    TileErase,
};

struct ViewportGizmoTransaction final {
    WorkspaceMode workspace = WorkspaceMode::World2D;
    Tina::Platform::PointerId pointer = Tina::Platform::PrimaryPointerId;
    u32 stableEntityId = 0;
    u64 baselineRevision = 0;
    UI::UILogicalPoint start{};
    UI::UILogicalPoint current{};
    UI::UILogicalPoint lastPublishedPoint{};
    float viewportWidth = 0.0F;
    float viewportHeight = 0.0F;
    Tina::Scene::LocalTransform baselineTransform{};
    float worldDeltaX = 0.0F;
    float worldDeltaY = 0.0F;
    float worldDeltaZ = 0.0F;
    bool captured = false;
    bool baselineReady = false;
    bool previewDirty = false;
    bool previewPublished = false;
    bool hasPublishedPoint = false;
    bool commitRequested = false;
    bool cancelRequested = false;
};

inline constexpr UI::UITreeViewItemKey SceneRootKey = 1;
inline constexpr UI::UITreeViewItemKey CameraKey = 2;
inline constexpr UI::UITreeViewItemKey PlayerKey = 3;
inline constexpr UI::UITreeViewItemKey PlayerSpriteKey = 4;
inline constexpr UI::UITreeViewItemKey PlayerTransformKey = 5;
inline constexpr UI::UITreeViewItemKey LightsKey = 6;
inline constexpr UI::UITreeViewItemKey TileMapKey = 7;

struct EditorLaunchOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    std::string world2DDocumentPathUtf8{};
    std::string world3DDocumentPathUtf8{};
    std::string catalogRootUtf8{};
    WorkspaceMode initialWorkspace = WorkspaceMode::World2D;
    bool autoDemo = true;
};

class EditorRenderDeviceAccess final {
  public:
    void set(Tina::Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Tina::Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Tina::Render::IRenderDevice* device_ = nullptr;
};

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

[[nodiscard]] Tina::Core::Result<std::filesystem::path> createUniqueEditorTempDirectory()
{
    std::error_code tempError;
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path(tempError);
    if (tempError) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                "Tina Editor could not query the temporary directory"};
        error.setNativeCode(tempError.value());
        error.addContext("temporary_directory_path", tempError.message());
        return Tina::Core::failure(std::move(error));
    }

    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    constexpr u32 MaximumAttempts = 256;
    for (u32 attempt = 0; attempt < MaximumAttempts; ++attempt) {
        const std::filesystem::path candidate =
            tempRoot / ("tina_editor_catalog_" + std::to_string(seed) + "_" +
                        std::to_string(attempt));
        std::error_code createError;
        if (std::filesystem::create_directory(candidate, createError)) {
            return candidate;
        }
        if (!createError) {
            continue;
        }
        std::error_code existsError;
        const bool exists = std::filesystem::exists(candidate, existsError);
        if (existsError || !exists) {
            Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                    "Tina Editor could not create a temporary Catalog root"};
            error.setNativeCode(createError.value());
            error.addContext("candidate", pathToUtf8(candidate));
            return Tina::Core::failure(std::move(error));
        }
    }
    return Tina::Core::failure(Tina::Core::CoreErrorCode::AlreadyExists,
                               "Tina Editor exhausted temporary Catalog directory attempts");
}

[[nodiscard]] Tina::Core::Result<Tina::Asset::CatalogCookRequest>
createBuiltInEditorCatalogRequest()
{
    std::string recipe;
#if defined(_WIN32)
    recipe = "platform WindowsX64\n";
#else
    recipe = "platform LinuxX64\n";
#endif
    const auto appendId = [&recipe](Tina::Core::AssetId id) {
        const auto text = id.canonicalText();
        recipe.append(text.data(), text.size());
    };

    recipe += "texture2d ";
    appendId(editorAssetId(0x21U));
    recipe += " 2 2 3478CFFF 4CB5AEFF F2C14EFF E05D5DFF\n";
    recipe += "sprite ";
    appendId(editorAssetId(0x22U));
    recipe += " ";
    appendId(editorAssetId(0x21U));
    recipe += " 0 0 0.5 0.5 0.5 0.5 16\n";
    recipe += "sprite ";
    appendId(editorAssetId(0x23U));
    recipe += " ";
    appendId(editorAssetId(0x21U));
    recipe += " 0.5 0 1 0.5 0.5 0.5 16\n";
    recipe += "sprite ";
    appendId(editorAssetId(0x24U));
    recipe += " ";
    appendId(editorAssetId(0x21U));
    recipe += " 0 0.5 0.5 1 0.5 0.5 16\n";
    recipe += "sprite ";
    appendId(editorAssetId(0x25U));
    recipe += " ";
    appendId(editorAssetId(0x21U));
    recipe += " 0.5 0.5 1 1 0.5 0.5 16\n";
    recipe += "tileset ";
    appendId(editorAssetId(0x41U));
    recipe += " ";
    appendId(editorAssetId(0x21U));
    recipe += " 16 16\n";
    recipe += "tile 1 0 0 0 0.5 0.5\n";
    recipe += "tile 2 1 0.5 0 1 0.5\n";
    recipe += "tile 3 0 0 0.5 0.5 1\n";
    recipe += "tile 4 0 0.5 0.5 1 1\n";
    recipe += "staticmesh ";
    appendId(editorAssetId(0x31U));
    recipe += " cube\n";
    recipe += "material ";
    appendId(editorAssetId(0x32U));
    recipe += " unlit 0.26 0.68 0.92 1.0\n";
    return Tina::Asset::parseCatalogCookRecipe(recipe, ".");
}

struct EditorAssetResources final {
    std::pmr::unsynchronized_pool_resource memory{};
    std::optional<Tina::Asset::AssetSystem> system{};
    std::filesystem::path ownedWorkRoot{};
    std::string catalogRootUtf8{};
    u32 catalogEntryCount = 0;
    bool projectCatalogConfigured = false;
    bool builtInPreviewCatalog = false;

    EditorAssetResources() = default;
    EditorAssetResources(const EditorAssetResources&) = delete;
    EditorAssetResources& operator=(const EditorAssetResources&) = delete;

    ~EditorAssetResources() noexcept
    {
        system.reset();
        if (!ownedWorkRoot.empty()) {
            std::error_code cleanupError;
            std::filesystem::remove_all(ownedWorkRoot, cleanupError);
        }
    }
};

[[nodiscard]] Tina::Core::Status prepareEditorCatalog(const EditorLaunchOptions& options,
                                                      EditorAssetResources& resources)
{
    resources.projectCatalogConfigured = !options.catalogRootUtf8.empty();
    resources.builtInPreviewCatalog = !resources.projectCatalogConfigured;
    if (resources.projectCatalogConfigured) {
        resources.catalogRootUtf8 = options.catalogRootUtf8;
    } else {
        auto workRoot = createUniqueEditorTempDirectory();
        if (!workRoot) {
            return Tina::Core::failure(std::move(workRoot.error()));
        }
        resources.ownedWorkRoot = std::move(*workRoot);
        resources.catalogRootUtf8 = pathToUtf8(resources.ownedWorkRoot / "catalog");
        auto request = createBuiltInEditorCatalogRequest();
        if (!request) {
            return Tina::Core::failure(std::move(request.error()));
        }
        if (auto status = Tina::Asset::cookAndPublishCatalogPackage(resources.catalogRootUtf8,
                                                                    *request);
            !status) {
            return status;
        }
    }

    auto system = Tina::Asset::AssetSystem::Create({
        .storeCapacity = 128,
        .memoryResource = &resources.memory,
        .requireTyped2dPayloads = true,
    });
    if (!system) {
        return Tina::Core::failure(std::move(system.error()));
    }
    Tina::Asset::CatalogPackageOpenConfig openConfig{};
    openConfig.validation.verifyTypedPayload = true;
    if (auto status = system->openAndBindCatalog(resources.catalogRootUtf8, openConfig); !status) {
        return status;
    }
    resources.catalogEntryCount = system->catalog() != nullptr
                                      ? system->catalog()->entryCount()
                                      : 0U;
    resources.system.emplace(std::move(*system));
    return Tina::Core::success();
}

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    u64 stateEnters = 0;
    u64 stateExits = 0;
    u64 applicationShutdowns = 0;
    u64 uiRootsCreated = 0;
    u64 uiRootsReleased = 0;
    u64 hierarchySelectionChanges = 0;
    UI::UITreeViewItemKey finalSelectionKey = UI::InvalidUITreeViewItemKey;
    u64 finalSelectionIndex = 0;
    u64 hierarchyLogicalItems = 0;
    u64 renderExtractions = 0;
    u64 gpuViewportSprites = 0;
    u64 gpuViewportMeshes = 0;
    u64 gpuViewportDocumentRevision = 0;
    u64 catalogAssetsLoaded = 0;
    u64 catalogGpuTextures = 0;
    u64 catalogGpuMeshes = 0;
    u64 catalogSpriteBindings = 0;
    u64 catalogMeshBindings = 0;
    u64 catalogMaterialBindings = 0;
    u64 catalogUnresolvedReferences = 0;
    u64 catalogResolved2DSprites = 0;
    u64 catalogResolved3DMeshes = 0;
    u64 tileMapDocumentRevision = 0;
    u64 tileMapLayerCount = 0;
    u64 tileMapChunkCount = 0;
    u64 tileMapAuthoredCells = 0;
    u64 tileMapCookArtifacts = 0;
    u64 tileMapCookPreviewBytes = 0;
    u64 tileMapEmittedSprites = 0;
    u64 tileMapEdits = 0;
    u64 tileMapUndos = 0;
    u64 tileMapRedos = 0;
    u64 animationDocumentRevision = 0;
    u64 animationFrameCount = 0;
    u64 animationCookPreviewBytes = 0;
    u64 animationPreviewFrameIndex = 0;
    u64 animationEdits = 0;
    u64 animationUndos = 0;
    u64 animationRedos = 0;
    u64 animationPlaybackTransitions = 0;
    u64 catalogEntryCount = 0;
    u64 workspaceSwitches = 0;
    u64 styleRegisteredClasses = 0;
    u64 styleRegisteredTokens = 0;
    u64 styleActiveRules = 0;
    u64 styleRevision = 0;
    u64 styleTokenUpdates = 0;
    u64 authoringActionsWired = 0;
    u64 authoringEdits = 0;
    u64 authoringUndos = 0;
    u64 authoringRedos = 0;
    u64 authoringSaves = 0;
    u64 inspectorTransactions = 0;
    u64 inspectorRejectedTransactions = 0;
    u64 viewportGizmoBegins = 0;
    u64 viewportGizmoPreviews = 0;
    u64 viewportGizmoCommits = 0;
    u64 viewportGizmoCancels = 0;
    u64 viewportGizmoRejects = 0;
    u64 savedSnapshotBytes = 0;
    u64 world2DSavedSnapshotBytes = 0;
    u64 world3DSavedSnapshotBytes = 0;
    u64 runtimePreviewInstantiations = 0;
    u64 documentRevision = 0;
    u64 documentEntityCount = 0;
    u64 documentUndoDepth = 0;
    u64 documentRedoDepth = 0;
    u64 cookPreviewBytes = 0;
    float finalPlayerPositionX = 0.0F;
    float finalPlayerPositionY = 0.0F;
    float finalPlayerPositionZ = 0.0F;
    float finalPlayerRotationXDegrees = 0.0F;
    float finalPlayerRotationYDegrees = 0.0F;
    float finalPlayerRotationDegrees = 0.0F;
    float finalPlayerScaleX = 1.0F;
    float finalPlayerScaleY = 1.0F;
    float finalPlayerScaleZ = 1.0F;
    float viewportLogicalX = 0.0F;
    float viewportLogicalY = 0.0F;
    float viewportLogicalWidth = 0.0F;
    float viewportLogicalHeight = 0.0F;
    float viewportNormalizedX = 0.0F;
    float viewportNormalizedY = 0.0F;
    float viewportNormalizedWidth = 0.0F;
    float viewportNormalizedHeight = 0.0F;
    float viewportGizmoWorldDeltaX = 0.0F;
    float viewportGizmoWorldDeltaY = 0.0F;
    float viewportGizmoWorldDeltaZ = 0.0F;
    u64 editorLayoutRegions = 0;
    bool selectionVerified = false;
    bool stylesheetInstalled = false;
    bool runtimePreviewValid = false;
    bool gpuViewportReady = false;
    bool viewportLayoutReady = false;
    bool inspectorScrollConfigured = false;
    bool documentPathConfigured = false;
    bool documentLoaded = false;
    bool documentSaved = false;
    bool documentDirty = true;
    bool finalWorkspaceWorld2D = true;
    bool world2DDocumentPathConfigured = false;
    bool world3DDocumentPathConfigured = false;
    bool world2DDocumentLoaded = false;
    bool world3DDocumentLoaded = false;
    bool world2DDocumentDirty = true;
    bool world3DDocumentDirty = true;
    bool world2DWorkspaceReady = false;
    bool world3DWorkspaceReady = false;
    bool catalogReady = false;
    bool projectCatalogConfigured = false;
    bool builtInPreviewCatalog = false;
};

enum class EditorCommand : u32 {
    SwitchToWorld2D,
    SwitchToWorld3D,
    MoveSelectedPositiveX,
    ApplyTransform,
    Undo,
    Redo,
    Save,
    PaintTile,
    EraseTile,
    ToggleTileLayer,
    AddTileLayer,
    AddObjectLayer,
    CookTileMapPreview,
    AnimationTogglePlayback,
    AnimationPreviousFrame,
    AnimationNextFrame,
    AnimationAddFrame,
    AnimationDuplicateFrame,
    AnimationDeleteFrame,
    AnimationMoveFrameLeft,
    AnimationMoveFrameRight,
    AnimationCycleSprite,
    AnimationDecreaseDuration,
    AnimationIncreaseDuration,
    AnimationCycleMode,
    AnimationCookPreview,
    AnimationUndo,
    AnimationRedo,
};

void writeJsonString(std::ostream& output, std::string_view value)
{
    constexpr char Hexadecimal[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                output << "\\u00" << Hexadecimal[byte >> 4U] << Hexadecimal[byte & 0x0FU];
            } else {
                output.put(static_cast<char>(byte));
            }
            break;
        }
    }
    output.put('"');
}

void writeError(const Tina::Core::Error& error)
{
    std::cerr << "{\"status\":\"error\",\"application\":\"TinaEditor\",\"domain\":"
              << static_cast<std::uint16_t>(error.code.domain) << ",\"code\":" << error.code.value
              << ",\"message\":";
    writeJsonString(std::cerr, error.message);
    std::cerr << "}\n";
}

template <typename Value>
[[nodiscard]] bool parseUnsigned(std::string_view text, Value& value) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] bool parseFiniteFloat(std::string_view text, float& value) noexcept
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && std::isfinite(value);
}

struct EulerDegrees final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] std::array<float, 4> quaternionFromEulerDegrees(EulerDegrees degrees) noexcept
{
    const float halfX = degrees.x * DegreesToRadians * 0.5F;
    const float halfY = degrees.y * DegreesToRadians * 0.5F;
    const float halfZ = degrees.z * DegreesToRadians * 0.5F;
    const float sinX = std::sin(halfX);
    const float cosX = std::cos(halfX);
    const float sinY = std::sin(halfY);
    const float cosY = std::cos(halfY);
    const float sinZ = std::sin(halfZ);
    const float cosZ = std::cos(halfZ);
    return {
        sinX * cosY * cosZ - cosX * sinY * sinZ,
        cosX * sinY * cosZ + sinX * cosY * sinZ,
        cosX * cosY * sinZ - sinX * sinY * cosZ,
        cosX * cosY * cosZ + sinX * sinY * sinZ,
    };
}

[[nodiscard]] EulerDegrees eulerDegreesFromQuaternion(float rotationX, float rotationY,
                                                       float rotationZ, float rotationW) noexcept
{
    const double lengthSquared = static_cast<double>(rotationX) * rotationX +
                                 static_cast<double>(rotationY) * rotationY +
                                 static_cast<double>(rotationZ) * rotationZ +
                                 static_cast<double>(rotationW) * rotationW;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12) {
        return {};
    }
    const float inverseLength = static_cast<float>(1.0 / std::sqrt(lengthSquared));
    rotationX *= inverseLength;
    rotationY *= inverseLength;
    rotationZ *= inverseLength;
    rotationW *= inverseLength;

    const float sinPitch = std::clamp(2.0F * (rotationW * rotationY - rotationZ * rotationX), -1.0F, 1.0F);
    const float roll = std::atan2(2.0F * (rotationW * rotationX + rotationY * rotationZ),
                                  1.0F - 2.0F * (rotationX * rotationX + rotationY * rotationY));
    const float pitch = std::asin(sinPitch);
    const float yaw = std::atan2(2.0F * (rotationW * rotationZ + rotationX * rotationY),
                                 1.0F - 2.0F * (rotationY * rotationY + rotationZ * rotationZ));
    return EulerDegrees{
        .x = roll * RadiansToDegrees,
        .y = pitch * RadiansToDegrees,
        .z = yaw * RadiansToDegrees,
    };
}

[[nodiscard]] Tina::Core::Result<EditorLaunchOptions> parseOptions(int argumentCount, char** arguments)
{
    constexpr std::string_view FramesPrefix = "--frames=";
    constexpr std::string_view DelayPrefix = "--frame-delay-ms=";
    constexpr std::string_view World2DPathPrefix = "--world2d-path=";
    constexpr std::string_view World3DPathPrefix = "--world3d-path=";
    constexpr std::string_view CatalogRootPrefix = "--catalog-root=";
    constexpr std::string_view WorkspacePrefix = "--workspace=";

    EditorLaunchOptions options{};
    bool hasFrames = false;
    bool hasDelay = false;
    bool hasWorld2DPath = false;
    bool hasWorld3DPath = false;
    bool hasCatalogRoot = false;
    bool hasWorkspace = false;
    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument.starts_with(FramesPrefix)) {
            if (hasFrames) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --frames argument");
            }
            const std::string_view value = argument.substr(FramesPrefix.size());
            if (!parseUnsigned(value, options.targetFrameCount) || options.targetFrameCount == 0) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must be an unsigned integer greater than zero");
            }
            hasFrames = true;
            continue;
        }
        if (argument.starts_with(DelayPrefix)) {
            if (hasDelay) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --frame-delay-ms argument");
            }
            const std::string_view value = argument.substr(DelayPrefix.size());
            if (!parseUnsigned(value, options.frameDelayMilliseconds)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frame-delay-ms must be an unsigned integer");
            }
            hasDelay = true;
            continue;
        }
        if (argument.starts_with(World2DPathPrefix)) {
            if (hasWorld2DPath) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --world2d-path argument");
            }
            const std::string_view value = argument.substr(World2DPathPrefix.size());
            if (value.empty()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--world2d-path must not be empty");
            }
            try {
                options.world2DDocumentPathUtf8.assign(value);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "Could not retain --world2d-path");
            }
            hasWorld2DPath = true;
            continue;
        }
        if (argument.starts_with(World3DPathPrefix)) {
            if (hasWorld3DPath) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --world3d-path argument");
            }
            const std::string_view value = argument.substr(World3DPathPrefix.size());
            if (value.empty()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--world3d-path must not be empty");
            }
            try {
                options.world3DDocumentPathUtf8.assign(value);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "Could not retain --world3d-path");
            }
            hasWorld3DPath = true;
            continue;
        }
        if (argument.starts_with(CatalogRootPrefix)) {
            if (hasCatalogRoot) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --catalog-root argument");
            }
            const std::string_view value = argument.substr(CatalogRootPrefix.size());
            if (value.empty()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--catalog-root must not be empty");
            }
            try {
                options.catalogRootUtf8.assign(value);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "Could not retain --catalog-root");
            }
            hasCatalogRoot = true;
            continue;
        }
        if (argument.starts_with(WorkspacePrefix)) {
            if (hasWorkspace) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --workspace argument");
            }
            const std::string_view value = argument.substr(WorkspacePrefix.size());
            if (value == "2d") {
                options.initialWorkspace = WorkspaceMode::World2D;
            } else if (value == "3d") {
                options.initialWorkspace = WorkspaceMode::World3D;
            } else {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--workspace must be 2d or 3d");
            }
            hasWorkspace = true;
            continue;
        }
        if (argument == "--no-auto-demo") {
            options.autoDemo = false;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Unsupported command-line argument");
    }
    return options;
}

[[nodiscard]] UI::UILayoutStyle percentSize(float widthPercent, float heightPercent) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(widthPercent);
    style.size.height = UI::UILayoutLength::Percent(heightPercent);
    return style;
}

[[nodiscard]] UI::UILayoutStyle flexChild(float grow, float basisPercent, float heightPercent) noexcept
{
    UI::UILayoutStyle style = percentSize(basisPercent, heightPercent);
    style.flexItem.grow = grow;
    style.flexItem.shrink = 1.0F;
    style.flexItem.basis = UI::UILayoutLength::Percent(basisPercent);
    return style;
}

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UILayoutStyle fillWidth(float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UILayoutStyle growingRegion() noexcept
{
    UI::UILayoutStyle style = percentSize(100.0F, 100.0F);
    style.flexItem.grow = 1.0F;
    style.flexItem.shrink = 1.0F;
    style.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    return style;
}

[[nodiscard]] UI::UILayoutStyle boundedDock(float basisPercent, float minimumWidth,
                                            float maximumWidth) noexcept
{
    UI::UILayoutStyle style = flexChild(0.0F, basisPercent, 100.0F);
    style.minMax.minWidth = UI::UILayoutLength::Px(minimumWidth);
    style.minMax.maxWidth = UI::UILayoutLength::Px(maximumWidth);
    return style;
}

[[nodiscard]] std::string_view hierarchyLabel(WorkspaceMode mode,
                                              UI::UITreeViewItemKey key) noexcept
{
    switch (key) {
    case SceneRootKey:
        return "Scene";
    case CameraKey:
        return mode == WorkspaceMode::World2D ? "Camera2D" : "Camera3D";
    case PlayerKey:
        return mode == WorkspaceMode::World2D ? "Player" : "HeroMesh";
    case PlayerSpriteKey:
        return mode == WorkspaceMode::World2D ? "SpriteRenderer2D" : "MeshRenderer3D";
    case PlayerTransformKey:
        return "Transform";
    case LightsKey:
        return mode == WorkspaceMode::World2D ? "PointLight2D" : "LeftMesh";
    case TileMapKey:
        return mode == WorkspaceMode::World2D ? "TileMap" : "RightMesh";
    default:
        return "Unknown";
    }
}

[[nodiscard]] std::string_view hierarchyKind(WorkspaceMode mode,
                                             UI::UITreeViewItemKey key) noexcept
{
    switch (key) {
    case SceneRootKey:
        return "Root";
    case CameraKey:
        return "Camera";
    case PlayerKey:
        return "Entity";
    case PlayerSpriteKey:
        return "Component";
    case PlayerTransformKey:
        return "Component";
    case LightsKey:
        return mode == WorkspaceMode::World2D ? "Component" : "Entity";
    case TileMapKey:
        return mode == WorkspaceMode::World2D ? "Asset" : "Entity";
    default:
        return "Unknown";
    }
}

[[nodiscard]] std::string_view hierarchyAuthoringNote(WorkspaceMode mode,
                                                      UI::UITreeViewItemKey key) noexcept
{
    if (mode == WorkspaceMode::World3D) {
        switch (key) {
        case SceneRootKey:
            return "Canonical Prefab v2 root.";
        case CameraKey:
            return "Perspective editor camera.";
        case PlayerKey:
            return "Stable 3D node identity.";
        case PlayerSpriteKey:
            return "Mesh and Material AssetId binding.";
        case PlayerTransformKey:
            return "Full TRS is revisioned.";
        case LightsKey:
        case TileMapKey:
            return "Prefab mesh authoring node.";
        default:
            return "Validated World3D authoring item.";
        }
    }
    switch (key) {
    case SceneRootKey:
        return "Canonical World2D root.";
    case CameraKey:
        return "Persisted Camera2D entity.";
    case PlayerKey:
        return "Stable entity identity.";
    case PlayerSpriteKey:
        return "Player sprite component.";
    case PlayerTransformKey:
        return "Move X is undoable.";
    case LightsKey:
        return "Runtime PointLight2D.";
    case TileMapKey:
        return "Root and streamed chunks share one revision.";
    default:
        return "Validated World2D authoring item.";
    }
}

[[nodiscard]] u32 stableEntityIdForHierarchyItem(UI::UITreeViewItemKey key) noexcept
{
    switch (key) {
    case SceneRootKey:
        return 1;
    case CameraKey:
        return 2;
    case PlayerKey:
    case PlayerSpriteKey:
    case PlayerTransformKey:
        return 3;
    case LightsKey:
        return 6;
    case TileMapKey:
        return 7;
    default:
        return 0;
    }
}

struct WorkspaceSessionState final {
    std::string documentPathUtf8{};
    std::vector<std::byte> savedBaselineBytes{};
    bool loadedFromPath = false;

    [[nodiscard]] bool hasDocumentPath() const noexcept
    {
        return !documentPathUtf8.empty();
    }
};

struct InitialAuthoringDocuments final {
    Tina::Editor::World2DAuthoringDocument world2D;
    Tina::Editor::World3DAuthoringDocument world3D;
    Tina::Editor::TileMapAuthoringDocument tileMap;
    Tina::Editor::SpriteAnimationAuthoringDocument spriteAnimation;
    WorkspaceSessionState world2DSession;
    WorkspaceSessionState world3DSession;
};

struct World3DPreviewBinding final {
    u32 stableNodeId = 0;
    Tina::Scene::EntityId entity{};
};

struct PreviewAssetReference final {
    Tina::Core::AssetId assetId{};
    Tina::AssetFormat::AssetKind kind = Tina::AssetFormat::AssetKind::Invalid;

    friend bool operator==(const PreviewAssetReference&, const PreviewAssetReference&) = default;
};

[[nodiscard]] Tina::Core::Result<InitialAuthoringDocuments>
createAuthoringDocuments(const EditorLaunchOptions& options)
{
    auto document = Tina::Editor::World2DAuthoringDocument::Create({
        .entityCapacity = AuthoringEntityCapacity,
        .gameplayByteCapacity = 1024,
        .historyEntryCapacity = 8,
        .historyByteCapacity = 64U * 1024U,
    });
    if (!document) {
        return Tina::Core::failure(std::move(document.error()));
    }

    WorkspaceSessionState world2DSession{};
    WorkspaceSessionState world3DSession{};
    try {
        world2DSession.documentPathUtf8 = options.world2DDocumentPathUtf8;
        world3DSession.documentPathUtf8 = options.world3DDocumentPathUtf8;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Tina Editor could not retain workspace document paths");
    }
    if (world2DSession.hasDocumentPath() && world3DSession.hasDocumentPath() &&
        world2DSession.documentPathUtf8 == world3DSession.documentPathUtf8) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "World2D and World3D workspace paths must reference different files");
    }

    if (world2DSession.hasDocumentPath()) {
        auto status = Tina::Editor::loadWorld2DAuthoringDocument(
            world2DSession.documentPathUtf8, *document);
        if (status) {
            world2DSession.loadedFromPath = true;
            try {
                const std::span<const std::byte> openedBytes = document->snapshotBytes();
                world2DSession.savedBaselineBytes.assign(openedBytes.begin(), openedBytes.end());
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Tina Editor could not retain the opened World2D baseline");
            }
        } else if (status.error().code != Tina::Core::CoreErrorCode::NotFound) {
            return Tina::Core::failure(std::move(status.error()));
        }
    }

    const std::array entities{
        Tina::AssetFormat::World2DEntityDesc{.stableEntityId = 1},
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 2,
            .parentStableEntityId = 1,
            .positionY = 4.0F,
            .camera = Tina::AssetFormat::World2DCameraDesc{
                .fixedWorldHeightMeters = PreviewWorldHeight,
            },
        },
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 3,
            .parentStableEntityId = 1,
            .sprite = Tina::AssetFormat::World2DSpriteDesc{
                .spriteId = editorAssetId(0x22U),
                .overrides = Tina::AssetFormat::World2DSpriteOverrideFlags::Size,
                .sizeX = 1.0F,
                .sizeY = 1.4F,
                .colorRed = 231,
                .colorGreen = 182,
                .colorBlue = 90,
            },
        },
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 6,
            .parentStableEntityId = 1,
            .positionX = 2.0F,
            .positionY = 2.0F,
            .pointLight = Tina::AssetFormat::World2DPointLightDesc{},
        },
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 7,
            .parentStableEntityId = 1,
        },
    };
    if (!world2DSession.loadedFromPath) {
        auto bytes = Tina::AssetFormat::writeWorld2DSnapshotBytes(
            Tina::AssetFormat::World2DSnapshotDesc{.entities = entities});
        if (!bytes) {
            return Tina::Core::failure(std::move(bytes.error()));
        }
        if (auto status = document->loadSnapshot(*bytes); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
    }

    auto world3D = Tina::Editor::World3DAuthoringDocument::Create({
        .nodeCapacity = AuthoringEntityCapacity,
        .historyEntryCapacity = 8,
        .historyByteCapacity = 64U * 1024U,
    });
    if (!world3D) {
        return Tina::Core::failure(std::move(world3D.error()));
    }
    if (world3DSession.hasDocumentPath()) {
        auto status = Tina::Editor::loadWorld3DAuthoringDocument(
            world3DSession.documentPathUtf8, *world3D);
        if (status) {
            world3DSession.loadedFromPath = true;
            try {
                const std::span<const std::byte> openedBytes = world3D->payloadBytes();
                world3DSession.savedBaselineBytes.assign(openedBytes.begin(), openedBytes.end());
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Tina Editor could not retain the opened World3D baseline");
            }
        } else if (status.error().code != Tina::Core::CoreErrorCode::NotFound) {
            return Tina::Core::failure(std::move(status.error()));
        }
    }
    const Tina::Core::AssetId meshId = editorAssetId(0x31U);
    const Tina::Core::AssetId materialId = editorAssetId(0x32U);
    const std::array nodes{
        Tina::AssetFormat::PrefabNodeDesc{.stableNodeId = 1},
        Tina::AssetFormat::PrefabNodeDesc{
            .stableNodeId = 2,
            .parentIndex = 0,
            .positionY = 2.25F,
            .positionZ = 8.0F,
        },
        Tina::AssetFormat::PrefabNodeDesc{
            .stableNodeId = 3,
            .parentIndex = 0,
            .positionZ = -1.0F,
            .scaleX = 1.15F,
            .scaleY = 1.15F,
            .scaleZ = 1.15F,
            .meshId = meshId,
            .materialId = materialId,
        },
        Tina::AssetFormat::PrefabNodeDesc{
            .stableNodeId = 6,
            .parentIndex = 0,
            .positionX = -2.3F,
            .positionZ = -0.4F,
            .scaleX = 0.9F,
            .scaleY = 0.9F,
            .scaleZ = 0.9F,
            .meshId = meshId,
            .materialId = materialId,
        },
        Tina::AssetFormat::PrefabNodeDesc{
            .stableNodeId = 7,
            .parentIndex = 0,
            .positionX = 2.3F,
            .positionZ = -1.6F,
            .scaleX = 0.8F,
            .scaleY = 0.8F,
            .scaleZ = 0.8F,
            .meshId = meshId,
            .materialId = materialId,
        },
    };
    if (!world3DSession.loadedFromPath) {
        auto prefabBytes = Tina::AssetFormat::writePrefabPayloadBytes({.nodes = nodes});
        if (!prefabBytes) {
            return Tina::Core::failure(std::move(prefabBytes.error()));
        }
        if (auto status = world3D->loadPayload(*prefabBytes); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
    }
    std::vector<Tina::Core::u16> leftChunkCells(16, 0U);
    leftChunkCells[0] = 1U;
    leftChunkCells[1] = 2U;
    leftChunkCells[2] = 1U;
    leftChunkCells[3] = 2U;
    leftChunkCells[4] = 3U;
    leftChunkCells[7] = 3U;
    std::vector<Tina::Core::u16> rightChunkCells(16, 0U);
    rightChunkCells[0] = 2U;
    rightChunkCells[1] = 1U;
    rightChunkCells[2] = 2U;
    rightChunkCells[3] = 1U;
    rightChunkCells[4] = 4U;
    rightChunkCells[7] = 4U;
    auto tileMap = Tina::Editor::TileMapAuthoringDocument::Create(
        Tina::Editor::TileMapAuthoringDesc{
            .tileMapId = editorAssetId(0x42U),
            .tilesetId = editorAssetId(0x41U),
            .widthCells = InitialTileMapWidthCells,
            .heightCells = InitialTileMapHeightCells,
            .cellSizeMeters = 1.0F,
            .chunkSizeCells = 4,
            .layers = {
                Tina::Editor::TileMapAuthoringLayer{
                    .stableLayerId = InitialTileMapLayerId,
                    .kind = Tina::AssetFormat::TileMapLayerKind::Tile,
                    .name = "Ground",
                    .chunks = {
                        Tina::Editor::TileMapAuthoringChunk{
                            .chunkX = 0,
                            .chunkY = 0,
                            .cells = std::move(leftChunkCells),
                        },
                        Tina::Editor::TileMapAuthoringChunk{
                            .chunkX = 1,
                            .chunkY = 0,
                            .cells = std::move(rightChunkCells),
                        },
                    },
                },
                Tina::Editor::TileMapAuthoringLayer{
                    .stableLayerId = 2,
                    .kind = Tina::AssetFormat::TileMapLayerKind::Object,
                    .name = "Gameplay",
                    .objects = {
                        Tina::Editor::TileMapAuthoringObject{
                            .stableObjectId = 1,
                            .kind = Tina::AssetFormat::TileMapObjectKind::Point,
                            .name = "Spawn",
                            .x = 1.5F,
                            .y = 1.5F,
                        },
                    },
                },
            },
        },
        Tina::Editor::TileMapAuthoringDocumentConfig{
            .layerCapacity = TileMapAuthoringLayerCapacity,
            .objectCapacity = 128,
            .chunkCapacity = 128,
            .historyEntryCapacity = 16,
            .historyByteCapacity = 4U * 1024U * 1024U,
        });
    if (!tileMap) {
        return Tina::Core::failure(std::move(tileMap.error()));
    }
    auto spriteAnimation = Tina::Editor::SpriteAnimationAuthoringDocument::Create(
        Tina::Editor::SpriteAnimationAuthoringDesc{
            .clipId = editorAssetId(0x50U),
            .playbackMode = Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop,
            .frames = {
                {.spriteId = editorAssetId(0x22U), .durationSeconds = 0.12F},
                {.spriteId = editorAssetId(0x23U), .durationSeconds = 0.12F},
                {.spriteId = editorAssetId(0x24U), .durationSeconds = 0.12F},
                {.spriteId = editorAssetId(0x25U), .durationSeconds = 0.18F},
            },
        },
        Tina::Editor::SpriteAnimationAuthoringDocumentConfig{
            .frameCapacity = 256,
            .historyEntryCapacity = 32,
            .historyByteCapacity = 512U * 1024U,
        });
    if (!spriteAnimation) {
        return Tina::Core::failure(std::move(spriteAnimation.error()));
    }
    return InitialAuthoringDocuments{
        .world2D = std::move(*document),
        .world3D = std::move(*world3D),
        .tileMap = std::move(*tileMap),
        .spriteAnimation = std::move(*spriteAnimation),
        .world2DSession = std::move(world2DSession),
        .world3DSession = std::move(world3DSession),
    };
}

class EditorWorkspaceState final : public Tina::IGameState {
  public:
    EditorWorkspaceState(EditorLaunchOptions options, LifecycleCounters& counters,
                         Tina::Editor::World2DAuthoringDocument world2D,
                         Tina::Editor::World3DAuthoringDocument world3D,
                         Tina::Editor::TileMapAuthoringDocument tileMap,
                         Tina::Editor::SpriteAnimationAuthoringDocument spriteAnimation,
                         WorkspaceSessionState world2DSession,
                         WorkspaceSessionState world3DSession,
                         EditorAssetResources& assetResources,
                         EditorRenderDeviceAccess& renderDeviceAccess) noexcept
        : options_(std::move(options)), counters_(counters), document_(std::move(world2D)),
          document3D_(std::move(world3D)), tileMapDocument_(std::move(tileMap)),
          spriteAnimationDocument_(std::move(spriteAnimation)),
          workspaceMode_(options_.initialWorkspace),
          world2DSession_(std::move(world2DSession)),
          world3DSession_(std::move(world3DSession)), assetResources_(assetResources),
          renderDeviceAccess_(renderDeviceAccess)
    {
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_.stateEnters;
        auto assetRollback = Tina::Core::makeScopeExit([this]() noexcept {
            releasePreviewAssetBindings();
        });
        if (auto status = preparePreviewAssetBindings(); !status) {
            return status;
        }

        auto rootBuilder = context.primaryWindowUIRootBuilder();
        if (!rootBuilder) {
            return Tina::Core::failure(std::move(rootBuilder.error()));
        }

        // Startup-only StyleClass/ColorToken/sheet must run before createRoot().
        auto dockClass = rootBuilder->registerStyleClass();
        if (!dockClass) {
            return Tina::Core::failure(std::move(dockClass.error()));
        }
        auto viewportClass = rootBuilder->registerStyleClass();
        if (!viewportClass) {
            return Tina::Core::failure(std::move(viewportClass.error()));
        }
        auto dockToken = rootBuilder->registerStyleColorToken(UI::rgb(0x152232));
        if (!dockToken) {
            return Tina::Core::failure(std::move(dockToken.error()));
        }
        auto viewportToken = rootBuilder->registerStyleColorToken(UI::rgb(0x0C141E));
        if (!viewportToken) {
            return Tina::Core::failure(std::move(viewportToken.error()));
        }
        dockClass_ = *dockClass;
        viewportClass_ = *viewportClass;
        viewportToken_ = *viewportToken;

        const std::array rules{
            UI::UIStyleBoxFillRule{
                .role = UI::UIStyleRoleId::PanelSurface,
                .styleClass = dockClass_,
                .colorToken = *dockToken,
            },
            UI::UIStyleBoxFillRule{
                .role = UI::UIStyleRoleId::PanelElevated,
                .styleClass = viewportClass_,
                .colorToken = viewportToken_,
            },
        };
        if (auto status = rootBuilder->installStyleSheet(std::span(rules)); !status) {
            return status;
        }
        counters_.stylesheetInstalled = true;
        counters_.styleRegisteredClasses = 2;
        counters_.styleRegisteredTokens = 2;
        counters_.styleActiveRules = rules.size();
        counters_.styleRevision = 1;

        auto root = rootBuilder->createRoot();
        if (!root) {
            return Tina::Core::failure(std::move(root.error()));
        }
        auto tree = rootBuilder->treeUpdater(*root);
        if (!tree) {
            return Tina::Core::failure(std::move(tree.error()));
        }

        const UI::UITheme productTheme = UI::makeDefaultProductTheme();
        if (auto status = tree->setProductTheme(productTheme); !status) {
            return status;
        }

        const UI::UITextStyle titleText = UI::makeTitleTextStyle(productTheme, 18.0F);
        const UI::UITextStyle sectionText = UI::makeTitleTextStyle(productTheme, 15.0F);
        const UI::UITextStyle bodyText = UI::makeBodyTextStyle(productTheme, 14.0F);
        const UI::UITextStyle compactText = UI::makeBodyTextStyle(productTheme, 12.0F);
        const UI::UITextStyle secondaryText = UI::makeSecondaryTextStyle(productTheme, 12.0F);
        const UI::UITextStyle accentText = UI::makeAccentTextStyle(productTheme, 13.0F);

        const auto storeNode = [](auto&& result, UI::UINodeId& output) -> Tina::Core::Status {
            if (!result) {
                return Tina::Core::failure(std::move(result.error()));
            }
            output = *result;
            return Tina::Core::success();
        };
        const auto createPanel = [&](UI::UINodeId parent, UI::UILayoutStyle layout,
                                     UI::UIStyleRoleId role,
                                     UI::UIStyleClassId styleClass = {}) {
            UI::UIElementDescriptor descriptor = UI::makePanelElement(layout);
            descriptor.visual.styleRole = role;
            if (styleClass.hasValue()) {
                descriptor.visual.styleClasses = std::span(&styleClass, 1);
            }
            return tree->createElement(parent, descriptor);
        };
        const auto createLabel = [&](UI::UINodeId parent, std::string_view text,
                                     UI::UILayoutStyle layout, const UI::UITextStyle& style) {
            UI::UIElementDescriptor descriptor = UI::makeLabelElement(text, layout);
            descriptor.textStyle = style;
            return tree->createElement(parent, descriptor);
        };
        const auto createButton = [&](UI::UINodeId parent, std::string_view text,
                                      UI::UILayoutStyle layout, bool enabled = true) {
            UI::UIElementDescriptor descriptor = UI::makeButtonElement(text, layout);
            descriptor.textStyle = compactText;
            descriptor.enabled = enabled;
            return tree->createElement(parent, descriptor);
        };
        const auto createTextEdit = [&](UI::UINodeId parent, std::string_view text,
                                        UI::UILayoutStyle layout, bool enabled) {
            UI::UIElementDescriptor descriptor = UI::makeTextEditElement(text, layout);
            descriptor.textStyle = compactText;
            descriptor.enabled = enabled;
            return tree->createElement(parent, descriptor);
        };

        UI::UILayoutStyle rootStyle = percentSize(100.0F, 100.0F);
        rootStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        rootStyle.flexContainer.gap.row = 6.0F;
        rootStyle.padding = UI::UIEdgeSpacing::All(10.0F);
        if (auto status = tree->setLayoutStyle(root->rootNodeId(), rootStyle); !status) {
            return status;
        }

        const UI::UINodeId rootNode = root->rootNodeId();
        UI::UINodeId toolbar{};
        UI::UILayoutStyle toolbarStyle = fillWidth(48.0F);
        toolbarStyle.flexItem.shrink = 0.0F;
        toolbarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        toolbarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        toolbarStyle.flexContainer.gap.column = 8.0F;
        toolbarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 7.0F);
        if (auto status = storeNode(createPanel(rootNode, toolbarStyle, UI::UIStyleRoleId::PanelSurface,
                                                dockClass_),
                                    toolbar);
            !status) {
            return status;
        }

        UI::UINodeId toolbarBrand{};
        if (auto status = storeNode(createLabel(toolbar, "TINA EDITOR", fixedSize(132.0F, 26.0F), titleText),
                                    toolbarBrand);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(toolbar,
                                                workspaceMode_ == WorkspaceMode::World2D
                                                    ? "World2D Scene"
                                                    : "World3D Scene",
                                                fixedSize(138.0F, 24.0F), bodyText),
                                    toolbarDocument_);
            !status) {
            return status;
        }
        UI::UILayoutStyle pathStyle = fixedSize(0.0F, 22.0F);
        pathStyle.size.width = UI::UILayoutLength::Auto();
        pathStyle.flexItem.grow = 1.0F;
        pathStyle.flexItem.shrink = 1.0F;
        pathStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        const WorkspaceSessionState& initialSession = activeWorkspaceSession();
        const std::string_view initialPathStatus =
            !initialSession.hasDocumentPath()
                ? "No save path | Unsaved"
                : (initialSession.loadedFromPath ? "Existing document opened | Saved"
                                                 : "Save target configured | Modified");
        if (auto status = storeNode(createLabel(toolbar, initialPathStatus, pathStyle, secondaryText),
                                    toolbarPath_);
            !status) {
            return status;
        }

        if (auto status = storeNode(createButton(toolbar, "2D", fixedSize(46.0F, 30.0F),
                                                 workspaceMode_ != WorkspaceMode::World2D),
                                    mode2DButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "3D", fixedSize(46.0F, 30.0F),
                                                 workspaceMode_ != WorkspaceMode::World3D),
                                    mode3DButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "Undo", fixedSize(60.0F, 30.0F)), undoButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "Redo", fixedSize(60.0F, 30.0F)), redoButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "Save", fixedSize(58.0F, 30.0F),
                                                 initialSession.hasDocumentPath()),
                                    saveButton_);
            !status) {
            return status;
        }

        UI::UINodeId contextBar{};
        UI::UILayoutStyle contextBarStyle = fillWidth(34.0F);
        contextBarStyle.flexItem.shrink = 0.0F;
        contextBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        contextBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        contextBarStyle.flexContainer.gap.column = 6.0F;
        contextBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 3.0F);
        if (auto status = storeNode(createPanel(rootNode, contextBarStyle, UI::UIStyleRoleId::PanelElevated,
                                                viewportClass_),
                                    contextBar);
            !status) {
            return status;
        }
        UI::UILayoutStyle breadcrumbStyle = fixedSize(0.0F, 22.0F);
        breadcrumbStyle.size.width = UI::UILayoutLength::Auto();
        breadcrumbStyle.flexItem.grow = 1.0F;
        breadcrumbStyle.flexItem.shrink = 1.0F;
        breadcrumbStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(createLabel(contextBar,
                                                workspaceMode_ == WorkspaceMode::World2D
                                                    ? "Scene / World2D / Player"
                                                    : "Scene / World3D / HeroMesh",
                                                breadcrumbStyle, secondaryText),
                                    breadcrumb_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(contextBar, "Select", fixedSize(58.0F, 26.0F), false),
                                    selectToolButtons_[0]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(contextBar, "Move", fixedSize(58.0F, 26.0F)),
                                    moveToolButtons_[0]);
            !status) {
            return status;
        }
        UI::UINodeId contextFrameButton{};
        if (auto status = storeNode(createButton(contextBar, "Frame", fixedSize(58.0F, 26.0F), false),
                                    contextFrameButton);
            !status) {
            return status;
        }
        UI::UINodeId snapStatus{};
        if (auto status = storeNode(createLabel(contextBar, "Free Move", fixedSize(72.0F, 20.0F), accentText),
                                    snapStatus);
            !status) {
            return status;
        }

        UI::UINodeId body{};
        UI::UILayoutStyle bodyStyle = growingRegion();
        bodyStyle.minMax.minHeight = UI::UILayoutLength::Px(320.0F);
        bodyStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        bodyStyle.flexContainer.gap.column = 8.0F;
        if (auto status = storeNode(createPanel(rootNode, bodyStyle, UI::UIStyleRoleId::None), body); !status) {
            return status;
        }

        UI::UINodeId left{};
        UI::UILayoutStyle leftStyle = boundedDock(22.0F, 220.0F, 300.0F);
        leftStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        leftStyle.flexContainer.gap.row = 7.0F;
        leftStyle.padding = UI::UIEdgeSpacing::All(8.0F);
        if (auto status = storeNode(createPanel(body, leftStyle, UI::UIStyleRoleId::PanelSurface, dockClass_), left);
            !status) {
            return status;
        }

        UI::UINodeId hierarchyHeader{};
        UI::UILayoutStyle hierarchyHeaderStyle = fillWidth(28.0F);
        hierarchyHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        hierarchyHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        if (auto status = storeNode(createPanel(left, hierarchyHeaderStyle, UI::UIStyleRoleId::None),
                                    hierarchyHeader);
            !status) {
            return status;
        }
        UI::UILayoutStyle hierarchyTitleStyle = fixedSize(0.0F, 24.0F);
        hierarchyTitleStyle.size.width = UI::UILayoutLength::Auto();
        hierarchyTitleStyle.flexItem.grow = 1.0F;
        hierarchyTitleStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        UI::UINodeId hierarchyTitle{};
        if (auto status = storeNode(createLabel(hierarchyHeader, "Hierarchy", hierarchyTitleStyle, sectionText),
                                    hierarchyTitle);
            !status) {
            return status;
        }
        UI::UINodeId hierarchyCount{};
        if (auto status = storeNode(createLabel(hierarchyHeader, "5 entities", fixedSize(66.0F, 20.0F),
                                                secondaryText),
                                    hierarchyCount);
            !status) {
            return status;
        }

        UI::UINodeId hierarchyFilter{};
        if (auto status = storeNode(createTextEdit(left, "Filter hierarchy", fillWidth(32.0F), false),
                                    hierarchyFilter);
            !status) {
            return status;
        }
        UI::UINodeId hierarchyActions{};
        UI::UILayoutStyle hierarchyActionsStyle = fillWidth(30.0F);
        hierarchyActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        hierarchyActionsStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(left, hierarchyActionsStyle, UI::UIStyleRoleId::None),
                                    hierarchyActions);
            !status) {
            return status;
        }
        UI::UILayoutStyle hierarchyActionStyle = fixedSize(0.0F, 30.0F);
        hierarchyActionStyle.size.width = UI::UILayoutLength::Auto();
        hierarchyActionStyle.flexItem.grow = 1.0F;
        hierarchyActionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        for (const std::string_view action : {std::string_view{"Add Entity"},
                                              std::string_view{"Focus"}}) {
            UI::UINodeId actionButton{};
            if (auto status = storeNode(createButton(hierarchyActions, action, hierarchyActionStyle, false),
                                        actionButton);
                !status) {
                return status;
            }
        }

        UI::UILayoutStyle hierarchyStyle = growingRegion();
        hierarchyStyle.minMax.minHeight = UI::UILayoutLength::Px(160.0F);
        auto hierarchy = tree->createElement(
            left, UI::makeTreeViewElement({.materializedItemCapacity = HierarchyMaterializedCapacity},
                                          hierarchyStyle));
        if (!hierarchy) {
            return Tina::Core::failure(std::move(hierarchy.error()));
        }
        hierarchyTree_ = *hierarchy;
        if (auto status = tree->setTreeViewStyle(
                hierarchyTree_,
                UI::UITreeViewStyle{
                    .rowHeight = 32.0F,
                    .overscanRows = 1,
                    .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                    .wheelStep = 32.0F,
                    .indentation = 16.0F,
                    .disclosureExtent = 10.0F,
                    .disclosureGap = 4.0F,
                });
            !status) {
            return status;
        }
        if (auto status = tree->setTreeViewPaint(hierarchyTree_, UI::makeTreeViewPaint(productTheme)); !status) {
            return status;
        }
        if (auto status = tree->setTreeViewDataSource(hierarchyTree_, hierarchyDataSource()); !status) {
            return status;
        }
        if (auto status = tree->setTreeViewSelectedIndex(hierarchyTree_, 0); !status) {
            return status;
        }

        UI::UINodeId hierarchyFooter{};
        UI::UILayoutStyle hierarchyFooterStyle = fillWidth(48.0F);
        hierarchyFooterStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(8.0F, 5.0F);
        hierarchyFooterStyle.flexContainer.gap.row = 2.0F;
        if (auto status = storeNode(createPanel(left, hierarchyFooterStyle, UI::UIStyleRoleId::PanelElevated),
                                    hierarchyFooter);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(hierarchyFooter, {}, fillWidth(20.0F), bodyText),
                                    hierarchySelectionSummary_);
            !status) {
            return status;
        }
        UI::UINodeId hierarchyFooterHint{};
        if (auto status = storeNode(createLabel(hierarchyFooter, "Stable authoring identity", fillWidth(18.0F),
                                                secondaryText),
                                    hierarchyFooterHint);
            !status) {
            return status;
        }

        UI::UINodeId center{};
        UI::UILayoutStyle centerStyle = growingRegion();
        centerStyle.size.width = UI::UILayoutLength::Percent(52.0F);
        centerStyle.minMax.minWidth = UI::UILayoutLength::Px(360.0F);
        centerStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        centerStyle.flexContainer.gap.row = 7.0F;
        centerStyle.padding = UI::UIEdgeSpacing::All(8.0F);
        if (auto status = storeNode(createPanel(body, centerStyle, UI::UIStyleRoleId::None), center);
            !status) {
            return status;
        }

        UI::UINodeId viewportHeader{};
        UI::UILayoutStyle viewportHeaderStyle = fillWidth(30.0F);
        viewportHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        viewportHeaderStyle.flexContainer.gap.column = 8.0F;
        if (auto status = storeNode(createPanel(center, viewportHeaderStyle, UI::UIStyleRoleId::None),
                                    viewportHeader);
            !status) {
            return status;
        }
        UI::UILayoutStyle viewportTitleStyle = fixedSize(0.0F, 24.0F);
        viewportTitleStyle.size.width = UI::UILayoutLength::Auto();
        viewportTitleStyle.flexItem.grow = 1.0F;
        viewportTitleStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(createLabel(viewportHeader,
                                                workspaceMode_ == WorkspaceMode::World2D
                                                    ? "World2D Viewport"
                                                    : "World3D Viewport",
                                                viewportTitleStyle, sectionText),
                                    viewportTitle_);
            !status) {
            return status;
        }
        UI::UIElementDescriptor viewportModeDesc =
            UI::makeDropdownElement(workspaceMode_ == WorkspaceMode::World2D
                                        ? "Orthographic"
                                        : "Perspective",
                                    fixedSize(108.0F, 28.0F));
        viewportModeDesc.textStyle = compactText;
        viewportModeDesc.enabled = false;
        if (auto status = storeNode(tree->createElement(viewportHeader, viewportModeDesc),
                                    viewportMode_);
            !status) {
            return status;
        }

        UI::UINodeId viewportTools{};
        UI::UILayoutStyle viewportToolsStyle = fillWidth(32.0F);
        viewportToolsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportToolsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        viewportToolsStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(center, viewportToolsStyle, UI::UIStyleRoleId::PanelSurface),
                                    viewportTools);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportTools, "Select", fixedSize(64.0F, 28.0F), false),
                                    selectToolButtons_[1]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportTools, "Move", fixedSize(64.0F, 28.0F)),
                                    moveToolButtons_[1]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportTools, "Paint", fixedSize(60.0F, 28.0F), false),
                                    tilePaintToolButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportTools, "Erase", fixedSize(60.0F, 28.0F), false),
                                    tileEraseToolButton_);
            !status) {
            return status;
        }
        UI::UINodeId frameAllButton{};
        if (auto status = storeNode(createButton(viewportTools, "Frame All", fixedSize(64.0F, 28.0F), false),
                                    frameAllButton);
            !status) {
            return status;
        }
        UI::UILayoutStyle zoomSliderStyle = fixedSize(0.0F, 24.0F);
        zoomSliderStyle.size.width = UI::UILayoutLength::Auto();
        zoomSliderStyle.flexItem.grow = 1.0F;
        zoomSliderStyle.flexItem.shrink = 1.0F;
        zoomSliderStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        UI::UIElementDescriptor zoomSliderDesc = UI::makeSliderElement(zoomSliderStyle);
        zoomSliderDesc.enabled = false;
        UI::UINodeId zoomSlider{};
        if (auto status = storeNode(tree->createElement(viewportTools, zoomSliderDesc), zoomSlider); !status) {
            return status;
        }
        if (auto status = tree->setSliderRange(zoomSlider, 25.0F, 400.0F, 25.0F); !status) {
            return status;
        }
        if (auto status = tree->setSliderValue(zoomSlider, 100.0F); !status) {
            return status;
        }
        UI::UINodeId zoomValue{};
        if (auto status = storeNode(createLabel(viewportTools, "100%", fixedSize(42.0F, 20.0F), accentText),
                                    zoomValue);
            !status) {
            return status;
        }

        UI::UINodeId viewportCanvas{};
        UI::UILayoutStyle viewportCanvasStyle = growingRegion();
        viewportCanvasStyle.minMax.minHeight = UI::UILayoutLength::Px(220.0F);
        viewportCanvasStyle.padding = UI::UIEdgeSpacing::All(12.0F);
        viewportCanvasStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        if (auto status = storeNode(createPanel(center, viewportCanvasStyle, UI::UIStyleRoleId::None),
                                    viewportCanvas);
            !status) {
            return status;
        }
        UI::UINodeId viewportCanvasTop{};
        UI::UILayoutStyle viewportCanvasTopStyle = fillWidth(22.0F);
        viewportCanvasTopStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportCanvasTopStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        if (auto status = storeNode(createPanel(viewportCanvas, viewportCanvasTopStyle, UI::UIStyleRoleId::None),
                                    viewportCanvasTop);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(viewportCanvasTop,
                                                workspaceMode_ == WorkspaceMode::World2D
                                                    ? "Tile Grid 1 m"
                                                    : "Grid 1 m",
                                                fixedSize(76.0F, 20.0F), secondaryText),
                                    gridStatus_);
            !status) {
            return status;
        }
        const std::string_view initialAssetStatus =
            counters_.catalogUnresolvedReferences != 0
                ? "Catalog refs unresolved"
                : (assetResources_.projectCatalogConfigured ? "Project Catalog ready"
                                                            : "Built-in Catalog ready");
        if (auto status = storeNode(createLabel(viewportCanvasTop, initialAssetStatus,
                                                fixedSize(154.0F, 20.0F), accentText),
                                    previewAssetStatus_);
            !status) {
            return status;
        }

        UI::UINodeId viewportSceneArea{};
        UI::UILayoutStyle viewportSceneAreaStyle = growingRegion();
        viewportSceneAreaStyle.flexContainer.justifyContent = UI::UIJustifyContent::Center;
        viewportSceneAreaStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        if (auto status = storeNode(createPanel(viewportCanvas, viewportSceneAreaStyle,
                                                UI::UIStyleRoleId::None),
                                    viewportSceneArea);
            !status) {
            return status;
        }
        UI::UINodeId previewFrame{};
        UI::UILayoutStyle previewFrameStyle = fixedSize(300.0F, 146.0F);
        previewFrameStyle = percentSize(86.0F, 82.0F);
        previewFrameStyle.minMax.minWidth = UI::UILayoutLength::Px(300.0F);
        previewFrameStyle.minMax.minHeight = UI::UILayoutLength::Px(146.0F);
        previewFrameStyle.minMax.maxWidth = UI::UILayoutLength::Px(600.0F);
        previewFrameStyle.minMax.maxHeight = UI::UILayoutLength::Px(360.0F);
        previewFrameStyle.padding = UI::UIEdgeSpacing::All(14.0F);
        previewFrameStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        previewFrameStyle.flexContainer.justifyContent = UI::UIJustifyContent::Center;
        previewFrameStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        previewFrameStyle.flexContainer.gap.row = 8.0F;
        if (auto status = storeNode(createPanel(viewportSceneArea, previewFrameStyle,
                                                UI::UIStyleRoleId::None),
                                    previewFrame);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(previewFrame,
                                                workspaceMode_ == WorkspaceMode::World2D
                                                    ? "World2D Scene"
                                                    : "World3D Scene",
                                                fixedSize(170.0F, 24.0F), titleText),
                                    previewTitle_);
            !status) {
            return status;
        }
        UI::UILayoutStyle previewWorldLayerStyle = growingRegion();
        previewWorldLayerStyle.minMax.minHeight = UI::UILayoutLength::Px(72.0F);
        previewWorldLayerStyle.placement = UI::UILayoutPlacement::Flow;
        if (auto status = storeNode(createPanel(previewFrame, previewWorldLayerStyle,
                                                UI::UIStyleRoleId::None),
                                    viewportPreviewLayer_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(previewFrame,
                                                workspaceMode_ == WorkspaceMode::World2D
                                                    ? "Camera | Player | TileMap"
                                                    : "Camera | Hero | Left | Right",
                                                fixedSize(240.0F, 22.0F), bodyText),
                                    previewEntities_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(previewFrame,
                                                workspaceMode_ == WorkspaceMode::World2D
                                                    ? "World2D + TileMap -> Scene"
                                                    : "Prefab v2 -> Catalog -> Scene",
                                                fixedSize(242.0F, 20.0F), secondaryText),
                                    previewCook_);
            !status) {
            return status;
        }

        UI::UINodeId viewportCanvasBottom{};
        UI::UILayoutStyle viewportCanvasBottomStyle = fillWidth(22.0F);
        viewportCanvasBottomStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportCanvasBottomStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        if (auto status = storeNode(createPanel(viewportCanvas, viewportCanvasBottomStyle,
                                                UI::UIStyleRoleId::None),
                                    viewportCanvasBottom);
            !status) {
            return status;
        }
        UI::UINodeId viewportOrigin{};
        if (auto status = storeNode(createLabel(viewportCanvasBottom, "Origin 0, 0", fixedSize(76.0F, 20.0F),
                                                secondaryText),
                                    viewportOrigin);
            !status) {
            return status;
        }
        UI::UINodeId viewportExtent{};
        if (auto status = storeNode(createLabel(viewportCanvasBottom, "1280 x 800 logical",
                                                fixedSize(120.0F, 20.0F), secondaryText),
                                    viewportExtent);
            !status) {
            return status;
        }

        UI::UINodeId viewportFooter{};
        UI::UILayoutStyle viewportFooterStyle = fillWidth(28.0F);
        viewportFooterStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportFooterStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        viewportFooterStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        viewportFooterStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(8.0F, 3.0F);
        if (auto status = storeNode(createPanel(center, viewportFooterStyle, UI::UIStyleRoleId::PanelSurface),
                                    viewportFooter);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(viewportFooter,
                                                workspaceMode_ == WorkspaceMode::World2D
                                                    ? "Camera2D"
                                                    : "Camera3D",
                                                fixedSize(70.0F, 20.0F), bodyText),
                                    cameraStatus_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(viewportFooter, "Select | Local | Snap",
                                                fixedSize(128.0F, 20.0F), secondaryText),
                                    viewportToolStatus_);
            !status) {
            return status;
        }

        UI::UINodeId right{};
        UI::UILayoutStyle rightStyle = boundedDock(26.0F, 280.0F, 360.0F);
        rightStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        rightStyle.flexContainer.gap.row = 7.0F;
        rightStyle.padding = UI::UIEdgeSpacing::All(8.0F);
        if (auto status = storeNode(createPanel(body, rightStyle, UI::UIStyleRoleId::PanelSurface, dockClass_),
                                    right);
            !status) {
            return status;
        }

        UI::UINodeId inspectorHeader{};
        UI::UILayoutStyle inspectorHeaderStyle = fillWidth(30.0F);
        inspectorHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        inspectorHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        inspectorHeaderStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        if (auto status = storeNode(createPanel(right, inspectorHeaderStyle, UI::UIStyleRoleId::None),
                                    inspectorHeader);
            !status) {
            return status;
        }
        UI::UINodeId inspectorTitle{};
        if (auto status = storeNode(createLabel(inspectorHeader, "Inspector", fixedSize(110.0F, 24.0F),
                                                sectionText),
                                    inspectorTitle);
            !status) {
            return status;
        }
        UI::UINodeId inspectorMode{};
        if (auto status = storeNode(createLabel(inspectorHeader, "Selection", fixedSize(58.0F, 20.0F),
                                                accentText),
                                    inspectorMode);
            !status) {
            return status;
        }

        UI::UILayoutStyle inspectorScrollStyle = growingRegion();
        inspectorScrollStyle.minMax.minHeight = UI::UILayoutLength::Px(180.0F);
        auto inspectorScroll = tree->createElement(right, UI::makeScrollViewElement(inspectorScrollStyle));
        if (!inspectorScroll) {
            return Tina::Core::failure(std::move(inspectorScroll.error()));
        }
        if (auto status = tree->setScrollViewStyle(
                *inspectorScroll,
                UI::UIScrollViewStyle{
                    .axes = UI::UIScrollAxes::Vertical,
                    .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                    .wheelStep = 36.0F,
                });
            !status) {
            return status;
        }
        counters_.inspectorScrollConfigured = true;

        UI::UINodeId inspectorContent{};
        UI::UILayoutStyle inspectorContentStyle = fillWidth(940.0F);
        inspectorContentStyle.padding = UI::UIEdgeSpacing::All(8.0F);
        inspectorContentStyle.flexContainer.gap.row = 7.0F;
        if (auto status = storeNode(createPanel(*inspectorScroll, inspectorContentStyle,
                                                UI::UIStyleRoleId::PanelSurface),
                                    inspectorContent);
            !status) {
            return status;
        }

        UI::UINodeId identityTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Identity", fillWidth(22.0F), sectionText),
                                    identityTitle);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(22.0F), bodyText),
                                    inspectorName_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(22.0F), secondaryText),
                                    inspectorKind_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(42.0F), secondaryText),
                                    inspectorNote_);
            !status) {
            return status;
        }

        UI::UINodeId transformTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Transform", fillWidth(22.0F), sectionText),
                                    transformTitle);
            !status) {
            return status;
        }
        const auto createTransformRow = [&](std::string_view caption, std::string_view value,
                                            bool enabled, UI::UINodeId& valueNode) -> Tina::Core::Status {
            UI::UINodeId row{};
            UI::UILayoutStyle rowStyle = fillWidth(30.0F);
            rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
            rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
            rowStyle.flexContainer.gap.column = 8.0F;
            if (auto status = storeNode(createPanel(inspectorContent, rowStyle, UI::UIStyleRoleId::None), row);
                !status) {
                return status;
            }
            UI::UINodeId captionNode{};
            if (auto status = storeNode(createLabel(row, caption, fixedSize(74.0F, 20.0F), secondaryText),
                                        captionNode);
                !status) {
                return status;
            }
            UI::UILayoutStyle valueStyle = fixedSize(0.0F, 30.0F);
            valueStyle.size.width = UI::UILayoutLength::Auto();
            valueStyle.flexItem.grow = 1.0F;
            valueStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
            return storeNode(createTextEdit(row, value, valueStyle, enabled), valueNode);
        };
        if (auto status = createTransformRow("Position X", "0.000", true, inspectorPositionX_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Position Y", "0.000", true, inspectorPositionY_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Position Z", "0.000", false, inspectorPositionZ_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Rotation X", "0.000", false, inspectorRotationX_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Rotation Y", "0.000", false, inspectorRotationY_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Rotation Z", "0.000", true, inspectorRotationZ_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Scale X", "1.000", true, inspectorScaleX_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Scale Y", "1.000", true, inspectorScaleY_); !status) {
            return status;
        }
        if (auto status = createTransformRow("Scale Z", "1.000", false, inspectorScaleZ_); !status) {
            return status;
        }
        if (auto status = storeNode(createButton(inspectorContent, "Apply Transform", fillWidth(34.0F)),
                                    applyTransformButton_);
            !status) {
            return status;
        }

        UI::UINodeId componentsTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Components", fillWidth(22.0F), sectionText),
                                    componentsTitle);
            !status) {
            return status;
        }
        const std::array<std::string_view, 3> componentNames =
            workspaceMode_ == WorkspaceMode::World2D
                ? std::array<std::string_view, 3>{"Transform", "SpriteRenderer2D",
                                                  "Runtime preview"}
                : std::array<std::string_view, 3>{"Transform", "MeshRenderer3D",
                                                  "Runtime preview"};
        for (u32 componentIndex = 0; componentIndex < componentNames.size(); ++componentIndex) {
            UI::UINodeId componentRow{};
            UI::UILayoutStyle componentRowStyle = fillWidth(24.0F);
            componentRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
            componentRowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
            componentRowStyle.flexContainer.gap.column = 8.0F;
            if (auto status = storeNode(createPanel(inspectorContent, componentRowStyle,
                                                    UI::UIStyleRoleId::None),
                                        componentRow);
                !status) {
                return status;
            }
            UI::UINodeId componentCheckbox{};
            UI::UIElementDescriptor checkboxDesc = UI::makeCheckboxElement(fixedSize(20.0F, 20.0F));
            checkboxDesc.enabled = false;
            if (auto status = storeNode(tree->createElement(componentRow, checkboxDesc), componentCheckbox);
                !status) {
                return status;
            }
            if (auto status = tree->setChecked(componentCheckbox, true); !status) {
                return status;
            }
            if (auto status = storeNode(createLabel(componentRow, componentNames[componentIndex],
                                                    fixedSize(170.0F, 20.0F), bodyText),
                                        componentLabels_[componentIndex]);
                !status) {
                return status;
            }
        }

        UI::UINodeId authoringTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Authoring", fillWidth(22.0F), sectionText),
                                    authoringTitle);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(inspectorContent, "Move X +1", fillWidth(34.0F)), moveButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, "One validated revision per command",
                                                fillWidth(20.0F), secondaryText),
                                    authoringHint_);
            !status) {
            return status;
        }

        UI::UINodeId tileMapTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "TileMap", fillWidth(22.0F), sectionText),
                                    tileMapTitle);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(42.0F), secondaryText),
                                    tileMapStatus_);
            !status) {
            return status;
        }
        const auto createTileMapActionRow = [&](UI::UINodeId& row) -> Tina::Core::Status {
            UI::UILayoutStyle rowStyle = fillWidth(34.0F);
            rowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
            rowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
            rowStyle.flexContainer.gap.column = 6.0F;
            return storeNode(createPanel(inspectorContent, rowStyle, UI::UIStyleRoleId::None), row);
        };
        UI::UINodeId tileMapBrushRow{};
        if (auto status = createTileMapActionRow(tileMapBrushRow); !status) {
            return status;
        }
        if (auto status = storeNode(createButton(tileMapBrushRow, "Paint Cell", fixedSize(104.0F, 30.0F)),
                                    paintTileButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(tileMapBrushRow, "Erase Cell", fixedSize(104.0F, 30.0F)),
                                    eraseTileButton_);
            !status) {
            return status;
        }
        UI::UINodeId tileMapLayerRow{};
        if (auto status = createTileMapActionRow(tileMapLayerRow); !status) {
            return status;
        }
        if (auto status = storeNode(createButton(tileMapLayerRow, "Toggle Layer", fixedSize(104.0F, 30.0F)),
                                    toggleTileLayerButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(tileMapLayerRow, "Cook Preview", fixedSize(104.0F, 30.0F)),
                                    cookTileMapButton_);
            !status) {
            return status;
        }
        UI::UINodeId tileMapAddRow{};
        if (auto status = createTileMapActionRow(tileMapAddRow); !status) {
            return status;
        }
        if (auto status = storeNode(createButton(tileMapAddRow, "+ Tile Layer", fixedSize(104.0F, 30.0F)),
                                    addTileLayerButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(tileMapAddRow, "+ Object Layer", fixedSize(104.0F, 30.0F)),
                                    addObjectLayerButton_);
            !status) {
            return status;
        }

        UI::UINodeId documentTitle{};
        if (auto status = storeNode(createLabel(inspectorContent, "Document", fillWidth(22.0F), sectionText),
                                    documentTitle);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(22.0F), bodyText),
                                    inspectorDocument_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent,
                                                workspaceMode_ == WorkspaceMode::World2D
                                                    ? "World2D v1 + TileMap v3/v1 | canonical"
                                                    : "Prefab schema v2 | canonical",
                                                fillWidth(20.0F), secondaryText),
                                    documentFormat_);
            !status) {
            return status;
        }

        UI::UINodeId animationTimeline{};
        UI::UILayoutStyle animationTimelineStyle = fillWidth(140.0F);
        animationTimelineStyle.flexItem.shrink = 0.0F;
        animationTimelineStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        animationTimelineStyle.flexContainer.gap.row = 5.0F;
        animationTimelineStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 6.0F);
        if (auto status = storeNode(createPanel(rootNode, animationTimelineStyle,
                                                UI::UIStyleRoleId::PanelSurface, dockClass_),
                                    animationTimeline);
            !status) {
            return status;
        }

        UI::UINodeId animationHeader{};
        UI::UILayoutStyle animationHeaderStyle = fillWidth(30.0F);
        animationHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        animationHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        animationHeaderStyle.flexContainer.gap.column = 7.0F;
        if (auto status = storeNode(createPanel(animationTimeline, animationHeaderStyle,
                                                UI::UIStyleRoleId::None),
                                    animationHeader);
            !status) {
            return status;
        }
        UI::UINodeId animationTitle{};
        if (auto status = storeNode(createLabel(animationHeader, "SpriteAnimationClip Timeline",
                                                fixedSize(210.0F, 24.0F), sectionText),
                                    animationTitle);
            !status) {
            return status;
        }
        UI::UILayoutStyle animationStatusStyle = fixedSize(0.0F, 22.0F);
        animationStatusStyle.size.width = UI::UILayoutLength::Auto();
        animationStatusStyle.flexItem.grow = 1.0F;
        animationStatusStyle.flexItem.shrink = 1.0F;
        animationStatusStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(createLabel(animationHeader, {}, animationStatusStyle,
                                                secondaryText),
                                    animationStatus_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationHeader, "Mode: Loop",
                                                 fixedSize(96.0F, 28.0F)),
                                    animationModeButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationHeader, "Play",
                                                 fixedSize(58.0F, 28.0F)),
                                    animationPlayButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationHeader, "Cook",
                                                 fixedSize(58.0F, 28.0F)),
                                    animationCookButton_);
            !status) {
            return status;
        }

        UI::UINodeId animationFrames{};
        UI::UILayoutStyle animationFramesStyle = fillWidth(32.0F);
        animationFramesStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        animationFramesStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        animationFramesStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(animationTimeline, animationFramesStyle,
                                                UI::UIStyleRoleId::None),
                                    animationFrames);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationFrames, "Prev",
                                                 fixedSize(54.0F, 28.0F)),
                                    animationPreviousButton_);
            !status) {
            return status;
        }
        for (u32 frameIndex = 0; frameIndex < animationFrameButtons_.size(); ++frameIndex) {
            if (auto status = storeNode(createButton(animationFrames, "--",
                                                     fixedSize(74.0F, 28.0F), false),
                                        animationFrameButtons_[frameIndex]);
                !status) {
                return status;
            }
        }
        if (auto status = storeNode(createButton(animationFrames, "Next",
                                                 fixedSize(54.0F, 28.0F)),
                                    animationNextButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationFrames, "Add",
                                                 fixedSize(50.0F, 28.0F)),
                                    animationAddButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationFrames, "Duplicate",
                                                 fixedSize(76.0F, 28.0F)),
                                    animationDuplicateButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationFrames, "Delete",
                                                 fixedSize(58.0F, 28.0F)),
                                    animationDeleteButton_);
            !status) {
            return status;
        }

        UI::UINodeId animationEditRow{};
        UI::UILayoutStyle animationEditStyle = fillWidth(30.0F);
        animationEditStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        animationEditStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        animationEditStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(animationTimeline, animationEditStyle,
                                                UI::UIStyleRoleId::None),
                                    animationEditRow);
            !status) {
            return status;
        }
        UI::UILayoutStyle animationSelectionStyle = fixedSize(0.0F, 22.0F);
        animationSelectionStyle.size.width = UI::UILayoutLength::Auto();
        animationSelectionStyle.flexItem.grow = 1.0F;
        animationSelectionStyle.flexItem.shrink = 1.0F;
        animationSelectionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(createLabel(animationEditRow, {}, animationSelectionStyle,
                                                accentText),
                                    animationSelection_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationEditRow, "Next Sprite",
                                                 fixedSize(82.0F, 28.0F)),
                                    animationCycleSpriteButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationEditRow, "Move Left",
                                                 fixedSize(78.0F, 28.0F)),
                                    animationMoveLeftButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationEditRow, "Move Right",
                                                 fixedSize(82.0F, 28.0F)),
                                    animationMoveRightButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationEditRow, "Duration -",
                                                 fixedSize(82.0F, 28.0F)),
                                    animationDurationDecreaseButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationEditRow, "Duration +",
                                                 fixedSize(82.0F, 28.0F)),
                                    animationDurationIncreaseButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationEditRow, "Undo",
                                                 fixedSize(54.0F, 28.0F)),
                                    animationUndoButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(animationEditRow, "Redo",
                                                 fixedSize(54.0F, 28.0F)),
                                    animationRedoButton_);
            !status) {
            return status;
        }

        UI::UINodeId statusBar{};
        UI::UILayoutStyle statusBarStyle = fillWidth(30.0F);
        statusBarStyle.flexItem.shrink = 0.0F;
        statusBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        statusBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        statusBarStyle.flexContainer.gap.column = 12.0F;
        statusBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(10.0F, 4.0F);
        if (auto status = storeNode(createPanel(rootNode, statusBarStyle, UI::UIStyleRoleId::PanelSurface,
                                                dockClass_),
                                    statusBar);
            !status) {
            return status;
        }
        UI::UILayoutStyle statusSegmentStyle = fixedSize(0.0F, 20.0F);
        statusSegmentStyle.size.width = UI::UILayoutLength::Auto();
        statusSegmentStyle.flexItem.grow = 1.0F;
        statusSegmentStyle.flexItem.shrink = 1.0F;
        statusSegmentStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(createLabel(statusBar, {}, statusSegmentStyle, compactText),
                                    statusDocument_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(statusBar, {}, statusSegmentStyle, secondaryText),
                                    statusPreview_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(statusBar, {}, fixedSize(190.0F, 20.0F), accentText),
                                    statusSelection_);
            !status) {
            return status;
        }

        if (auto status = tree->setButtonAction(
                mode2DButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::SwitchToWorld2D);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                mode3DButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::SwitchToWorld3D);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                moveButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::MoveSelectedPositiveX);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                applyTransformButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::ApplyTransform);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                undoButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::Undo);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                redoButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::Redo);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                saveButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::Save);
                }});
            !status) {
            return status;
        }
        const auto bindEditorCommand = [&](UI::UINodeId button,
                                           EditorCommand command) -> Tina::Core::Status {
            return tree->setButtonAction(
                button, UI::UIButtonActionCallback{[this, command](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(command);
                }});
        };
        if (auto status = bindEditorCommand(paintTileButton_, EditorCommand::PaintTile); !status) {
            return status;
        }
        if (auto status = bindEditorCommand(eraseTileButton_, EditorCommand::EraseTile); !status) {
            return status;
        }
        if (auto status = bindEditorCommand(toggleTileLayerButton_, EditorCommand::ToggleTileLayer); !status) {
            return status;
        }
        if (auto status = bindEditorCommand(addTileLayerButton_, EditorCommand::AddTileLayer); !status) {
            return status;
        }
        if (auto status = bindEditorCommand(addObjectLayerButton_, EditorCommand::AddObjectLayer); !status) {
            return status;
        }
        if (auto status = bindEditorCommand(cookTileMapButton_, EditorCommand::CookTileMapPreview); !status) {
            return status;
        }
        const std::array animationCommandBindings{
            std::pair{animationPlayButton_, EditorCommand::AnimationTogglePlayback},
            std::pair{animationPreviousButton_, EditorCommand::AnimationPreviousFrame},
            std::pair{animationNextButton_, EditorCommand::AnimationNextFrame},
            std::pair{animationAddButton_, EditorCommand::AnimationAddFrame},
            std::pair{animationDuplicateButton_, EditorCommand::AnimationDuplicateFrame},
            std::pair{animationDeleteButton_, EditorCommand::AnimationDeleteFrame},
            std::pair{animationMoveLeftButton_, EditorCommand::AnimationMoveFrameLeft},
            std::pair{animationMoveRightButton_, EditorCommand::AnimationMoveFrameRight},
            std::pair{animationCycleSpriteButton_, EditorCommand::AnimationCycleSprite},
            std::pair{animationDurationDecreaseButton_, EditorCommand::AnimationDecreaseDuration},
            std::pair{animationDurationIncreaseButton_, EditorCommand::AnimationIncreaseDuration},
            std::pair{animationModeButton_, EditorCommand::AnimationCycleMode},
            std::pair{animationCookButton_, EditorCommand::AnimationCookPreview},
            std::pair{animationUndoButton_, EditorCommand::AnimationUndo},
            std::pair{animationRedoButton_, EditorCommand::AnimationRedo},
        };
        for (const auto& [button, command] : animationCommandBindings) {
            if (auto status = bindEditorCommand(button, command); !status) {
                return status;
            }
        }
        for (u32 frameIndex = 0; frameIndex < animationFrameButtons_.size(); ++frameIndex) {
            if (auto status = tree->setButtonAction(
                    animationFrameButtons_[frameIndex],
                    UI::UIButtonActionCallback{
                        [this, frameIndex](const UI::UIButtonActionEvent&) noexcept {
                            pendingAnimationFrameSelection_ = frameIndex;
                        }});
                !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : selectToolButtons_) {
            if (auto status = tree->setButtonAction(
                    button, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                        queueViewportToolMode(ViewportToolMode::Select);
                    }});
                !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : moveToolButtons_) {
            if (auto status = tree->setButtonAction(
                    button, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                        queueViewportToolMode(ViewportToolMode::Move);
                    }});
                !status) {
                return status;
            }
        }
        if (auto status = tree->setButtonAction(
                tilePaintToolButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueViewportToolMode(ViewportToolMode::TilePaint);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                tileEraseToolButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueViewportToolMode(ViewportToolMode::TileErase);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setPointerHitPolicy(viewportPreviewLayer_,
                                                    UI::UIPointerHitPolicy::Targetable);
            !status) {
            return status;
        }
        if (auto status = registerViewportPointerListeners(*tree); !status) {
            return status;
        }
        counters_.authoringActionsWired = EditorActionCount;
        counters_.editorLayoutRegions = EditorLayoutRegionCount;
        counters_.viewportLayoutReady = true;
        auto initialSelection = tree->treeViewSelection(hierarchyTree_);
        if (!initialSelection) {
            return Tina::Core::failure(std::move(initialSelection.error()));
        }
        selectionKey_ = initialSelection->key;
        if (auto status = rebuildAnimationAnimator(); !status) {
            return status;
        }
        if (auto status = validateRuntimePreview(); !status) {
            return status;
        }
        if (auto status = refreshAuthoringUi(*tree); !status) {
            return status;
        }
        counters_.finalSelectionKey = selectionKey_;
        counters_.finalSelectionIndex = initialSelection->logicalIndex;
        counters_.hierarchyLogicalItems = hierarchyItemCount(this);
        counters_.selectionVerified = true;

        uiRoot_ = std::move(*root);
        ++counters_.uiRootsCreated;
        assetRollback.release();
        return Tina::Core::success();
    }

    void onExit(Tina::GameStateExitContext&) noexcept override
    {
        viewportGizmo_ = {};
        pendingTileCellEdit_.reset();
        pendingTileLayerId_ = 0;
        for (auto& listener : viewportPointerListeners_) {
            listener.reset();
        }
        viewportNormalized_.reset();
        previewBindings_.clear();
        preview3DBindings_.clear();
        previewCamera3D_ = {};
        animationAnimator_.reset();
        releasePreviewAssetBindings();
        if (uiRoot_) {
            uiRoot_.reset();
            ++counters_.uiRootsReleased;
        }
        ++counters_.stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_.frameUpdates;
        if (options_.autoDemo) {
            const u64 first = (std::max)(u64{1}, options_.targetFrameCount / u64{3});
            const u64 second =
                (std::max)(first + u64{1}, options_.targetFrameCount - options_.targetFrameCount / u64{3});
            if (!queuedFirstSelection_ && counters_.frameUpdates >= first) {
                pendingSelectionIndex_ = 3;
                pendingViewportTokenColor_ = UI::rgb(0x1A3348);
                pendingViewportToolMode_ = ViewportToolMode::Move;
                queuedFirstSelection_ = true;
            } else if (queuedFirstSelection_ && !queuedSecondSelection_ &&
                       counters_.frameUpdates >= second) {
                pendingSelectionIndex_ = 6;
                pendingViewportTokenColor_ = UI::rgb(0x0C141E);
                queuedSecondSelection_ = true;
            }
            if (queuedFirstSelection_) {
                const auto queueAutoCommand = [&](EditorCommand command) noexcept {
                    if (!queueEditorCommand(command)) {
                        return false;
                    }
                    pendingAutoTransformInput_ = command == EditorCommand::ApplyTransform;
                    ++autoAuthoringStage_;
                    return true;
                };
                switch (autoAuthoringStage_) {
                case 0:
                    (void)queueAutoCommand(EditorCommand::MoveSelectedPositiveX);
                    break;
                case 1:
                    (void)queueAutoCommand(EditorCommand::ApplyTransform);
                    break;
                case 2:
                    if (viewportLogicalRect_.width > 0.0F && viewportLogicalRect_.height > 0.0F) {
                        autoGizmoStart_ = {
                            .x = viewportLogicalRect_.x + viewportLogicalRect_.width * 0.5F,
                            .y = viewportLogicalRect_.y + viewportLogicalRect_.height * 0.5F,
                        };
                        if (beginViewportGizmo(Tina::Platform::PrimaryPointerId,
                                               autoGizmoStart_)) {
                            ++autoAuthoringStage_;
                        }
                    }
                    break;
                case 3:
                    if (updateViewportGizmo(
                            Tina::Platform::PrimaryPointerId,
                            {
                                .x = autoGizmoStart_.x + viewportLogicalRect_.width / 16.0F,
                                .y = autoGizmoStart_.y + viewportLogicalRect_.height / 18.0F,
                            })) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 4:
                    if (requestViewportGizmoCommit(
                            Tina::Platform::PrimaryPointerId,
                            {
                                .x = autoGizmoStart_.x + viewportLogicalRect_.width / 8.0F,
                                .y = autoGizmoStart_.y + viewportLogicalRect_.height / 9.0F,
                            }) &&
                        !updateViewportGizmo(
                            Tina::Platform::PrimaryPointerId,
                            {
                                .x = autoGizmoStart_.x + viewportLogicalRect_.width / 4.0F,
                                .y = autoGizmoStart_.y + viewportLogicalRect_.height / 4.0F,
                            })) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 5:
                    (void)queueAutoCommand(EditorCommand::Undo);
                    break;
                case 6:
                    (void)queueAutoCommand(EditorCommand::Redo);
                    break;
                case 7:
                    if (!activeWorkspaceSession().hasDocumentPath()) {
                        ++autoAuthoringStage_;
                    } else {
                        (void)queueAutoCommand(EditorCommand::Save);
                    }
                    break;
                case 8:
                    (void)queueAutoCommand(options_.initialWorkspace == WorkspaceMode::World2D
                                               ? EditorCommand::SwitchToWorld3D
                                               : EditorCommand::SwitchToWorld2D);
                    break;
                case 9:
                    if (options_.initialWorkspace == WorkspaceMode::World3D) {
                        ++autoAuthoringStage_;
                    } else {
                        (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                    }
                    break;
                case 10:
                    if (workspaceMode_ == WorkspaceMode::World2D) {
                        ++autoAuthoringStage_;
                    } else {
                        (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                    }
                    break;
                case 11:
                    (void)queueAutoCommand(EditorCommand::AnimationNextFrame);
                    break;
                case 12:
                    (void)queueAutoCommand(EditorCommand::AnimationCycleMode);
                    break;
                case 13:
                    (void)queueAutoCommand(EditorCommand::AnimationUndo);
                    break;
                case 14:
                    (void)queueAutoCommand(EditorCommand::AnimationRedo);
                    break;
                case 15:
                    (void)queueAutoCommand(EditorCommand::AnimationCookPreview);
                    break;
                case 16:
                    if (options_.initialWorkspace == WorkspaceMode::World2D) {
                        ++autoAuthoringStage_;
                    } else {
                        (void)queueAutoCommand(EditorCommand::SwitchToWorld3D);
                    }
                    break;
                default:
                    break;
                }
            }
        }
        if (workspaceMode_ == WorkspaceMode::World2D && animationPlaying_ &&
            animationAnimator_.has_value()) {
            auto update = animationAnimator_->update(context.frameTiming().updateDelta);
            if (!update) {
                return Tina::Core::failure(std::move(update.error()));
            }
            if (update->currentFrameChanged) {
                if (auto status = applyAnimationPreviewFrame(
                        static_cast<u32>(update->currentFrameIndex)); !status) {
                    return status;
                }
                pendingAnimationTimelineRefresh_ = true;
            }
            if (animationAnimator_->isCompleted()) {
                animationPlaying_ = false;
                pendingAnimationTimelineRefresh_ = true;
            }
        }
        if (counters_.frameUpdates >= options_.targetFrameCount) {
            context.requestExitAfterFrame();
        }
        if (options_.frameDelayMilliseconds != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{options_.frameDelayMilliseconds});
        }
        return Tina::Core::success();
    }

    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override
    {
        ++counters_.renderExtractions;
        counters_.gpuViewportSprites = 0;
        counters_.gpuViewportMeshes = 0;
        if (!viewportNormalized_.has_value()) {
            return Tina::Core::success();
        }
        if (!previewWorld_.has_value()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport has no canonical preview World");
        }
        if (workspaceMode_ == WorkspaceMode::World3D) {
            return extractWorld3DViewport(context);
        }

        const Tina::Scene::World2DEntityBinding* cameraBinding = findPreviewBinding(CameraKey);
        if (cameraBinding == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport is missing the Camera binding");
        }
        const Tina::Scene::Camera2D* authoredCamera = previewWorld_->camera2D(cameraBinding->entity);
        if (authoredCamera == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport is missing the Camera2D component");
        }
        Tina::Scene::Camera2D camera = *authoredCamera;
        camera.projection = Tina::Render::FixedWorldHeight2D{.heightMeters = PreviewWorldHeight};
        camera.normalizedViewport = *viewportNormalized_;
        camera.pixelSnap = Tina::Render::RenderPixelSnapPolicy::Disabled;
        if (auto status = previewWorld_->setCamera2D(cameraBinding->entity, camera); !status) {
            return status;
        }
        if (auto status = Tina::Scene::extractRenderSceneFromWorld(
                *previewWorld_, context.renderSceneWriter(), context.frameResourceSink(),
                Tina::Scene::ExtractRenderSceneParams{
                    .surfaceViewport = {
                        .pixelWidth = surfacePixelWidth_,
                        .pixelHeight = surfacePixelHeight_,
                    },
                    .spriteBindingResolver = {
                        .userData = const_cast<EditorWorkspaceState*>(this),
                        .resolve = &EditorWorkspaceState::resolvePreviewSprite,
                    },
                    .normalTextureBindingResolver = {
                        .userData = const_cast<EditorWorkspaceState*>(this),
                        .resolve = &EditorWorkspaceState::resolvePreviewTexture,
                    },
                });
            !status) {
            return status;
        }
        u64 emittedTileSprites = 0;
        if (previewTileMap_.has_value() && !previewTileMapLayerIds_.empty() &&
            previewTilesetAsset_) {
            const Tina::Scene::WorldTransform* cameraTransform =
                previewWorld_->worldTransform(cameraBinding->entity);
            if (cameraTransform == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "editor TileMap preview is missing the Camera2D world transform");
            }
            constexpr u64 TileEntityKeyBase = 100'000U;
            constexpr u64 TileLayerEntityKeyStride =
                static_cast<u64>(Tina::AssetFormat::TileMapWire::MaxDimension) *
                    Tina::AssetFormat::TileMapWire::MaxDimension +
                1U;
            const Tina::Asset::TileChunkCameraQuery cameraQuery{
                .centerX = cameraTransform->position.x,
                .centerY = cameraTransform->position.y,
                .halfWidth = PreviewWorldWidth * 0.5F,
                .halfHeight = PreviewWorldHeight * 0.5F,
            };
            std::pmr::vector<Tina::Render::RenderSprite2DInput> tileSprites{
                &assetResources_.memory};
            for (Tina::Core::usize layerIndex = 0;
                 layerIndex < previewTileMapLayerIds_.size(); ++layerIndex) {
                auto emitted = Tina::Asset::emitVisibleTileMapSprites(
                    *previewTileMap_, previewTileMapLayerIds_[layerIndex], cameraQuery,
                    Tina::Asset::TileChunkSpriteEmitParams{
                        .tileset = previewTilesetAsset_,
                        .bindingResolver = {
                            .userData = const_cast<EditorWorkspaceState*>(this),
                            .resolve = &EditorWorkspaceState::resolvePreviewTileset,
                        },
                        .stableEntityKeyBase =
                            TileEntityKeyBase + layerIndex * TileLayerEntityKeyStride,
                        .sortingLayer = static_cast<Tina::Core::i16>(
                            static_cast<Tina::Core::i32>(layerIndex) -
                            static_cast<Tina::Core::i32>(previewTileMapLayerIds_.size())),
                    },
                    context.frameResourceSink(), tileSprites);
                if (!emitted) {
                    return Tina::Core::failure(std::move(emitted.error()));
                }
                for (const auto& sprite : tileSprites) {
                    if (auto status = context.renderSceneWriter().addSprite2D(sprite); !status) {
                        return status;
                    }
                }
                emittedTileSprites += *emitted;
            }
        }
        counters_.tileMapEmittedSprites = emittedTileSprites;
        counters_.gpuViewportSprites = previewResolvedSpriteCount_ + emittedTileSprites;
        counters_.gpuViewportDocumentRevision = previewRevision_;
        return Tina::Core::success();
    }

    Tina::Core::Status updateUI(Tina::UIUpdateContext& context) override
    {
        if (!context.hasPrimaryWindowUI() || !uiRoot_) {
            return Tina::Core::success();
        }
        auto tree = context.primaryWindowUITreeUpdater(uiRoot_);
        if (!tree) {
            return Tina::Core::failure(std::move(tree.error()));
        }
        if (auto status = updateGpuViewport(*tree); !status) {
            return status;
        }
        if (pendingAnimationTimelineRefresh_) {
            pendingAnimationTimelineRefresh_ = false;
            if (auto status = refreshAnimationTimelineUi(*tree); !status) {
                return status;
            }
        }
        if (auto status = processPendingAnimationFrameSelection(*tree); !status) {
            return status;
        }
        if (pendingViewportTokenColor_.has_value()) {
            const UI::UIStraightSrgba8Color color = *pendingViewportTokenColor_;
            pendingViewportTokenColor_.reset();
            if (auto status = tree->setStyleColorToken(viewportToken_, color); !status) {
                return status;
            }
            ++counters_.styleTokenUpdates;
        }
        if (pendingSelectionIndex_.has_value()) {
            const u64 index = *pendingSelectionIndex_;
            pendingSelectionIndex_.reset();
            if (auto status = tree->setTreeViewSelectedIndex(hierarchyTree_, index); !status) {
                return status;
            }
        }
        auto selection = tree->treeViewSelection(hierarchyTree_);
        if (!selection) {
            return Tina::Core::failure(std::move(selection.error()));
        }
        if (selection->key != selectionKey_) {
            selectionKey_ = selection->key;
            ++counters_.hierarchySelectionChanges;
            if (auto status = refreshAuthoringUi(*tree); !status) {
                return status;
            }
        }
        if (pendingViewportToolMode_.has_value()) {
            viewportToolMode_ = *pendingViewportToolMode_;
            pendingViewportToolMode_.reset();
            if (viewportToolMode_ != ViewportToolMode::Move && viewportGizmo_.captured) {
                viewportGizmo_.cancelRequested = true;
            }
            if (auto status = refreshViewportToolUi(*tree); !status) {
                return status;
            }
        }
        if (auto status = processViewportGizmo(*tree); !status) {
            return status;
        }
        if (auto status = processPendingTileBrush(*tree); !status) {
            return status;
        }
        if (pendingAutoTransformInput_ && pendingEditorCommand_ == EditorCommand::ApplyTransform) {
            pendingAutoTransformInput_ = false;
            if (auto status = tree->setText(inspectorPositionX_, "2.5"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorPositionY_, "-1.25"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorRotationZ_, "30.0"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorScaleX_, "1.25"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorScaleY_, "0.75"); !status) {
                return status;
            }
            if (workspaceMode_ == WorkspaceMode::World3D) {
                if (auto status = tree->setText(inspectorPositionZ_, "1.5"); !status) {
                    return status;
                }
                if (auto status = tree->setText(inspectorRotationX_, "15.0"); !status) {
                    return status;
                }
                if (auto status = tree->setText(inspectorRotationY_, "25.0"); !status) {
                    return status;
                }
                if (auto status = tree->setText(inspectorScaleZ_, "1.5"); !status) {
                    return status;
                }
            }
        }
        if (pendingEditorCommand_.has_value()) {
            if (auto status = executeEditorCommand(*tree); !status) {
                return status;
            }
        }
        counters_.finalSelectionKey = selection->key;
        counters_.finalSelectionIndex = selection->logicalIndex;
        UI::UITreeViewItemDescriptor descriptor{};
        counters_.selectionVerified =
            resolveHierarchyItem(this, selection->logicalIndex, descriptor) &&
            descriptor.key == selection->key;
        auto metrics = tree->treeViewMetrics(hierarchyTree_);
        if (!metrics) {
            return Tina::Core::failure(std::move(metrics.error()));
        }
        counters_.hierarchyLogicalItems = metrics->logicalItemCount;
        return Tina::Core::success();
    }

  private:
    bool queueEditorCommand(EditorCommand command) noexcept
    {
        if (pendingEditorCommand_.has_value()) {
            return false;
        }
        pendingEditorCommand_ = command;
        return true;
    }

    void queueViewportToolMode(ViewportToolMode mode) noexcept
    {
        pendingViewportToolMode_ = mode;
    }

    [[nodiscard]] bool tileMapEditingContext() const noexcept
    {
        return workspaceMode_ == WorkspaceMode::World2D && selectionKey_ == TileMapKey;
    }

    [[nodiscard]] static Tina::Scene::SpriteAnimationPlaybackMode
    sceneAnimationMode(Tina::AssetFormat::SpriteAnimationPlaybackMode mode) noexcept
    {
        switch (mode) {
        case Tina::AssetFormat::SpriteAnimationPlaybackMode::Once:
            return Tina::Scene::SpriteAnimationPlaybackMode::Once;
        case Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong:
            return Tina::Scene::SpriteAnimationPlaybackMode::PingPong;
        case Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop:
        default:
            return Tina::Scene::SpriteAnimationPlaybackMode::Loop;
        }
    }

    [[nodiscard]] static std::string_view
    animationModeLabel(Tina::AssetFormat::SpriteAnimationPlaybackMode mode) noexcept
    {
        switch (mode) {
        case Tina::AssetFormat::SpriteAnimationPlaybackMode::Once:
            return "Once";
        case Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong:
            return "PingPong";
        case Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop:
        default:
            return "Loop";
        }
    }

    [[nodiscard]] Tina::Core::Status applyAnimationPreviewFrame(u32 frameIndex)
    {
        const auto frame = spriteAnimationDocument_.frameAt(frameIndex);
        if (!frame) {
            return Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                       "Animation timeline frame does not exist");
        }
        const Tina::Asset::AssetHandle sprite = loadedAsset(
            frame->spriteId, Tina::AssetFormat::AssetKind::Sprite);
        if (!sprite || !containsHandle(boundSpriteAssets_, sprite)) {
            animationPreviewAvailable_ = false;
            return Tina::Core::success();
        }
        animationPreviewAvailable_ = true;
        animationSelectedFrameIndex_ = frameIndex;
        counters_.animationPreviewFrameIndex = frameIndex;
        if (workspaceMode_ != WorkspaceMode::World2D || !previewWorld_.has_value()) {
            return Tina::Core::success();
        }
        const Tina::Scene::World2DEntityBinding* player = findPreviewBinding(PlayerKey);
        if (player == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Animation preview is missing the Player binding");
        }
        const Tina::Scene::SpriteRenderer2D* current =
            previewWorld_->spriteRenderer2D(player->entity);
        if (current == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Animation preview is missing the Player sprite");
        }
        Tina::Scene::SpriteRenderer2D updated = *current;
        updated.sprite = sprite;
        return previewWorld_->setSpriteRenderer2D(player->entity, updated);
    }

    [[nodiscard]] Tina::Core::Status rebuildAnimationAnimator()
    {
        std::vector<Tina::Scene::SpriteAnimationFrame2D> resolvedFrames;
        try {
            resolvedFrames.reserve(spriteAnimationDocument_.frameCount());
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Animation preview frame allocation failed");
        }
        for (u32 frameIndex = 0; frameIndex < spriteAnimationDocument_.frameCount(); ++frameIndex) {
            const auto frame = spriteAnimationDocument_.frameAt(frameIndex);
            if (!frame) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "Animation document frame disappeared during preview rebuild");
            }
            const Tina::Asset::AssetHandle sprite = loadedAsset(
                frame->spriteId, Tina::AssetFormat::AssetKind::Sprite);
            if (!sprite || !containsHandle(boundSpriteAssets_, sprite)) {
                animationAnimator_.reset();
                animationPlaying_ = false;
                animationPreviewAvailable_ = false;
                return Tina::Core::success();
            }
            resolvedFrames.push_back(Tina::Scene::SpriteAnimationFrame2D{
                .sprite = Tina::Scene::SpriteRenderer2D{.sprite = sprite},
                .duration = Tina::Core::Duration{frame->durationSeconds},
            });
        }
        auto animator = Tina::Scene::SpriteAnimator2D::Create(
            Tina::Scene::SpriteAnimationClip2D{
                .frames = resolvedFrames,
                .playbackMode = sceneAnimationMode(spriteAnimationDocument_.playbackMode()),
            },
            assetResources_.memory);
        if (!animator) {
            return Tina::Core::failure(std::move(animator.error()));
        }
        animator->pause();
        animationAnimator_.reset();
        animationAnimator_.emplace(std::move(*animator));
        animationPlaying_ = false;
        animationPreviewAvailable_ = true;
        animationSelectedFrameIndex_ = (std::min)(
            animationSelectedFrameIndex_,
            static_cast<u32>(spriteAnimationDocument_.frameCount() - 1U));
        return applyAnimationPreviewFrame(animationSelectedFrameIndex_);
    }

    [[nodiscard]] Tina::Core::Status processPendingAnimationFrameSelection(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (!pendingAnimationFrameSelection_.has_value()) {
            return Tina::Core::success();
        }
        const u32 slot = *pendingAnimationFrameSelection_;
        pendingAnimationFrameSelection_.reset();
        if (workspaceMode_ != WorkspaceMode::World2D) {
            return refreshAnimationTimelineUi(tree);
        }
        const u32 frameIndex = animationVisibleFrameStart_ + slot;
        if (frameIndex >= spriteAnimationDocument_.frameCount()) {
            return Tina::Core::success();
        }
        animationPlaying_ = false;
        if (animationAnimator_.has_value()) {
            animationAnimator_->pause();
        }
        if (auto status = applyAnimationPreviewFrame(frameIndex); !status) {
            return status;
        }
        authoringFeedback_ = "Animation playhead moved to frame " +
                             std::to_string(frameIndex + 1U);
        return refreshAuthoringUi(tree);
    }

    [[nodiscard]] Tina::Core::Status refreshAnimationTimelineUi(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const bool editable = workspaceMode_ == WorkspaceMode::World2D;
        const u32 frameCount = static_cast<u32>(spriteAnimationDocument_.frameCount());
        animationSelectedFrameIndex_ = (std::min)(animationSelectedFrameIndex_, frameCount - 1U);
        counters_.animationDocumentRevision = spriteAnimationDocument_.revision();
        counters_.animationFrameCount = frameCount;
        counters_.animationPreviewFrameIndex = animationSelectedFrameIndex_;

        std::string statusText;
        if (!editable) {
            statusText = "Switch to 2D to edit this clip";
        } else {
            statusText = std::to_string(frameCount);
            statusText += " frames | ";
            statusText += std::to_string(static_cast<u64>(std::llround(
                spriteAnimationDocument_.totalDurationSeconds() * 1000.0)));
            statusText += " ms | Rev ";
            statusText += std::to_string(spriteAnimationDocument_.revision());
            statusText += " | Cook ";
            statusText += std::to_string(counters_.animationCookPreviewBytes);
            statusText += " B";
            if (!animationPreviewAvailable_) {
                statusText += " | Sprite unresolved";
            }
        }
        if (auto status = tree.setText(animationStatus_, statusText); !status) {
            return status;
        }
        std::string modeText = "Mode: ";
        modeText += animationModeLabel(spriteAnimationDocument_.playbackMode());
        if (auto status = tree.setText(animationModeButton_, modeText); !status) {
            return status;
        }
        if (auto status = tree.setText(animationPlayButton_, animationPlaying_ ? "Pause" : "Play");
            !status) {
            return status;
        }

        const auto selectedFrame = spriteAnimationDocument_.frameAt(animationSelectedFrameIndex_);
        if (!selectedFrame) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Animation timeline selected frame is invalid");
        }
        const auto spriteText = selectedFrame->spriteId.canonicalText();
        std::string selectionText = "Frame ";
        selectionText += std::to_string(animationSelectedFrameIndex_ + 1U);
        selectionText += " | Sprite ";
        selectionText.append(spriteText.data(),
                             (std::min)(Tina::Core::usize{8}, spriteText.size()));
        selectionText += " | ";
        selectionText += std::to_string(
            static_cast<u32>(std::lround(selectedFrame->durationSeconds * 1000.0F)));
        selectionText += " ms";
        if (auto status = tree.setText(animationSelection_, selectionText); !status) {
            return status;
        }

        animationVisibleFrameStart_ = 0;
        if (frameCount > AnimationVisibleFrameSlots &&
            animationSelectedFrameIndex_ >= AnimationVisibleFrameSlots) {
            animationVisibleFrameStart_ = (std::min)(
                animationSelectedFrameIndex_ - AnimationVisibleFrameSlots + 1U,
                frameCount - AnimationVisibleFrameSlots);
        }
        for (u32 slot = 0; slot < animationFrameButtons_.size(); ++slot) {
            const u32 frameIndex = animationVisibleFrameStart_ + slot;
            const bool materialized = frameIndex < frameCount;
            std::string label = "--";
            if (materialized) {
                const auto frame = spriteAnimationDocument_.frameAt(frameIndex);
                label = frameIndex == animationSelectedFrameIndex_ ? ">" : "";
                label += std::to_string(frameIndex + 1U);
                label += " ";
                label += std::to_string(
                    static_cast<u32>(std::lround(frame->durationSeconds * 1000.0F)));
                label += "ms";
            }
            if (auto status = tree.setText(animationFrameButtons_[slot], label); !status) {
                return status;
            }
            if (auto status = tree.setEnabled(animationFrameButtons_[slot], editable && materialized);
                !status) {
                return status;
            }
        }

        if (auto status = tree.setEnabled(animationPlayButton_, editable && animationPreviewAvailable_);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(animationPreviousButton_,
                                          editable && animationSelectedFrameIndex_ > 0U);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(animationNextButton_,
                                          editable && animationSelectedFrameIndex_ + 1U < frameCount);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(animationAddButton_,
                                          editable && frameCount < spriteAnimationDocument_.config().frameCapacity);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(animationDuplicateButton_, editable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(animationDeleteButton_, editable && frameCount > 1U); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(animationMoveLeftButton_,
                                          editable && animationSelectedFrameIndex_ > 0U);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(animationMoveRightButton_,
                                          editable && animationSelectedFrameIndex_ + 1U < frameCount);
            !status) {
            return status;
        }
        const std::array alwaysEditableButtons{
            animationCycleSpriteButton_, animationDurationDecreaseButton_,
            animationDurationIncreaseButton_,
            animationModeButton_, animationCookButton_,
        };
        for (const UI::UINodeId button : alwaysEditableButtons) {
            if (auto status = tree.setEnabled(button, editable); !status) {
                return status;
            }
        }
        if (auto status = tree.setEnabled(animationUndoButton_,
                                          editable && spriteAnimationDocument_.canUndo());
            !status) {
            return status;
        }
        return tree.setEnabled(animationRedoButton_,
                               editable && spriteAnimationDocument_.canRedo());
    }

    [[nodiscard]] Tina::Core::Status
    registerViewportPointerListeners(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const auto registerListener = [&](u32 index, UI::UIRoutedPointerEventKind kind,
                                          UI::UIRoutedPointerCallback callback) -> Tina::Core::Status {
            auto listener = tree.addRoutedPointerListener(
                {
                    .node = viewportPreviewLayer_,
                    .kind = kind,
                    .phases = UI::UIEventPhaseMask::Target,
                },
                std::move(callback));
            if (!listener) {
                return Tina::Core::failure(std::move(listener.error()));
            }
            viewportPointerListeners_[index] = std::move(*listener);
            return Tina::Core::success();
        };

        if (auto status = registerListener(
                0, UI::UIRoutedPointerEventKind::ButtonDown,
                UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                    handleViewportPointerDown(event);
                }});
            !status) {
            return status;
        }
        if (auto status = registerListener(
                1, UI::UIRoutedPointerEventKind::Move,
                UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                    handleViewportPointerMove(event);
                }});
            !status) {
            return status;
        }
        if (auto status = registerListener(
                2, UI::UIRoutedPointerEventKind::ButtonUp,
                UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                    handleViewportPointerUp(event);
                }});
            !status) {
            return status;
        }
        return registerListener(
            3, UI::UIRoutedPointerEventKind::PointerCancel,
            UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                handleViewportPointerCancel(event);
            }});
    }

    [[nodiscard]] bool beginViewportGizmo(Tina::Platform::PointerId pointer,
                                          UI::UILogicalPoint position) noexcept
    {
        if (pointer != Tina::Platform::PrimaryPointerId ||
            viewportToolMode_ != ViewportToolMode::Move || viewportGizmo_.captured ||
            tileMapEditingContext() ||
            !std::isfinite(viewportLogicalRect_.width) ||
            !std::isfinite(viewportLogicalRect_.height) ||
            viewportLogicalRect_.width <= 0.0F || viewportLogicalRect_.height <= 0.0F) {
            return false;
        }
        const u32 stableEntityId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableEntityId == 0U) {
            return false;
        }
        viewportGizmo_ = ViewportGizmoTransaction{
            .workspace = workspaceMode_,
            .pointer = pointer,
            .stableEntityId = stableEntityId,
            .baselineRevision = activeDocumentRevision(),
            .start = position,
            .current = position,
            .viewportWidth = viewportLogicalRect_.width,
            .viewportHeight = viewportLogicalRect_.height,
            .captured = true,
        };
        return true;
    }

    [[nodiscard]] bool updateViewportGizmo(Tina::Platform::PointerId pointer,
                                           UI::UILogicalPoint position) noexcept
    {
        if (!viewportGizmo_.captured || pointer != viewportGizmo_.pointer ||
            viewportGizmo_.commitRequested || viewportGizmo_.cancelRequested) {
            return false;
        }
        viewportGizmo_.current = position;
        viewportGizmo_.previewDirty = true;
        return true;
    }

    [[nodiscard]] bool requestViewportGizmoCommit(Tina::Platform::PointerId pointer,
                                                  UI::UILogicalPoint position) noexcept
    {
        if (!updateViewportGizmo(pointer, position)) {
            return false;
        }
        viewportGizmo_.commitRequested = true;
        return true;
    }

    [[nodiscard]] bool queueViewportTileBrush(UI::UILogicalPoint position) noexcept
    {
        if (!tileMapEditingContext() ||
            (viewportToolMode_ != ViewportToolMode::TilePaint &&
             viewportToolMode_ != ViewportToolMode::TileErase) ||
            pendingTileCellEdit_.has_value() || !previewWorld_.has_value() ||
            tileMapWidthCells_ == 0U || tileMapHeightCells_ == 0U ||
            viewportLogicalRect_.width <= 0.0F || viewportLogicalRect_.height <= 0.0F) {
            return false;
        }
        const Tina::Scene::World2DEntityBinding* cameraBinding = findPreviewBinding(CameraKey);
        const Tina::Scene::WorldTransform* cameraTransform =
            cameraBinding != nullptr ? previewWorld_->worldTransform(cameraBinding->entity) : nullptr;
        if (cameraTransform == nullptr) {
            return false;
        }
        const float normalizedX =
            (position.x - viewportLogicalRect_.x) / viewportLogicalRect_.width;
        const float normalizedY =
            (position.y - viewportLogicalRect_.y) / viewportLogicalRect_.height;
        if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
            normalizedX < 0.0F || normalizedX >= 1.0F ||
            normalizedY < 0.0F || normalizedY >= 1.0F) {
            return false;
        }
        const float worldHeight = PreviewWorldHeight;
        const float worldWidth = worldHeight * viewportLogicalRect_.width /
                                 viewportLogicalRect_.height;
        const float worldX = cameraTransform->position.x +
                             (normalizedX - 0.5F) * worldWidth;
        const float worldY = cameraTransform->position.y +
                             (0.5F - normalizedY) * worldHeight;
        if (!std::isfinite(worldX) || !std::isfinite(worldY) ||
            worldX < 0.0F || worldY < 0.0F) {
            return false;
        }
        const u32 cellX = static_cast<u32>(std::floor(worldX));
        const u32 cellY = static_cast<u32>(std::floor(worldY));
        if (cellX >= tileMapWidthCells_ || cellY >= tileMapHeightCells_) {
            return false;
        }
        pendingTileCellEdit_ = Tina::Editor::TileMapAuthoringCellEdit{
            .x = cellX,
            .y = cellY,
            .localTileId = viewportToolMode_ == ViewportToolMode::TileErase
                               ? Tina::Core::u16{0}
                               : selectedTileId_,
        };
        pendingTileLayerId_ = activeTileMapLayerId_;
        return true;
    }

    void handleViewportPointerDown(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        if (input.button != Tina::Platform::PointerButton::Primary) {
            return;
        }
        if (queueViewportTileBrush(input.position)) {
            (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
            event.consumeInputTransition();
            event.preventDefaultAction();
            return;
        }
        const bool began = beginViewportGizmo(input.pointer, input.position);
        if (!began && !viewportGizmo_.captured) {
            return;
        }
        if (began) {
            event.capturePointer();
        }
        (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void handleViewportPointerMove(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        if (!updateViewportGizmo(input.pointer, input.position)) {
            return;
        }
        (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void handleViewportPointerUp(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        if (input.button != Tina::Platform::PointerButton::Primary ||
            !requestViewportGizmoCommit(input.pointer, input.position)) {
            return;
        }
        event.releasePointerCapture();
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void handleViewportPointerCancel(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        if (!viewportGizmo_.captured || input.pointer != viewportGizmo_.pointer) {
            return;
        }
        viewportGizmo_.cancelRequested = true;
        event.releasePointerCapture();
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    [[nodiscard]] Tina::Scene::EntityId findPreviewEntity(u32 stableEntityId) const noexcept
    {
        if (workspaceMode_ == WorkspaceMode::World3D) {
            const auto binding = std::find_if(
                preview3DBindings_.begin(), preview3DBindings_.end(),
                [stableEntityId](const World3DPreviewBinding& candidate) {
                    return candidate.stableNodeId == stableEntityId;
                });
            return binding == preview3DBindings_.end() ? Tina::Scene::EntityId{}
                                                       : binding->entity;
        }
        const auto binding = std::find_if(
            previewBindings_.begin(), previewBindings_.end(),
            [stableEntityId](const Tina::Scene::World2DEntityBinding& candidate) {
                return candidate.stableEntityId == stableEntityId;
            });
        return binding == previewBindings_.end() ? Tina::Scene::EntityId{}
                                                 : binding->entity;
    }

    [[nodiscard]] bool viewportGizmoContextMatches() const noexcept
    {
        return viewportGizmo_.workspace == workspaceMode_ &&
               viewportGizmo_.stableEntityId == stableEntityIdForHierarchyItem(selectionKey_) &&
               viewportGizmo_.baselineRevision == activeDocumentRevision() &&
               viewportGizmo_.baselineRevision == previewRevision_ &&
               previewWorld_.has_value();
    }

    [[nodiscard]] Tina::Core::Status
    finishViewportGizmoWithoutCommit(Tina::PrimaryWindowUITreeUpdater& tree, bool rejected,
                                     std::string_view feedback)
    {
        const bool restorePreview = viewportGizmo_.previewPublished;
        viewportGizmo_ = {};
        if (restorePreview) {
            if (auto status = validateRuntimePreview(); !status) {
                return status;
            }
        }
        if (rejected) {
            ++counters_.viewportGizmoRejects;
        } else {
            ++counters_.viewportGizmoCancels;
        }
        authoringFeedback_.assign(feedback);
        return refreshAuthoringUi(tree);
    }

    [[nodiscard]] Tina::Core::Status
    commitViewportGizmoTransform(const ViewportGizmoTransaction& transaction)
    {
        if (transaction.workspace == WorkspaceMode::World3D) {
            std::vector<Tina::AssetFormat::PrefabNodeView> views;
            auto prefab = document3D_.parseCurrentPrefab(views);
            if (!prefab) {
                return Tina::Core::failure(std::move(prefab.error()));
            }
            const auto node = std::find_if(
                views.begin(), views.end(), [&](const Tina::AssetFormat::PrefabNodeView& candidate) {
                    return candidate.stableNodeId == transaction.stableEntityId;
                });
            if (node == views.end()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                           "viewport gizmo target is absent from the World3D document");
            }
            const Tina::AssetFormat::PrefabNodeDesc edited{
                .stableNodeId = node->stableNodeId,
                .parentIndex = node->parentIndex,
                .positionX = transaction.baselineTransform.position.x + transaction.worldDeltaX,
                .positionY = transaction.baselineTransform.position.y,
                .positionZ = transaction.baselineTransform.position.z + transaction.worldDeltaZ,
                .rotationX = node->rotationX,
                .rotationY = node->rotationY,
                .rotationZ = node->rotationZ,
                .rotationW = node->rotationW,
                .scaleX = node->scaleX,
                .scaleY = node->scaleY,
                .scaleZ = node->scaleZ,
                .meshId = node->meshId,
                .materialId = node->materialId,
                .visible = node->visible,
            };
            return document3D_.upsertNode(edited);
        }

        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto entity = std::find_if(
            storage.begin(), storage.end(), [&](const Tina::AssetFormat::World2DEntityDesc& candidate) {
                return candidate.stableEntityId == transaction.stableEntityId;
            });
        if (entity == storage.end()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "viewport gizmo target is absent from the World2D document");
        }
        auto edited = *entity;
        edited.positionX = transaction.baselineTransform.position.x + transaction.worldDeltaX;
        edited.positionY = transaction.baselineTransform.position.y + transaction.worldDeltaY;
        return document_.upsertEntity(edited);
    }

    [[nodiscard]] Tina::Core::Status
    processViewportGizmo(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (!viewportGizmo_.captured) {
            return Tina::Core::success();
        }
        if (viewportGizmo_.cancelRequested) {
            return finishViewportGizmoWithoutCommit(tree, false,
                                                    "Viewport move cancelled; document unchanged");
        }
        if (!viewportGizmoContextMatches() || pendingEditorCommand_.has_value()) {
            return finishViewportGizmoWithoutCommit(
                tree, true, "Viewport move rejected after selection, workspace, or revision changed");
        }

        const Tina::Scene::EntityId previewEntity =
            findPreviewEntity(viewportGizmo_.stableEntityId);
        if (!previewEntity.hasValue()) {
            return finishViewportGizmoWithoutCommit(tree, true,
                                                    "Viewport move rejected: preview target is unavailable");
        }
        if (!viewportGizmo_.baselineReady) {
            const Tina::Scene::LocalTransform* local = previewWorld_->localTransform(previewEntity);
            if (local == nullptr) {
                return finishViewportGizmoWithoutCommit(
                    tree, true, "Viewport move rejected: preview transform is unavailable");
            }
            viewportGizmo_.baselineTransform = *local;
            viewportGizmo_.baselineReady = true;
            ++counters_.viewportGizmoBegins;
        }

        if (viewportGizmo_.previewDirty) {
            const float deltaX = viewportGizmo_.current.x - viewportGizmo_.start.x;
            const float deltaY = viewportGizmo_.current.y - viewportGizmo_.start.y;
            viewportGizmo_.worldDeltaX = deltaX / viewportGizmo_.viewportWidth * PreviewWorldWidth;
            viewportGizmo_.worldDeltaY = workspaceMode_ == WorkspaceMode::World2D
                                             ? -deltaY / viewportGizmo_.viewportHeight * PreviewWorldHeight
                                             : 0.0F;
            viewportGizmo_.worldDeltaZ = workspaceMode_ == WorkspaceMode::World3D
                                             ? deltaY / viewportGizmo_.viewportHeight * PreviewWorldHeight
                                             : 0.0F;
            if (!std::isfinite(viewportGizmo_.worldDeltaX) ||
                !std::isfinite(viewportGizmo_.worldDeltaY) ||
                !std::isfinite(viewportGizmo_.worldDeltaZ)) {
                return finishViewportGizmoWithoutCommit(
                    tree, true, "Viewport move rejected: pointer delta is not finite");
            }

            viewportGizmo_.previewDirty = false;
            if (!viewportGizmo_.hasPublishedPoint ||
                viewportGizmo_.current != viewportGizmo_.lastPublishedPoint) {
                Tina::Scene::LocalTransform preview = viewportGizmo_.baselineTransform;
                preview.position.x += viewportGizmo_.worldDeltaX;
                preview.position.y += viewportGizmo_.worldDeltaY;
                preview.position.z += viewportGizmo_.worldDeltaZ;
                if (auto status = previewWorld_->setLocalTransform(previewEntity, preview); !status) {
                    return status;
                }
                if (auto status = previewWorld_->updateWorldTransforms(); !status) {
                    return status;
                }
                viewportGizmo_.previewPublished = true;
                viewportGizmo_.hasPublishedPoint = true;
                viewportGizmo_.lastPublishedPoint = viewportGizmo_.current;
                ++counters_.viewportGizmoPreviews;
                counters_.viewportGizmoWorldDeltaX = viewportGizmo_.worldDeltaX;
                counters_.viewportGizmoWorldDeltaY = viewportGizmo_.worldDeltaY;
                counters_.viewportGizmoWorldDeltaZ = viewportGizmo_.worldDeltaZ;
                authoringFeedback_ = workspaceMode_ == WorkspaceMode::World2D
                                         ? "Move preview on the World2D XY plane"
                                         : "Move preview on the World3D XZ ground plane";
                if (auto status = tree.setText(authoringHint_, authoringFeedback_); !status) {
                    return status;
                }
            }
        }

        if (!viewportGizmo_.commitRequested) {
            return Tina::Core::success();
        }
        constexpr float MinimumWorldDelta = 1.0e-6F;
        if (std::abs(viewportGizmo_.worldDeltaX) <= MinimumWorldDelta &&
            std::abs(viewportGizmo_.worldDeltaY) <= MinimumWorldDelta &&
            std::abs(viewportGizmo_.worldDeltaZ) <= MinimumWorldDelta) {
            return finishViewportGizmoWithoutCommit(tree, false,
                                                    "Viewport move ended without a document change");
        }
        if (!viewportGizmoContextMatches()) {
            return finishViewportGizmoWithoutCommit(
                tree, true, "Viewport move rejected before commit because its baseline changed");
        }

        if (auto status = commitViewportGizmoTransform(viewportGizmo_); !status) {
            if (auto restoreStatus = validateRuntimePreview(); !restoreStatus) {
                return restoreStatus;
            }
            viewportGizmo_ = {};
            return status;
        }
        const u64 committedRevision = activeDocumentRevision();
        if (committedRevision == viewportGizmo_.baselineRevision) {
            return finishViewportGizmoWithoutCommit(
                tree, false, "Viewport move ended without a document change");
        }
        if (committedRevision < viewportGizmo_.baselineRevision ||
            committedRevision - viewportGizmo_.baselineRevision != 1U) {
            viewportGizmo_ = {};
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "viewport gizmo commit did not publish exactly one document revision");
        }
        viewportGizmo_ = {};
        ++counters_.viewportGizmoCommits;
        ++counters_.authoringEdits;
        if (auto status = validateRuntimePreview(); !status) {
            return status;
        }
        authoringFeedback_ = "Viewport move committed as one document revision";
        return refreshAuthoringUi(tree);
    }

    [[nodiscard]] Tina::Core::Status
    processPendingTileBrush(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (!pendingTileCellEdit_.has_value()) {
            return Tina::Core::success();
        }
        const Tina::Editor::TileMapAuthoringCellEdit edit = *pendingTileCellEdit_;
        const auto layerId = pendingTileLayerId_;
        pendingTileCellEdit_.reset();
        pendingTileLayerId_ = 0;

        const u64 revisionBefore = tileMapDocument_.revision();
        if (auto status = tileMapDocument_.paintCell(
                layerId, edit.x, edit.y, edit.localTileId); !status) {
            return status;
        }
        if (tileMapDocument_.revision() == revisionBefore) {
            authoringFeedback_ = edit.localTileId == 0U
                                     ? "Tile erase left an already-empty cell unchanged"
                                     : "Tile paint left the existing tile unchanged";
            return refreshAuthoringUi(tree);
        }
        if (edit.localTileId != 0U) {
            lastPaintedTile_ = edit;
            selectedTileId_ = static_cast<Tina::Core::u16>(edit.localTileId % 4U + 1U);
        } else {
            lastPaintedTile_.reset();
        }
        ++counters_.authoringEdits;
        ++counters_.tileMapEdits;
        if (auto status = validateRuntimePreview(); !status) {
            return status;
        }
        authoringFeedback_ = edit.localTileId == 0U
                                 ? "Viewport tile erase committed"
                                 : "Viewport tile paint committed";
        return refreshAuthoringUi(tree);
    }

    [[nodiscard]] Tina::Core::Status
    refreshViewportToolUi(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const bool tileToolsAvailable = tileMapEditingContext();
        if (!tileToolsAvailable &&
            (viewportToolMode_ == ViewportToolMode::TilePaint ||
             viewportToolMode_ == ViewportToolMode::TileErase)) {
            viewportToolMode_ = ViewportToolMode::Select;
        }
        const bool selectActive = viewportToolMode_ == ViewportToolMode::Select;
        for (const UI::UINodeId button : selectToolButtons_) {
            if (auto status = tree.setEnabled(button, !selectActive); !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : moveToolButtons_) {
            if (auto status = tree.setEnabled(button, viewportToolMode_ != ViewportToolMode::Move); !status) {
                return status;
            }
        }
        if (auto status = tree.setEnabled(
                tilePaintToolButton_,
                tileToolsAvailable && viewportToolMode_ != ViewportToolMode::TilePaint); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                tileEraseToolButton_,
                tileToolsAvailable && viewportToolMode_ != ViewportToolMode::TileErase); !status) {
            return status;
        }
        std::string_view toolStatus = "Select | Local | Free";
        if (viewportToolMode_ == ViewportToolMode::Move) {
            toolStatus = "Move | Local | Free";
        } else if (viewportToolMode_ == ViewportToolMode::TilePaint) {
            toolStatus = "Tile Paint | Grid Snap";
        } else if (viewportToolMode_ == ViewportToolMode::TileErase) {
            toolStatus = "Tile Erase | Grid Snap";
        }
        return tree.setText(viewportToolStatus_, toolStatus);
    }

    [[nodiscard]] const Tina::Scene::World2DEntityBinding*
    findPreviewBinding(UI::UITreeViewItemKey hierarchyKey) const noexcept
    {
        const u32 stableEntityId = stableEntityIdForHierarchyItem(hierarchyKey);
        const auto binding = std::find_if(
            previewBindings_.begin(), previewBindings_.end(),
            [stableEntityId](const Tina::Scene::World2DEntityBinding& candidate) {
                return candidate.stableEntityId == stableEntityId;
            });
        return binding == previewBindings_.end() ? nullptr : &*binding;
    }

    [[nodiscard]] Tina::Core::Status
    extractWorld3DViewport(Tina::RenderSceneExtractionContext& context) const
    {
        if (!previewCamera3D_.hasValue()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor World3D viewport is missing its camera");
        }
        Tina::Scene::PerspectiveCamera3D camera{
            .verticalFovDegrees = 55.0F,
            .nearPlaneMeters = 0.1F,
            .farPlaneMeters = 100.0F,
            .normalizedViewport = *viewportNormalized_,
            .active = true,
        };
        if (auto status = previewWorld_->setPerspectiveCamera3D(previewCamera3D_, camera); !status) {
            return status;
        }
        if (auto status = Tina::Scene::extractRenderSceneFromWorld(
                *previewWorld_, context.renderSceneWriter(), context.frameResourceSink(),
                Tina::Scene::ExtractRenderSceneParams{
                    .surfaceViewport = {
                        .pixelWidth = surfacePixelWidth_,
                        .pixelHeight = surfacePixelHeight_,
                    },
                    .mesh3DBindingResolver = {
                        .userData = const_cast<EditorWorkspaceState*>(this),
                        .resolve = &EditorWorkspaceState::resolvePreviewMesh,
                    },
                    .material3DBindingResolver = {
                        .userData = const_cast<EditorWorkspaceState*>(this),
                        .resolve = &EditorWorkspaceState::resolvePreviewMaterial,
                    },
                });
            !status) {
            return status;
        }
        counters_.gpuViewportMeshes = previewResolvedMeshCount_;
        counters_.gpuViewportDocumentRevision = previewRevision_;
        return Tina::Core::success();
    }

    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewSprite(void* userData, Tina::Asset::AssetHandle asset,
                         Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<EditorWorkspaceState*>(userData);
        if (!self.spriteBindings_.has_value()) {
            return Tina::Render::FrameResourceRef{};
        }
        return self.spriteBindings_->internSpriteFrameResource(asset, sink);
    }

    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewTexture(void* userData, Tina::Asset::AssetHandle asset,
                          Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<EditorWorkspaceState*>(userData);
        if (!self.spriteBindings_.has_value() || !self.assetResources_.system.has_value()) {
            return Tina::Render::FrameResourceRef{};
        }
        const Tina::Core::AssetId assetId = self.assetResources_.system->store().assetId(asset);
        if (!assetId.hasValue()) {
            return Tina::Render::FrameResourceRef{};
        }
        const auto resolver = self.spriteBindings_->texture2DFrameResourceResolver();
        auto resolution = resolver.resolve(resolver.userData, assetId, sink);
        if (!resolution) {
            return Tina::Core::failure(std::move(resolution.error()));
        }
        return resolution->has_value() ? resolution->value().resource
                                       : Tina::Render::FrameResourceRef{};
    }

    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewTileset(void* userData, Tina::Asset::AssetHandle asset,
                          Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<EditorWorkspaceState*>(userData);
        if (!self.spriteBindings_.has_value()) {
            return Tina::Render::FrameResourceRef{};
        }
        return self.spriteBindings_->internTilesetFrameResource(asset, sink);
    }

    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewMesh(void* userData, Tina::Asset::AssetHandle asset,
                       Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<EditorWorkspaceState*>(userData);
        if (!self.mesh3DBindings_.has_value()) {
            return Tina::Render::FrameResourceRef{};
        }
        return self.mesh3DBindings_->internMeshFrameResource(asset, sink);
    }

    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewMaterial(void* userData, Tina::Asset::AssetHandle asset,
                           Tina::Render::FrameResourceSink& sink) noexcept
    {
        auto& self = *static_cast<EditorWorkspaceState*>(userData);
        if (!self.mesh3DBindings_.has_value()) {
            return Tina::Render::FrameResourceRef{};
        }
        return self.mesh3DBindings_->internMaterialFrameResource(asset, sink);
    }

    [[nodiscard]] Tina::Core::Status updateGpuViewport(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        auto viewportRect = tree.committedLayoutRect(viewportPreviewLayer_);
        if (!viewportRect) {
            return Tina::Core::failure(std::move(viewportRect.error()));
        }
        auto rootRect = tree.committedLayoutRect(uiRoot_.rootNodeId());
        if (!rootRect) {
            return Tina::Core::failure(std::move(rootRect.error()));
        }
        if (!std::isfinite(rootRect->x) || !std::isfinite(rootRect->y) ||
            !std::isfinite(rootRect->width) || !std::isfinite(rootRect->height) ||
            !std::isfinite(viewportRect->x) || !std::isfinite(viewportRect->y) ||
            !std::isfinite(viewportRect->width) || !std::isfinite(viewportRect->height) ||
            rootRect->width <= 0.0F || rootRect->height <= 0.0F ||
            viewportRect->width <= 0.0F || viewportRect->height <= 0.0F) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport committed layout is not usable");
        }

        const double rootWidth = rootRect->width;
        const double rootHeight = rootRect->height;
        const double left = std::clamp(
            (static_cast<double>(viewportRect->x) - rootRect->x) / rootWidth, 0.0, 1.0);
        const double top = std::clamp(
            (static_cast<double>(viewportRect->y) - rootRect->y) / rootHeight, 0.0, 1.0);
        const double right = std::clamp(
            (static_cast<double>(viewportRect->x) + viewportRect->width - rootRect->x) /
                rootWidth,
            0.0, 1.0);
        const double bottom = std::clamp(
            (static_cast<double>(viewportRect->y) + viewportRect->height - rootRect->y) /
                rootHeight,
            0.0, 1.0);
        if (!(right > left) || !(bottom > top)) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport lies outside the committed UI root");
        }

        Tina::Render::RenderNormalizedViewport normalized{
            .x = static_cast<float>(left),
            .y = static_cast<float>(top),
            .width = static_cast<float>(right - left),
            .height = static_cast<float>(bottom - top),
        };
        if (static_cast<double>(normalized.x) + normalized.width > 1.0) {
            normalized.width = std::nextafter(normalized.width, 0.0F);
        }
        if (static_cast<double>(normalized.y) + normalized.height > 1.0) {
            normalized.height = std::nextafter(normalized.height, 0.0F);
        }
        if (normalized.width <= 0.0F || normalized.height <= 0.0F) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport normalized extent is empty");
        }

        viewportLogicalRect_ = *viewportRect;
        viewportNormalized_ = normalized;
        const double maximumSurfaceExtent =
            static_cast<double>((std::numeric_limits<u32>::max)());
        surfacePixelWidth_ = static_cast<u32>(
            std::clamp(std::round(static_cast<double>(rootRect->width)), 1.0,
                       maximumSurfaceExtent));
        surfacePixelHeight_ = static_cast<u32>(
            std::clamp(std::round(static_cast<double>(rootRect->height)), 1.0,
                       maximumSurfaceExtent));
        counters_.viewportLogicalX = viewportRect->x;
        counters_.viewportLogicalY = viewportRect->y;
        counters_.viewportLogicalWidth = viewportRect->width;
        counters_.viewportLogicalHeight = viewportRect->height;
        counters_.viewportNormalizedX = normalized.x;
        counters_.viewportNormalizedY = normalized.y;
        counters_.viewportNormalizedWidth = normalized.width;
        counters_.viewportNormalizedHeight = normalized.height;
        counters_.gpuViewportReady = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status editTileMapBrushCell(bool erase)
    {
        if (!tileMapEditingContext()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "TileMap brush requires the 2D TileMap selection");
        }
        auto authored = tileMapDocument_.snapshot();
        if (!authored) {
            return Tina::Core::failure(std::move(authored.error()));
        }
        auto layer = std::find_if(
            authored->layers.begin(), authored->layers.end(), [this](const auto& candidate) {
                return candidate.stableLayerId == activeTileMapLayerId_ &&
                       candidate.kind == Tina::AssetFormat::TileMapLayerKind::Tile;
            });
        if (layer == authored->layers.end()) {
            layer = std::find_if(authored->layers.begin(), authored->layers.end(),
                                 [](const auto& candidate) {
                                     return candidate.kind ==
                                            Tina::AssetFormat::TileMapLayerKind::Tile;
                                 });
        }
        if (layer == authored->layers.end()) {
            return Tina::Core::failure(Tina::Editor::EditorErrorCode::LayerNotFound,
                                       "TileMap document has no tile layer for the brush");
        }
        activeTileMapLayerId_ = layer->stableLayerId;

        const auto authoredCellAt = [&](u32 x, u32 y) noexcept {
            const u32 chunkX = x / authored->chunkSizeCells;
            const u32 chunkY = y / authored->chunkSizeCells;
            const auto chunk = std::find_if(
                layer->chunks.begin(), layer->chunks.end(),
                [chunkX, chunkY](const Tina::Editor::TileMapAuthoringChunk& candidate) {
                    return candidate.chunkX == chunkX && candidate.chunkY == chunkY;
                });
            if (chunk == layer->chunks.end()) {
                return Tina::Core::u16{0};
            }
            const u32 originX = chunkX * authored->chunkSizeCells;
            const u32 originY = chunkY * authored->chunkSizeCells;
            const u32 width = (std::min)(
                static_cast<u32>(authored->chunkSizeCells), authored->widthCells - originX);
            const auto index = static_cast<Tina::Core::usize>(y - originY) * width +
                               (x - originX);
            return index < chunk->cells.size() ? chunk->cells[index] : Tina::Core::u16{0};
        };

        u32 x = tileBrushX_ % authored->widthCells;
        u32 y = tileBrushY_ % authored->heightCells;
        if (erase && lastPaintedTile_.has_value()) {
            x = lastPaintedTile_->x;
            y = lastPaintedTile_->y;
        } else if (erase && authoredCellAt(x, y) == 0U) {
            bool found = false;
            for (const auto& chunk : layer->chunks) {
                const u32 originX = chunk.chunkX * authored->chunkSizeCells;
                const u32 originY = chunk.chunkY * authored->chunkSizeCells;
                const u32 width = (std::min)(
                    static_cast<u32>(authored->chunkSizeCells), authored->widthCells - originX);
                for (Tina::Core::usize index = 0; index < chunk.cells.size(); ++index) {
                    if (chunk.cells[index] == 0U) {
                        continue;
                    }
                    x = originX + static_cast<u32>(index % width);
                    y = originY + static_cast<u32>(index / width);
                    found = true;
                    break;
                }
                if (found) {
                    break;
                }
            }
        }

        Tina::Core::u16 tileId = erase ? 0U : selectedTileId_;
        if (!erase && authoredCellAt(x, y) == tileId) {
            tileId = static_cast<Tina::Core::u16>(tileId % 4U + 1U);
        }
        const auto revisionBefore = tileMapDocument_.revision();
        if (auto status = tileMapDocument_.paintCell(layer->stableLayerId, x, y, tileId);
            !status) {
            return status;
        }
        if (tileMapDocument_.revision() == revisionBefore) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "TileMap brush operation did not change the selected cell");
        }
        if (erase) {
            lastPaintedTile_.reset();
        } else {
            lastPaintedTile_ = Tina::Editor::TileMapAuthoringCellEdit{
                .x = x,
                .y = y,
                .localTileId = tileId,
            };
            selectedTileId_ = static_cast<Tina::Core::u16>(tileId % 4U + 1U);
            tileBrushX_ = (x + 1U) % authored->widthCells;
            tileBrushY_ = y + (tileBrushX_ == 0U ? 1U : 0U);
            tileBrushY_ %= authored->heightCells;
        }
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status toggleActiveTileMapLayer()
    {
        if (!tileMapEditingContext()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "TileMap layer controls require the 2D TileMap selection");
        }
        auto authored = tileMapDocument_.snapshot();
        if (!authored) {
            return Tina::Core::failure(std::move(authored.error()));
        }
        auto layer = std::find_if(
            authored->layers.begin(), authored->layers.end(), [this](const auto& candidate) {
                return candidate.stableLayerId == activeTileMapLayerId_ &&
                       candidate.kind == Tina::AssetFormat::TileMapLayerKind::Tile;
            });
        if (layer == authored->layers.end()) {
            layer = std::find_if(authored->layers.begin(), authored->layers.end(),
                                 [](const auto& candidate) {
                                     return candidate.kind ==
                                            Tina::AssetFormat::TileMapLayerKind::Tile;
                                 });
        }
        if (layer == authored->layers.end()) {
            return Tina::Core::failure(Tina::Editor::EditorErrorCode::LayerNotFound,
                                       "TileMap document has no tile layer to toggle");
        }
        activeTileMapLayerId_ = layer->stableLayerId;
        return tileMapDocument_.setLayerVisibility(layer->stableLayerId, !layer->visible);
    }

    [[nodiscard]] Tina::Core::Status
    addTileMapLayer(Tina::AssetFormat::TileMapLayerKind kind)
    {
        if (!tileMapEditingContext()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "TileMap layer controls require the 2D TileMap selection");
        }
        auto authored = tileMapDocument_.snapshot();
        if (!authored) {
            return Tina::Core::failure(std::move(authored.error()));
        }
        u32 layerId = 1U;
        while (std::any_of(authored->layers.begin(), authored->layers.end(),
                           [layerId](const auto& candidate) {
                               return candidate.stableLayerId == layerId;
                           })) {
            if (layerId == (std::numeric_limits<u32>::max)()) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                    "TileMap layer identity space is exhausted");
            }
            ++layerId;
        }
        if (kind == Tina::AssetFormat::TileMapLayerKind::Tile) {
            if (auto status = tileMapDocument_.addTileLayer(
                    layerId, "Tile Layer " + std::to_string(layerId)); !status) {
                return status;
            }
            activeTileMapLayerId_ = layerId;
            return Tina::Core::success();
        }
        return tileMapDocument_.addObjectLayer(
            layerId, "Object Layer " + std::to_string(layerId));
    }

    [[nodiscard]] Tina::Core::Status executeEditorCommand(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const EditorCommand command = *pendingEditorCommand_;
        pendingEditorCommand_.reset();

        Tina::Core::Status status = Tina::Core::success();
        bool requiresPreviewValidation = false;
        bool animationDocumentChanged = false;
        if (command >= EditorCommand::AnimationTogglePlayback &&
            workspaceMode_ != WorkspaceMode::World2D) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "SpriteAnimationClip authoring is available in the 2D workspace");
        }
        switch (command) {
        case EditorCommand::SwitchToWorld2D:
            status = activateWorkspace(tree, WorkspaceMode::World2D);
            break;
        case EditorCommand::SwitchToWorld3D:
            status = activateWorkspace(tree, WorkspaceMode::World3D);
            break;
        case EditorCommand::MoveSelectedPositiveX:
            status = moveSelectedPositiveX();
            if (status) {
                ++counters_.authoringEdits;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Move X +1 applied as one document revision";
            }
            break;
        case EditorCommand::ApplyTransform: {
            auto positionXText = tree.text(inspectorPositionX_);
            if (!positionXText) {
                return Tina::Core::failure(std::move(positionXText.error()));
            }
            auto positionYText = tree.text(inspectorPositionY_);
            if (!positionYText) {
                return Tina::Core::failure(std::move(positionYText.error()));
            }
            auto positionZText = tree.text(inspectorPositionZ_);
            if (!positionZText) {
                return Tina::Core::failure(std::move(positionZText.error()));
            }
            auto rotationXText = tree.text(inspectorRotationX_);
            if (!rotationXText) {
                return Tina::Core::failure(std::move(rotationXText.error()));
            }
            auto rotationYText = tree.text(inspectorRotationY_);
            if (!rotationYText) {
                return Tina::Core::failure(std::move(rotationYText.error()));
            }
            auto rotationZText = tree.text(inspectorRotationZ_);
            if (!rotationZText) {
                return Tina::Core::failure(std::move(rotationZText.error()));
            }
            auto scaleXText = tree.text(inspectorScaleX_);
            if (!scaleXText) {
                return Tina::Core::failure(std::move(scaleXText.error()));
            }
            auto scaleYText = tree.text(inspectorScaleY_);
            if (!scaleYText) {
                return Tina::Core::failure(std::move(scaleYText.error()));
            }
            auto scaleZText = tree.text(inspectorScaleZ_);
            if (!scaleZText) {
                return Tina::Core::failure(std::move(scaleZText.error()));
            }
            float positionX = 0.0F;
            float positionY = 0.0F;
            float positionZ = 0.0F;
            EulerDegrees rotationDegrees{};
            float scaleX = 1.0F;
            float scaleY = 1.0F;
            float scaleZ = 1.0F;
            if (!parseFiniteFloat(*positionXText, positionX) || !parseFiniteFloat(*positionYText, positionY) ||
                !parseFiniteFloat(*rotationZText, rotationDegrees.z) ||
                !parseFiniteFloat(*scaleXText, scaleX) || !parseFiniteFloat(*scaleYText, scaleY) ||
                (workspaceMode_ == WorkspaceMode::World3D &&
                 (!parseFiniteFloat(*positionZText, positionZ) ||
                  !parseFiniteFloat(*rotationXText, rotationDegrees.x) ||
                  !parseFiniteFloat(*rotationYText, rotationDegrees.y) ||
                  !parseFiniteFloat(*scaleZText, scaleZ)))) {
                ++counters_.inspectorRejectedTransactions;
                authoringFeedback_ = "Transform rejected: enter finite decimal values";
                return refreshAuthoringUi(tree);
            }
            status = applySelectedTransform(positionX, positionY, positionZ, rotationDegrees,
                                            scaleX, scaleY, scaleZ);
            if (status) {
                ++counters_.authoringEdits;
                ++counters_.inspectorTransactions;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Transform applied as one document revision";
            }
            break;
        }
        case EditorCommand::Undo:
            status = tileMapEditingContext()
                         ? tileMapDocument_.undo()
                         : (workspaceMode_ == WorkspaceMode::World2D ? document_.undo()
                                                                      : document3D_.undo());
            if (status) {
                ++counters_.authoringUndos;
                if (tileMapEditingContext()) {
                    ++counters_.tileMapUndos;
                }
                requiresPreviewValidation = true;
                authoringFeedback_ = "Undo restored the previous canonical snapshot";
            }
            break;
        case EditorCommand::Redo:
            status = tileMapEditingContext()
                         ? tileMapDocument_.redo()
                         : (workspaceMode_ == WorkspaceMode::World2D ? document_.redo()
                                                                      : document3D_.redo());
            if (status) {
                ++counters_.authoringRedos;
                if (tileMapEditingContext()) {
                    ++counters_.tileMapRedos;
                }
                requiresPreviewValidation = true;
                authoringFeedback_ = "Redo restored the next canonical snapshot";
            }
            break;
        case EditorCommand::Save:
            status = saveActiveDocument();
            if (status) {
                authoringFeedback_ = workspaceMode_ == WorkspaceMode::World2D
                                         ? "Canonical World2D document saved atomically"
                                         : "Canonical Prefab v2 document saved atomically";
            }
            break;
        case EditorCommand::PaintTile:
            status = editTileMapBrushCell(false);
            if (status) {
                ++counters_.authoringEdits;
                ++counters_.tileMapEdits;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Tile painted as one root/chunk revision";
            }
            break;
        case EditorCommand::EraseTile:
            status = editTileMapBrushCell(true);
            if (status) {
                ++counters_.authoringEdits;
                ++counters_.tileMapEdits;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Tile erased as one root/chunk revision";
            }
            break;
        case EditorCommand::ToggleTileLayer:
            status = toggleActiveTileMapLayer();
            if (status) {
                ++counters_.authoringEdits;
                ++counters_.tileMapEdits;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Tile layer visibility committed";
            }
            break;
        case EditorCommand::AddTileLayer:
            status = addTileMapLayer(Tina::AssetFormat::TileMapLayerKind::Tile);
            if (status) {
                ++counters_.authoringEdits;
                ++counters_.tileMapEdits;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Tile layer added to the TileMap document";
            }
            break;
        case EditorCommand::AddObjectLayer:
            status = addTileMapLayer(Tina::AssetFormat::TileMapLayerKind::Object);
            if (status) {
                ++counters_.authoringEdits;
                ++counters_.tileMapEdits;
                requiresPreviewValidation = true;
                authoringFeedback_ = "Object layer added to the TileMap document";
            }
            break;
        case EditorCommand::CookTileMapPreview: {
            if (!tileMapEditingContext()) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "TileMap cook preview requires the 2D TileMap selection");
                break;
            }
            auto preview = tileMapDocument_.cookPreview();
            if (!preview) {
                status = Tina::Core::failure(std::move(preview.error()));
                break;
            }
            counters_.tileMapCookArtifacts = preview->artifacts.size();
            counters_.tileMapCookPreviewBytes = 0;
            for (const auto& artifact : preview->artifacts) {
                counters_.tileMapCookPreviewBytes += artifact.cookedBytes.size();
            }
            authoringFeedback_ = "TileMap root and chunk cook preview rebuilt";
            break;
        }
        case EditorCommand::AnimationTogglePlayback:
            if (!animationAnimator_.has_value() || !animationPreviewAvailable_) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Animation preview requires resolved Sprite frames");
                break;
            }
            if (animationPlaying_) {
                animationAnimator_->pause();
                animationPlaying_ = false;
                authoringFeedback_ = "Animation preview paused";
            } else {
                animationAnimator_->restart();
                animationPlaying_ = true;
                status = applyAnimationPreviewFrame(0);
                authoringFeedback_ = "Animation preview playing";
            }
            ++counters_.animationPlaybackTransitions;
            break;
        case EditorCommand::AnimationPreviousFrame:
            animationPlaying_ = false;
            if (animationAnimator_.has_value()) {
                animationAnimator_->pause();
            }
            if (animationSelectedFrameIndex_ > 0U) {
                status = applyAnimationPreviewFrame(animationSelectedFrameIndex_ - 1U);
            }
            authoringFeedback_ = "Animation playhead stepped backward";
            break;
        case EditorCommand::AnimationNextFrame:
            animationPlaying_ = false;
            if (animationAnimator_.has_value()) {
                animationAnimator_->pause();
            }
            if (animationSelectedFrameIndex_ + 1U < spriteAnimationDocument_.frameCount()) {
                status = applyAnimationPreviewFrame(animationSelectedFrameIndex_ + 1U);
            }
            authoringFeedback_ = "Animation playhead stepped forward";
            break;
        case EditorCommand::AnimationAddFrame: {
            const auto selected = spriteAnimationDocument_.frameAt(animationSelectedFrameIndex_);
            if (!selected) {
                status = Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                             "Animation selected frame does not exist");
                break;
            }
            status = spriteAnimationDocument_.appendFrame(*selected);
            if (status) {
                animationSelectedFrameIndex_ =
                    static_cast<u32>(spriteAnimationDocument_.frameCount() - 1U);
                animationDocumentChanged = true;
                ++counters_.animationEdits;
                ++counters_.authoringEdits;
                authoringFeedback_ = "Animation frame appended as one clip revision";
            }
            break;
        }
        case EditorCommand::AnimationDuplicateFrame:
            status = spriteAnimationDocument_.duplicateFrame(animationSelectedFrameIndex_);
            if (status) {
                ++animationSelectedFrameIndex_;
                animationDocumentChanged = true;
                ++counters_.animationEdits;
                ++counters_.authoringEdits;
                authoringFeedback_ = "Animation frame duplicated as one clip revision";
            }
            break;
        case EditorCommand::AnimationDeleteFrame:
            status = spriteAnimationDocument_.eraseFrame(animationSelectedFrameIndex_);
            if (status) {
                animationSelectedFrameIndex_ = (std::min)(
                    animationSelectedFrameIndex_,
                    static_cast<u32>(spriteAnimationDocument_.frameCount() - 1U));
                animationDocumentChanged = true;
                ++counters_.animationEdits;
                ++counters_.authoringEdits;
                authoringFeedback_ = "Animation frame deleted as one clip revision";
            }
            break;
        case EditorCommand::AnimationMoveFrameLeft:
            if (animationSelectedFrameIndex_ > 0U) {
                status = spriteAnimationDocument_.moveFrame(
                    animationSelectedFrameIndex_, animationSelectedFrameIndex_ - 1U);
                if (status) {
                    --animationSelectedFrameIndex_;
                    animationDocumentChanged = true;
                    ++counters_.animationEdits;
                    ++counters_.authoringEdits;
                    authoringFeedback_ = "Animation frame moved left";
                }
            }
            break;
        case EditorCommand::AnimationMoveFrameRight:
            if (animationSelectedFrameIndex_ + 1U < spriteAnimationDocument_.frameCount()) {
                status = spriteAnimationDocument_.moveFrame(
                    animationSelectedFrameIndex_, animationSelectedFrameIndex_ + 1U);
                if (status) {
                    ++animationSelectedFrameIndex_;
                    animationDocumentChanged = true;
                    ++counters_.animationEdits;
                    ++counters_.authoringEdits;
                    authoringFeedback_ = "Animation frame moved right";
                }
            }
            break;
        case EditorCommand::AnimationCycleSprite: {
            const auto selected = spriteAnimationDocument_.frameAt(animationSelectedFrameIndex_);
            if (!selected) {
                status = Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                             "Animation selected frame does not exist");
                break;
            }
            Tina::Core::AssetId nextSprite = selected->spriteId;
            for (u32 offset = 1; offset < spriteAnimationDocument_.frameCount(); ++offset) {
                const u32 candidateIndex = static_cast<u32>(
                    (animationSelectedFrameIndex_ + offset) %
                    spriteAnimationDocument_.frameCount());
                const auto candidate = spriteAnimationDocument_.frameAt(candidateIndex);
                if (candidate && candidate->spriteId != selected->spriteId) {
                    nextSprite = candidate->spriteId;
                    break;
                }
            }
            if (nextSprite != selected->spriteId) {
                auto updated = *selected;
                updated.spriteId = nextSprite;
                status = spriteAnimationDocument_.setFrame(animationSelectedFrameIndex_, updated);
                if (status) {
                    animationDocumentChanged = true;
                    ++counters_.animationEdits;
                    ++counters_.authoringEdits;
                    authoringFeedback_ = "Animation frame Sprite binding changed";
                }
            }
            break;
        }
        case EditorCommand::AnimationDecreaseDuration:
        case EditorCommand::AnimationIncreaseDuration: {
            const auto selected = spriteAnimationDocument_.frameAt(animationSelectedFrameIndex_);
            if (!selected) {
                status = Tina::Core::failure(Tina::Editor::EditorErrorCode::FrameNotFound,
                                             "Animation selected frame does not exist");
                break;
            }
            const float delta = command == EditorCommand::AnimationIncreaseDuration
                                    ? 0.05F
                                    : -0.05F;
            const float duration = (std::max)(0.01F, selected->durationSeconds + delta);
            status = spriteAnimationDocument_.setFrameDuration(
                animationSelectedFrameIndex_, duration);
            if (status && duration != selected->durationSeconds) {
                animationDocumentChanged = true;
                ++counters_.animationEdits;
                ++counters_.authoringEdits;
                authoringFeedback_ = "Animation frame duration changed";
            }
            break;
        }
        case EditorCommand::AnimationCycleMode: {
            Tina::AssetFormat::SpriteAnimationPlaybackMode nextMode =
                Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop;
            switch (spriteAnimationDocument_.playbackMode()) {
            case Tina::AssetFormat::SpriteAnimationPlaybackMode::Once:
                nextMode = Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop;
                break;
            case Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop:
                nextMode = Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong;
                break;
            case Tina::AssetFormat::SpriteAnimationPlaybackMode::PingPong:
                nextMode = Tina::AssetFormat::SpriteAnimationPlaybackMode::Once;
                break;
            }
            status = spriteAnimationDocument_.setPlaybackMode(nextMode);
            if (status) {
                animationDocumentChanged = true;
                ++counters_.animationEdits;
                ++counters_.authoringEdits;
                authoringFeedback_ = "Animation playback mode changed";
            }
            break;
        }
        case EditorCommand::AnimationCookPreview: {
            auto preview = spriteAnimationDocument_.cookPreview();
            if (!preview) {
                status = Tina::Core::failure(std::move(preview.error()));
                break;
            }
            counters_.animationCookPreviewBytes = preview->cookedBytes.size();
            authoringFeedback_ = "SpriteAnimationClip Cook preview rebuilt";
            break;
        }
        case EditorCommand::AnimationUndo:
            status = spriteAnimationDocument_.undo();
            if (status) {
                animationSelectedFrameIndex_ = (std::min)(
                    animationSelectedFrameIndex_,
                    static_cast<u32>(spriteAnimationDocument_.frameCount() - 1U));
                animationDocumentChanged = true;
                ++counters_.animationUndos;
                ++counters_.authoringUndos;
                authoringFeedback_ = "Animation Undo restored the previous clip revision";
            }
            break;
        case EditorCommand::AnimationRedo:
            status = spriteAnimationDocument_.redo();
            if (status) {
                animationSelectedFrameIndex_ = (std::min)(
                    animationSelectedFrameIndex_,
                    static_cast<u32>(spriteAnimationDocument_.frameCount() - 1U));
                animationDocumentChanged = true;
                ++counters_.animationRedos;
                ++counters_.authoringRedos;
                authoringFeedback_ = "Animation Redo restored the next clip revision";
            }
            break;
        }
        if (!status) {
            return status;
        }
        if (animationDocumentChanged) {
            if (auto animationStatus = rebuildAnimationAnimator(); !animationStatus) {
                return animationStatus;
            }
            auto preview = spriteAnimationDocument_.cookPreview();
            if (!preview) {
                return Tina::Core::failure(std::move(preview.error()));
            }
            counters_.animationDocumentRevision = spriteAnimationDocument_.revision();
            counters_.animationFrameCount = spriteAnimationDocument_.frameCount();
            counters_.animationCookPreviewBytes = preview->cookedBytes.size();
        }
        if (requiresPreviewValidation) {
            if (auto previewStatus = validateRuntimePreview(); !previewStatus) {
                return previewStatus;
            }
        }
        return refreshAuthoringUi(tree);
    }

    [[nodiscard]] Tina::Core::Status
    activateWorkspace(Tina::PrimaryWindowUITreeUpdater& tree, WorkspaceMode mode)
    {
        if (workspaceMode_ == mode) {
            return Tina::Core::success();
        }
        workspaceMode_ = mode;
        if (mode == WorkspaceMode::World3D) {
            animationPlaying_ = false;
            if (animationAnimator_.has_value()) {
                animationAnimator_->pause();
            }
        }
        selectionKey_ = SceneRootKey;
        if (auto status = tree.invalidateTreeViewItems(hierarchyTree_); !status) {
            return status;
        }
        if (auto status = tree.setTreeViewSelectedIndex(hierarchyTree_, 0); !status) {
            return status;
        }
        if (auto status = refreshWorkspaceChrome(tree); !status) {
            return status;
        }
        if (auto status = validateRuntimePreview(); !status) {
            return status;
        }
        ++counters_.workspaceSwitches;
        authoringFeedback_ = mode == WorkspaceMode::World2D
                                 ? "World2D workspace active"
                                 : "World3D workspace active";
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status
    refreshWorkspaceChrome(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
        if (auto status = tree.setText(toolbarDocument_, world2D ? "World2D Scene" : "World3D Scene");
            !status) {
            return status;
        }
        if (auto status = tree.setText(breadcrumb_, world2D ? "Scene / World2D / Player"
                                                            : "Scene / World3D / HeroMesh");
            !status) {
            return status;
        }
        if (auto status = tree.setText(viewportTitle_, world2D ? "World2D Viewport"
                                                               : "World3D Viewport");
            !status) {
            return status;
        }
        if (auto status = tree.setText(viewportMode_, world2D ? "Orthographic" : "Perspective");
            !status) {
            return status;
        }
        if (auto status = tree.setText(gridStatus_, world2D ? "Tile Grid 1 m" : "Grid 1 m"); !status) {
            return status;
        }
        if (auto status = tree.setText(previewTitle_, world2D ? "World2D Scene" : "World3D Scene");
            !status) {
            return status;
        }
        if (auto status = tree.setText(previewEntities_,
                                       world2D ? "Camera | Player | TileMap"
                                               : "Camera | Hero | Left | Right");
            !status) {
            return status;
        }
        if (auto status = tree.setText(previewCook_,
                                       world2D ? "World2D + TileMap -> Scene"
                                               : "Prefab v2 -> Catalog -> Scene");
            !status) {
            return status;
        }
        std::string assetStatus = assetResources_.projectCatalogConfigured
                                      ? "Project Catalog | "
                                      : "Built-in Catalog | ";
        assetStatus += world2D ? "2D " : "3D ";
        assetStatus += std::to_string(world2D ? previewResolvedSpriteCount_
                                              : previewResolvedMeshCount_);
        assetStatus += " resolved";
        if (auto status = tree.setText(previewAssetStatus_, assetStatus); !status) {
            return status;
        }
        if (auto status = tree.setText(cameraStatus_, world2D ? "Camera2D" : "Camera3D"); !status) {
            return status;
        }
        if (auto status = tree.setText(documentFormat_,
                                       world2D ? "World2D v1 + TileMap v3/v1 | canonical"
                                               : "Prefab schema v2 | canonical");
            !status) {
            return status;
        }
        if (auto status = tree.setText(componentLabels_[1],
                                       world2D ? "SpriteRenderer2D" : "MeshRenderer3D");
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(mode2DButton_, !world2D); !status) {
            return status;
        }
        return tree.setEnabled(mode3DButton_, world2D);
    }

    [[nodiscard]] Tina::Core::Status saveActiveDocument()
    {
        WorkspaceSessionState& session = activeWorkspaceSession();
        if (!session.hasDocumentPath()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "active editor workspace has no configured document path");
        }
        try {
            const std::span<const std::byte> current = activeDocumentBytes();
            std::vector<std::byte> savedCandidate(current.begin(), current.end());
            Tina::Core::Status status = workspaceMode_ == WorkspaceMode::World2D
                                            ? Tina::Editor::saveWorld2DAuthoringDocument(
                                                  session.documentPathUtf8, document_)
                                            : Tina::Editor::saveWorld3DAuthoringDocument(
                                                  session.documentPathUtf8, document3D_);
            if (!status) {
                return status;
            }
            session.savedBaselineBytes = std::move(savedCandidate);
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Tina Editor could not retain the saved document baseline");
        }
        ++counters_.authoringSaves;
        return Tina::Core::success();
    }

    [[nodiscard]] WorkspaceSessionState& activeWorkspaceSession() noexcept
    {
        return workspaceMode_ == WorkspaceMode::World2D ? world2DSession_ : world3DSession_;
    }

    [[nodiscard]] const WorkspaceSessionState& activeWorkspaceSession() const noexcept
    {
        return workspaceMode_ == WorkspaceMode::World2D ? world2DSession_ : world3DSession_;
    }

    [[nodiscard]] const WorkspaceSessionState& workspaceSession(WorkspaceMode mode) const noexcept
    {
        return mode == WorkspaceMode::World2D ? world2DSession_ : world3DSession_;
    }

    [[nodiscard]] std::span<const std::byte> documentBytes(WorkspaceMode mode) const noexcept
    {
        return mode == WorkspaceMode::World2D ? document_.snapshotBytes()
                                               : document3D_.payloadBytes();
    }

    [[nodiscard]] bool isDocumentDirty(WorkspaceMode mode) const noexcept
    {
        const WorkspaceSessionState& session = workspaceSession(mode);
        const std::span<const std::byte> current = documentBytes(mode);
        return session.savedBaselineBytes.size() != current.size() ||
               !std::equal(session.savedBaselineBytes.begin(), session.savedBaselineBytes.end(),
                           current.begin());
    }

    void publishWorkspaceSessionCounters() noexcept
    {
        const bool world2DDirty = isDocumentDirty(WorkspaceMode::World2D);
        const bool world3DDirty = isDocumentDirty(WorkspaceMode::World3D);
        const WorkspaceSessionState& active = activeWorkspaceSession();

        counters_.finalWorkspaceWorld2D = workspaceMode_ == WorkspaceMode::World2D;
        counters_.world2DDocumentPathConfigured = world2DSession_.hasDocumentPath();
        counters_.world3DDocumentPathConfigured = world3DSession_.hasDocumentPath();
        counters_.world2DDocumentLoaded = world2DSession_.loadedFromPath;
        counters_.world3DDocumentLoaded = world3DSession_.loadedFromPath;
        counters_.world2DDocumentDirty = world2DDirty;
        counters_.world3DDocumentDirty = world3DDirty;
        counters_.world2DSavedSnapshotBytes = world2DSession_.savedBaselineBytes.size();
        counters_.world3DSavedSnapshotBytes = world3DSession_.savedBaselineBytes.size();
        counters_.documentPathConfigured = active.hasDocumentPath();
        counters_.documentLoaded = active.loadedFromPath;
        counters_.documentDirty = workspaceMode_ == WorkspaceMode::World2D ? world2DDirty
                                                                           : world3DDirty;
        counters_.documentSaved = active.hasDocumentPath() && !counters_.documentDirty;
        counters_.savedSnapshotBytes = active.savedBaselineBytes.size();
    }

    [[nodiscard]] std::span<const std::byte> activeDocumentBytes() const noexcept
    {
        return documentBytes(workspaceMode_);
    }

    [[nodiscard]] u64 activeDocumentRevision() const noexcept
    {
        return workspaceMode_ == WorkspaceMode::World2D ? document_.revision()
                                                        : document3D_.revision();
    }

    [[nodiscard]] u64 activeDocumentItemCount() const noexcept
    {
        return workspaceMode_ == WorkspaceMode::World2D ? document_.entityCount()
                                                        : document3D_.nodeCount();
    }

    [[nodiscard]] u64 activeUndoDepth() const noexcept
    {
        return tileMapEditingContext()
                   ? tileMapDocument_.undoDepth()
                   : (workspaceMode_ == WorkspaceMode::World2D ? document_.undoDepth()
                                                                : document3D_.undoDepth());
    }

    [[nodiscard]] u64 activeRedoDepth() const noexcept
    {
        return tileMapEditingContext()
                   ? tileMapDocument_.redoDepth()
                   : (workspaceMode_ == WorkspaceMode::World2D ? document_.redoDepth()
                                                                : document3D_.redoDepth());
    }

    [[nodiscard]] bool activeCanUndo() const noexcept
    {
        return tileMapEditingContext()
                   ? tileMapDocument_.canUndo()
                   : (workspaceMode_ == WorkspaceMode::World2D ? document_.canUndo()
                                                                : document3D_.canUndo());
    }

    [[nodiscard]] bool activeCanRedo() const noexcept
    {
        return tileMapEditingContext()
                   ? tileMapDocument_.canRedo()
                   : (workspaceMode_ == WorkspaceMode::World2D ? document_.canRedo()
                                                                : document3D_.canRedo());
    }

    [[nodiscard]] Tina::Core::Status moveSelectedPositiveX()
    {
        const u32 stableEntityId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableEntityId == 0U) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "editor selection has no authoring entity");
        }

        if (workspaceMode_ == WorkspaceMode::World3D) {
            std::vector<Tina::AssetFormat::PrefabNodeView> views;
            auto prefab = document3D_.parseCurrentPrefab(views);
            if (!prefab) {
                return Tina::Core::failure(std::move(prefab.error()));
            }
            const auto node = std::find_if(views.begin(), views.end(), [stableEntityId](const auto& candidate) {
                return candidate.stableNodeId == stableEntityId;
            });
            if (node == views.end()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                           "editor selection is absent from the World3D document");
            }
            Tina::AssetFormat::PrefabNodeDesc edited{
                .stableNodeId = node->stableNodeId,
                .parentIndex = node->parentIndex,
                .positionX = node->positionX + 1.0F,
                .positionY = node->positionY,
                .positionZ = node->positionZ,
                .rotationX = node->rotationX,
                .rotationY = node->rotationY,
                .rotationZ = node->rotationZ,
                .rotationW = node->rotationW,
                .scaleX = node->scaleX,
                .scaleY = node->scaleY,
                .scaleZ = node->scaleZ,
                .meshId = node->meshId,
                .materialId = node->materialId,
                .visible = node->visible,
            };
            return document3D_.upsertNode(edited);
        }

        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto entity = std::find_if(storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
            return candidate.stableEntityId == stableEntityId;
        });
        if (entity == storage.end()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "editor selection is absent from the authoring document");
        }
        auto edited = *entity;
        edited.positionX += 1.0F;
        return document_.upsertEntity(edited);
    }

    [[nodiscard]] Tina::Core::Status applySelectedTransform(float positionX, float positionY,
                                                            float positionZ, EulerDegrees rotationDegrees,
                                                            float scaleX, float scaleY, float scaleZ)
    {
        const u32 stableEntityId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableEntityId == 0U) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "editor selection has no authoring entity");
        }

        if (workspaceMode_ == WorkspaceMode::World3D) {
            std::vector<Tina::AssetFormat::PrefabNodeView> views;
            auto prefab = document3D_.parseCurrentPrefab(views);
            if (!prefab) {
                return Tina::Core::failure(std::move(prefab.error()));
            }
            const auto node = std::find_if(views.begin(), views.end(), [stableEntityId](const auto& candidate) {
                return candidate.stableNodeId == stableEntityId;
            });
            if (node == views.end()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                           "editor selection is absent from the World3D document");
            }
            const std::array rotation = quaternionFromEulerDegrees(rotationDegrees);
            Tina::AssetFormat::PrefabNodeDesc edited{
                .stableNodeId = node->stableNodeId,
                .parentIndex = node->parentIndex,
                .positionX = positionX,
                .positionY = positionY,
                .positionZ = positionZ,
                .rotationX = rotation[0],
                .rotationY = rotation[1],
                .rotationZ = rotation[2],
                .rotationW = rotation[3],
                .scaleX = scaleX,
                .scaleY = scaleY,
                .scaleZ = scaleZ,
                .meshId = node->meshId,
                .materialId = node->materialId,
                .visible = node->visible,
            };
            return document3D_.upsertNode(edited);
        }

        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto entity = std::find_if(storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
            return candidate.stableEntityId == stableEntityId;
        });
        if (entity == storage.end()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::NotFound,
                                       "editor selection is absent from the authoring document");
        }
        auto edited = *entity;
        edited.positionX = positionX;
        edited.positionY = positionY;
        const float normalizedDegrees = std::remainder(rotationDegrees.z, 360.0F);
        const float halfRadians = normalizedDegrees * DegreesToRadians * 0.5F;
        edited.rotationX = 0.0F;
        edited.rotationY = 0.0F;
        edited.rotationZ = std::sin(halfRadians);
        edited.rotationW = std::cos(halfRadians);
        edited.scaleX = scaleX;
        edited.scaleY = scaleY;
        return document_.upsertEntity(edited);
    }

    [[nodiscard]] Tina::Asset::AssetHandle
    loadedAsset(Tina::Core::AssetId assetId, Tina::AssetFormat::AssetKind expectedKind) const noexcept
    {
        if (!assetResources_.system.has_value()) {
            return {};
        }
        const auto handle = assetResources_.system->find(assetId);
        if (!handle.has_value() ||
            assetResources_.system->store().assetKind(*handle) != expectedKind ||
            assetResources_.system->tryGet(*handle) == nullptr) {
            return {};
        }
        return *handle;
    }

    [[nodiscard]] static bool containsHandle(std::span<const Tina::Asset::AssetHandle> handles,
                                             Tina::Asset::AssetHandle handle) noexcept
    {
        return std::find(handles.begin(), handles.end(), handle) != handles.end();
    }

    [[nodiscard]] Tina::Core::Status preparePreviewAssetBindings()
    {
        auto* device = renderDeviceAccess_.get();
        if (device == nullptr || !assetResources_.system.has_value() ||
            assetResources_.system->catalog() == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Tina Editor Catalog or RenderDevice is unavailable");
        }

        std::vector<Tina::AssetFormat::World2DEntityDesc> world2DStorage;
        auto world2D = document_.parseCurrentSnapshot(world2DStorage);
        if (!world2D) {
            return Tina::Core::failure(std::move(world2D.error()));
        }
        std::vector<Tina::AssetFormat::PrefabNodeView> world3DStorage;
        auto world3D = document3D_.parseCurrentPrefab(world3DStorage);
        if (!world3D) {
            return Tina::Core::failure(std::move(world3D.error()));
        }

        std::vector<PreviewAssetReference> references;
        const auto appendReference = [&references](Tina::Core::AssetId assetId,
                                                   Tina::AssetFormat::AssetKind kind) {
            if (!assetId.hasValue()) {
                return;
            }
            const PreviewAssetReference reference{.assetId = assetId, .kind = kind};
            if (std::find(references.begin(), references.end(), reference) == references.end()) {
                references.push_back(reference);
            }
        };
        for (const auto& entity : world2DStorage) {
            if (!entity.sprite.has_value()) {
                continue;
            }
            appendReference(entity.sprite->spriteId, Tina::AssetFormat::AssetKind::Sprite);
            appendReference(entity.sprite->normalTextureId,
                            Tina::AssetFormat::AssetKind::Texture2D);
        }
        for (const auto& node : world3DStorage) {
            if (!node.hasMesh) {
                continue;
            }
            appendReference(node.meshId, Tina::AssetFormat::AssetKind::StaticMesh);
            appendReference(node.materialId, Tina::AssetFormat::AssetKind::Material);
        }
        appendReference(tileMapDocument_.tilesetId(),
                        Tina::AssetFormat::AssetKind::Tileset);
        for (u32 frameIndex = 0; frameIndex < spriteAnimationDocument_.frameCount(); ++frameIndex) {
            const auto frame = spriteAnimationDocument_.frameAt(frameIndex);
            if (!frame) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "Animation frame disappeared while collecting Catalog references");
            }
            appendReference(frame->spriteId, Tina::AssetFormat::AssetKind::Sprite);
        }

        std::vector<Tina::Core::AssetId> loadIds;
        const Tina::Asset::CatalogSnapshot& catalog = *assetResources_.system->catalog();
        for (const PreviewAssetReference& reference : references) {
            const auto entryIndex = catalog.find(reference.assetId);
            const auto entry = entryIndex.has_value() ? catalog.entry(*entryIndex) : std::nullopt;
            if (!entry.has_value() || entry->assetKind != reference.kind) {
                ++counters_.catalogUnresolvedReferences;
                continue;
            }
            if (std::find(loadIds.begin(), loadIds.end(), reference.assetId) == loadIds.end()) {
                loadIds.push_back(reference.assetId);
            }
        }
        if (!loadIds.empty()) {
            auto loaded = assetResources_.system->load(loadIds);
            if (!loaded) {
                return Tina::Core::failure(std::move(loaded.error()));
            }
            loadedPreviewHandles_.assign(loaded->begin(), loaded->end());
            counters_.catalogAssetsLoaded = loaded->size();
        }

        std::vector<Tina::Asset::AssetHandle> spriteAssets;
        std::vector<Tina::Asset::AssetHandle> tilesetAssets;
        std::vector<Tina::Asset::AssetHandle> spriteTextureAssets;
        for (const PreviewAssetReference& reference : references) {
            if (reference.kind == Tina::AssetFormat::AssetKind::Texture2D) {
                const Tina::Asset::AssetHandle texture = loadedAsset(reference.assetId, reference.kind);
                if (texture && !containsHandle(spriteTextureAssets, texture)) {
                    spriteTextureAssets.push_back(texture);
                }
                continue;
            }
            if (reference.kind != Tina::AssetFormat::AssetKind::Sprite &&
                reference.kind != Tina::AssetFormat::AssetKind::Tileset) {
                continue;
            }
            const Tina::Asset::AssetHandle asset = loadedAsset(reference.assetId, reference.kind);
            if (!asset) {
                continue;
            }
            const Tina::Asset::CookedAssetFile* file = assetResources_.system->tryGet(asset);
            const auto textureDependency = file != nullptr ? file->dependency(0) : std::nullopt;
            if (!textureDependency.has_value() ||
                textureDependency->expectedKind != Tina::AssetFormat::AssetKind::Texture2D) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "Catalog 2D asset has no required Texture2D dependency");
            }
            const Tina::Asset::AssetHandle texture = loadedAsset(
                textureDependency->assetId, Tina::AssetFormat::AssetKind::Texture2D);
            if (!texture) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "Catalog 2D Texture2D dependency was not loaded");
            }
            if (reference.kind == Tina::AssetFormat::AssetKind::Sprite) {
                if (!containsHandle(spriteAssets, asset)) {
                    spriteAssets.push_back(asset);
                }
            } else if (!containsHandle(tilesetAssets, asset)) {
                tilesetAssets.push_back(asset);
            }
            if (!containsHandle(spriteTextureAssets, texture)) {
                spriteTextureAssets.push_back(texture);
            }
        }
        if (!spriteTextureAssets.empty()) {
            auto registry = Tina::Asset::Sprite2DBindingRegistry::Create(
                *assetResources_.system, *device,
                Tina::Asset::Sprite2DBindingRegistryConfig{
                    .textureCapacity = spriteTextureAssets.size(),
                    .memoryResource = &assetResources_.memory,
                });
            if (!registry) {
                return Tina::Core::failure(std::move(registry.error()));
            }
            spriteBindings_.emplace(std::move(*registry));
            for (const Tina::Asset::AssetHandle textureAsset : spriteTextureAssets) {
                const Tina::Asset::CookedAssetFile* textureFile =
                    assetResources_.system->tryGet(textureAsset);
                if (textureFile == nullptr) {
                    return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                               "Catalog Texture2D payload is unavailable");
                }
                auto texture = Tina::Asset::uploadTexture2DFromCooked(*device, *textureFile);
                if (!texture) {
                    return Tina::Core::failure(std::move(texture.error()));
                }
                Tina::Render::GpuTextureId gpuTexture = *texture;
                auto textureCleanup = Tina::Core::makeScopeExit([device, &gpuTexture]() noexcept {
                    if (gpuTexture) {
                        (void)device->destroyTexture2D(gpuTexture);
                    }
                });
                auto binding = spriteBindings_->registerTextureBinding(textureAsset, gpuTexture);
                if (!binding) {
                    return Tina::Core::failure(std::move(binding.error()));
                }
                textureCleanup.release();
                ++counters_.catalogGpuTextures;
                ++counters_.catalogSpriteBindings;
            }
            for (const Tina::Asset::AssetHandle sprite : spriteAssets) {
                if (spriteBindings_->resolveSprite(sprite) != 0) {
                    boundSpriteAssets_.push_back(sprite);
                }
            }
            for (const Tina::Asset::AssetHandle tileset : tilesetAssets) {
                if (spriteBindings_->resolveTileset(tileset) != 0) {
                    boundTilesetAssets_.push_back(tileset);
                }
            }
        }

        std::vector<Tina::Asset::AssetHandle> meshAssets;
        std::vector<Tina::Asset::AssetHandle> materialAssets;
        std::vector<Tina::Asset::AssetHandle> materialTextureAssets;
        for (const PreviewAssetReference& reference : references) {
            if (reference.kind == Tina::AssetFormat::AssetKind::StaticMesh) {
                const Tina::Asset::AssetHandle mesh = loadedAsset(reference.assetId, reference.kind);
                if (mesh && !containsHandle(meshAssets, mesh)) {
                    meshAssets.push_back(mesh);
                }
            } else if (reference.kind == Tina::AssetFormat::AssetKind::Material) {
                const Tina::Asset::AssetHandle material = loadedAsset(reference.assetId, reference.kind);
                if (!material || containsHandle(materialAssets, material)) {
                    continue;
                }
                materialAssets.push_back(material);
                const Tina::Asset::CookedAssetFile* file = assetResources_.system->tryGet(material);
                if (file == nullptr) {
                    return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                               "Catalog Material payload is unavailable");
                }
                for (u32 dependencyIndex = 0;
                     dependencyIndex < file->header().dependencyCount;
                     ++dependencyIndex) {
                    const auto dependency = file->dependency(dependencyIndex);
                    if (!dependency.has_value() ||
                        dependency->expectedKind != Tina::AssetFormat::AssetKind::Texture2D) {
                        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                                   "Catalog Material has an invalid dependency");
                    }
                    const Tina::Asset::AssetHandle texture = loadedAsset(
                        dependency->assetId, Tina::AssetFormat::AssetKind::Texture2D);
                    if (!texture) {
                        return Tina::Core::failure(
                            Tina::Core::CoreErrorCode::Internal,
                            "Catalog Material Texture2D dependency was not loaded");
                    }
                    if (!containsHandle(materialTextureAssets, texture)) {
                        materialTextureAssets.push_back(texture);
                    }
                }
            }
        }
        if (!meshAssets.empty() || !materialAssets.empty()) {
            auto registry = Tina::Asset::Mesh3DBindingRegistry::Create(
                *assetResources_.system, *device,
                Tina::Asset::Mesh3DBindingRegistryConfig{
                    .meshCapacity = (std::max)(std::size_t{1}, meshAssets.size()),
                    .materialCapacity = (std::max)(std::size_t{1}, materialAssets.size()),
                    .textureCapacity = (std::max)(std::size_t{1}, materialTextureAssets.size()),
                    .memoryResource = &assetResources_.memory,
                });
            if (!registry) {
                return Tina::Core::failure(std::move(registry.error()));
            }
            mesh3DBindings_.emplace(std::move(*registry));
            for (const Tina::Asset::AssetHandle textureAsset : materialTextureAssets) {
                const Tina::Asset::CookedAssetFile* textureFile =
                    assetResources_.system->tryGet(textureAsset);
                if (textureFile == nullptr) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::Internal,
                        "Catalog material Texture2D is unavailable");
                }
                auto texture = Tina::Asset::uploadTexture2DFromCooked(*device, *textureFile);
                if (!texture) {
                    return Tina::Core::failure(std::move(texture.error()));
                }
                Tina::Render::GpuTextureId gpuTexture = *texture;
                auto textureCleanup = Tina::Core::makeScopeExit([device, &gpuTexture]() noexcept {
                    if (gpuTexture) {
                        (void)device->destroyTexture2D(gpuTexture);
                    }
                });
                if (auto status = mesh3DBindings_->registerMaterialTexture(textureAsset, gpuTexture);
                    !status) {
                    return status;
                }
                textureCleanup.release();
                ++counters_.catalogGpuTextures;
            }
            for (const Tina::Asset::AssetHandle meshAsset : meshAssets) {
                const Tina::Asset::CookedAssetFile* meshFile =
                    assetResources_.system->tryGet(meshAsset);
                if (meshFile == nullptr) {
                    return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                               "Catalog StaticMesh payload is unavailable");
                }
                auto mesh = Tina::Asset::uploadStaticMeshFromCooked(*device, *meshFile);
                if (!mesh) {
                    return Tina::Core::failure(std::move(mesh.error()));
                }
                Tina::Render::GpuMeshId gpuMesh = *mesh;
                auto meshCleanup = Tina::Core::makeScopeExit([device, &gpuMesh]() noexcept {
                    if (gpuMesh) {
                        (void)device->destroyStaticMesh(gpuMesh);
                    }
                });
                auto binding = mesh3DBindings_->registerMeshBinding(meshAsset, gpuMesh);
                if (!binding) {
                    return Tina::Core::failure(std::move(binding.error()));
                }
                meshCleanup.release();
                boundMeshAssets_.push_back(meshAsset);
                ++counters_.catalogGpuMeshes;
                ++counters_.catalogMeshBindings;
            }
            for (const Tina::Asset::AssetHandle materialAsset : materialAssets) {
                auto binding = mesh3DBindings_->registerMaterialBinding(materialAsset);
                if (!binding) {
                    return Tina::Core::failure(std::move(binding.error()));
                }
                boundMaterialAssets_.push_back(materialAsset);
                ++counters_.catalogMaterialBindings;
            }
        }

        counters_.catalogReady = true;
        counters_.projectCatalogConfigured = assetResources_.projectCatalogConfigured;
        counters_.builtInPreviewCatalog = assetResources_.builtInPreviewCatalog;
        counters_.catalogEntryCount = assetResources_.catalogEntryCount;
        return Tina::Core::success();
    }

    void releasePreviewAssetBindings() noexcept
    {
        previewWorld_.reset();
        previewTileMap_.reset();
        previewTileMapLayerIds_.clear();
        previewTilesetAsset_ = {};
        if (mesh3DBindings_.has_value()) {
            if (auto status = mesh3DBindings_->retireAllBindings(); !status) {
                std::terminate();
            }
            mesh3DBindings_.reset();
        }
        if (spriteBindings_.has_value()) {
            if (auto status = spriteBindings_->retireAllTextureBindings(); !status) {
                std::terminate();
            }
            spriteBindings_.reset();
        }
        loadedPreviewHandles_.clear();
        boundSpriteAssets_.clear();
        boundTilesetAssets_.clear();
        boundMeshAssets_.clear();
        boundMaterialAssets_.clear();
    }

    [[nodiscard]] Tina::Core::Status validateRuntimePreview()
    {
        if (workspaceMode_ == WorkspaceMode::World3D) {
            return validateWorld3DRuntimePreview();
        }
        counters_.runtimePreviewValid = false;
        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto animationFrame = spriteAnimationDocument_.frameAt(
            animationSelectedFrameIndex_);
        if (!animationFrame) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Animation preview selected frame is invalid");
        }
        const Tina::Asset::AssetHandle animationSprite = loadedAsset(
            animationFrame->spriteId, Tina::AssetFormat::AssetKind::Sprite);
        animationPreviewAvailable_ = animationSprite &&
                                     containsHandle(boundSpriteAssets_, animationSprite);
        if (animationPreviewAvailable_) {
            const auto player = std::find_if(storage.begin(), storage.end(), [](const auto& entity) {
                return entity.stableEntityId == 3U;
            });
            if (player != storage.end() && player->sprite.has_value()) {
                player->sprite->spriteId = animationFrame->spriteId;
            }
        }
        u64 resolvedSpriteCount = 0;
        for (auto& entity : storage) {
            if (!entity.sprite.has_value()) {
                continue;
            }
            const Tina::Asset::AssetHandle sprite = loadedAsset(
                entity.sprite->spriteId, Tina::AssetFormat::AssetKind::Sprite);
            const Tina::Asset::AssetHandle normalTexture = loadedAsset(
                entity.sprite->normalTextureId, Tina::AssetFormat::AssetKind::Texture2D);
            const bool spriteResolved = sprite && containsHandle(boundSpriteAssets_, sprite);
            const bool normalResolved = !entity.sprite->normalTextureId.hasValue() ||
                                        (normalTexture && spriteBindings_.has_value() &&
                                         spriteBindings_->bindingKey(normalTexture) != 0);
            if (!spriteResolved || !normalResolved) {
                entity.sprite.reset();
                continue;
            }
            ++resolvedSpriteCount;
        }
        const Tina::AssetFormat::World2DSnapshotView previewSnapshot{
            .schemaVersion = snapshot->schemaVersion,
            .entities = storage,
            .gameplaySchema = snapshot->gameplaySchema,
            .gameplayVersion = snapshot->gameplayVersion,
            .gameplayBytes = snapshot->gameplayBytes,
        };
        auto world = Tina::Scene::World::Create({.entityCapacity = AuthoringEntityCapacity});
        if (!world) {
            return Tina::Core::failure(std::move(world.error()));
        }
        auto bindings = Tina::Scene::instantiateWorld2DSnapshot(
            *world, previewSnapshot,
            Tina::Scene::World2DSnapshotAssetResolver{
                .resolveSprite = [this](Tina::Core::AssetId assetId) {
                    return loadedAsset(assetId, Tina::AssetFormat::AssetKind::Sprite);
                },
                .resolveTexture = [this](Tina::Core::AssetId assetId) {
                    return loadedAsset(assetId, Tina::AssetFormat::AssetKind::Texture2D);
                },
            });
        if (!bindings) {
            return Tina::Core::failure(std::move(bindings.error()));
        }
        if (bindings->size() != document_.entityCount() || world->entityCount() != document_.entityCount()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor runtime preview entity count mismatch");
        }
        constexpr std::array requiredHierarchyKeys{
            CameraKey,
            PlayerKey,
        };
        for (const UI::UITreeViewItemKey hierarchyKey : requiredHierarchyKeys) {
            const u32 stableEntityId = stableEntityIdForHierarchyItem(hierarchyKey);
            const auto binding = std::find_if(
                bindings->begin(), bindings->end(),
                [stableEntityId](const Tina::Scene::World2DEntityBinding& candidate) {
                    return candidate.stableEntityId == stableEntityId;
                });
            if (binding == bindings->end() || world->worldTransform(binding->entity) == nullptr) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "editor runtime preview is missing a required transform");
            }
        }
        const auto player = std::find_if(bindings->begin(), bindings->end(), [](const auto& binding) {
            return binding.stableEntityId == 3U;
        });
        if (player == bindings->end()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor runtime preview is missing the Player entity");
        }
        const Tina::Scene::LocalTransform* playerTransform = world->localTransform(player->entity);
        if (playerTransform == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor runtime preview is missing the Player transform");
        }

        counters_.tileMapDocumentRevision = tileMapDocument_.revision();
        counters_.tileMapLayerCount = tileMapDocument_.layerCount();
        counters_.tileMapChunkCount = tileMapDocument_.chunkCount();
        counters_.tileMapAuthoredCells = tileMapDocument_.nonEmptyCellCount();
        auto tileMapRoot = Tina::AssetFormat::parseTileMapPayload(
            tileMapDocument_.rootPayloadBytes());
        if (!tileMapRoot) {
            return Tina::Core::failure(std::move(tileMapRoot.error()));
        }
        tileMapWidthCells_ = tileMapRoot->widthCells;
        tileMapHeightCells_ = tileMapRoot->heightCells;
        counters_.tileMapCookArtifacts = 0;
        counters_.tileMapCookPreviewBytes = 0;
        auto cookPreview = tileMapDocument_.cookPreview();
        if (!cookPreview) {
            return Tina::Core::failure(std::move(cookPreview.error()));
        }
        counters_.tileMapCookArtifacts = cookPreview->artifacts.size();
        for (const auto& artifact : cookPreview->artifacts) {
            counters_.tileMapCookPreviewBytes += artifact.cookedBytes.size();
        }
        auto animationCookPreview = spriteAnimationDocument_.cookPreview();
        if (!animationCookPreview) {
            return Tina::Core::failure(std::move(animationCookPreview.error()));
        }
        counters_.animationDocumentRevision = spriteAnimationDocument_.revision();
        counters_.animationFrameCount = spriteAnimationDocument_.frameCount();
        counters_.animationCookPreviewBytes = animationCookPreview->cookedBytes.size();
        counters_.animationPreviewFrameIndex = animationSelectedFrameIndex_;

        previewTileMap_.reset();
        previewTileMapLayerIds_.clear();
        previewTilesetAsset_ = {};
        const Tina::Asset::AssetHandle tilesetAsset = loadedAsset(
            tileMapDocument_.tilesetId(), Tina::AssetFormat::AssetKind::Tileset);
        if (tilesetAsset && containsHandle(boundTilesetAssets_, tilesetAsset) &&
            assetResources_.system.has_value()) {
            const Tina::Asset::CookedAssetFile* tilesetFile =
                assetResources_.system->tryGet(tilesetAsset);
            if (tilesetFile == nullptr) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "Catalog Tileset payload is unavailable");
            }
            auto tileSet = Tina::AssetFormat::parseTilesetPayload(tilesetFile->payload());
            if (!tileSet) {
                return Tina::Core::failure(std::move(tileSet.error()));
            }
            auto map = Tina::Asset::TileMapInstance::Create(
                *tileMapRoot, *tileSet, tileMapDocument_.tileMapId(),
                tileMapDocument_.tilesetId(),
                Tina::Asset::TileMapInstanceConfig{
                    .residentChunkCapacity =
                        (std::max)(Tina::Core::usize{1}, tileMapDocument_.chunkCount()),
                    .memoryResource = &assetResources_.memory,
                });
            if (!map) {
                return Tina::Core::failure(std::move(map.error()));
            }
            for (Tina::Core::usize chunkIndex = 0;
                 chunkIndex < tileMapDocument_.chunkCount(); ++chunkIndex) {
                const auto chunkPayload = tileMapDocument_.chunkPayloadAt(chunkIndex);
                if (!chunkPayload) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::Internal,
                        "TileMap authoring chunk view disappeared during preview build");
                }
                auto chunk = Tina::AssetFormat::parseTileMapChunkPayload(
                    chunkPayload->payloadBytes);
                if (!chunk) {
                    return Tina::Core::failure(std::move(chunk.error()));
                }
                if (auto status = map->attachChunk(
                        chunkPayload->assetId, *chunk,
                        static_cast<u64>(chunkIndex + 1U)); !status) {
                    return status;
                }
            }
            previewTileMapLayerIds_.reserve(tileMapRoot->layerCount);
            for (Tina::Core::u16 layerIndex = 0; layerIndex < tileMapRoot->layerCount;
                 ++layerIndex) {
                const auto layer = tileMapRoot->layerAt(layerIndex);
                if (layer && layer->kind == Tina::AssetFormat::TileMapLayerKind::Tile) {
                    previewTileMapLayerIds_.push_back(layer->stableLayerId);
                }
            }
            previewTileMap_.emplace(std::move(*map));
            previewTilesetAsset_ = tilesetAsset;
        }

        ++counters_.runtimePreviewInstantiations;
        counters_.documentRevision = document_.revision();
        counters_.documentEntityCount = document_.entityCount();
        counters_.documentUndoDepth = document_.undoDepth();
        counters_.documentRedoDepth = document_.redoDepth();
        counters_.cookPreviewBytes = document_.snapshotBytes().size();
        counters_.finalPlayerPositionX = playerTransform->position.x;
        counters_.finalPlayerPositionY = playerTransform->position.y;
        counters_.finalPlayerPositionZ = playerTransform->position.z;
        const EulerDegrees playerRotation = eulerDegreesFromQuaternion(
            playerTransform->rotation.x, playerTransform->rotation.y, playerTransform->rotation.z,
            playerTransform->rotation.w);
        counters_.finalPlayerRotationXDegrees = playerRotation.x;
        counters_.finalPlayerRotationYDegrees = playerRotation.y;
        counters_.finalPlayerRotationDegrees = playerRotation.z;
        counters_.finalPlayerScaleX = playerTransform->scale.x;
        counters_.finalPlayerScaleY = playerTransform->scale.y;
        counters_.finalPlayerScaleZ = playerTransform->scale.z;
        previewWorld_.emplace(std::move(*world));
        previewBindings_ = std::move(*bindings);
        preview3DBindings_.clear();
        previewCamera3D_ = {};
        previewRevision_ = document_.revision();
        previewResolvedSpriteCount_ = resolvedSpriteCount;
        counters_.catalogResolved2DSprites = resolvedSpriteCount;
        counters_.world2DWorkspaceReady = true;
        counters_.runtimePreviewValid = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status validateWorld3DRuntimePreview()
    {
        counters_.runtimePreviewValid = false;
        std::vector<Tina::AssetFormat::PrefabNodeView> nodeStorage;
        auto prefab = document3D_.parseCurrentPrefab(nodeStorage);
        if (!prefab) {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        u64 resolvedMeshCount = 0;
        for (auto& node : nodeStorage) {
            if (!node.hasMesh) {
                continue;
            }
            const Tina::Asset::AssetHandle mesh = loadedAsset(
                node.meshId, Tina::AssetFormat::AssetKind::StaticMesh);
            const Tina::Asset::AssetHandle material = loadedAsset(
                node.materialId, Tina::AssetFormat::AssetKind::Material);
            if (!mesh || !material || !containsHandle(boundMeshAssets_, mesh) ||
                !containsHandle(boundMaterialAssets_, material)) {
                node.hasMesh = false;
                node.hasMaterial = false;
                node.meshId = {};
                node.materialId = {};
                continue;
            }
            ++resolvedMeshCount;
        }
        auto world = Tina::Scene::World::Create({.entityCapacity = AuthoringEntityCapacity});
        if (!world) {
            return Tina::Core::failure(std::move(world.error()));
        }
        auto entities = Tina::Scene::instantiatePrefab(
            *world, *prefab,
            Tina::Scene::PrefabMeshBinding{
                .localBounds = {.radius = 1.75F},
                .baseColorFactor = {.red = 0.26F, .green = 0.68F, .blue = 0.92F,
                                    .alpha = 1.0F},
                .resolveMesh = [this](Tina::Core::AssetId assetId) {
                    return loadedAsset(assetId, Tina::AssetFormat::AssetKind::StaticMesh);
                },
                .resolveMaterial = [this](Tina::Core::AssetId assetId) {
                    return loadedAsset(assetId, Tina::AssetFormat::AssetKind::Material);
                },
            });
        if (!entities) {
            return Tina::Core::failure(std::move(entities.error()));
        }
        if (entities->size() != nodeStorage.size() ||
            world->entityCount() != document3D_.nodeCount()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor World3D preview node count mismatch");
        }

        std::vector<World3DPreviewBinding> bindings;
        try {
            bindings.reserve(nodeStorage.size());
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "editor World3D preview binding allocation failed");
        }
        Tina::Scene::EntityId cameraEntity{};
        Tina::Scene::EntityId heroEntity{};
        for (u32 index = 0; index < nodeStorage.size(); ++index) {
            const auto& node = nodeStorage[index];
            const Tina::Scene::EntityId entity = (*entities)[index];
            bindings.push_back({.stableNodeId = node.stableNodeId, .entity = entity});
            if (node.stableNodeId == CameraKey) {
                cameraEntity = entity;
            }
            if (node.stableNodeId == PlayerKey) {
                heroEntity = entity;
            }
            if (node.hasMesh) {
                Tina::Render::RenderLinearColor color{
                    .red = 0.26F,
                    .green = 0.68F,
                    .blue = 0.92F,
                    .alpha = 1.0F,
                };
                if (node.stableNodeId == LightsKey) {
                    color = {.red = 0.91F, .green = 0.42F, .blue = 0.30F, .alpha = 1.0F};
                } else if (node.stableNodeId == TileMapKey) {
                    color = {.red = 0.31F, .green = 0.82F, .blue = 0.49F, .alpha = 1.0F};
                }
                if (auto status = world->setMeshRenderer3D(
                        entity,
                        Tina::Scene::MeshRenderer3D{
                            .mesh = loadedAsset(node.meshId,
                                                Tina::AssetFormat::AssetKind::StaticMesh),
                            .material = loadedAsset(node.materialId,
                                                    Tina::AssetFormat::AssetKind::Material),
                            .localBounds = {.radius = 1.75F},
                            .baseColorFactor = color,
                            .visible = node.visible,
                        });
                    !status) {
                    return status;
                }
            }
        }
        if (!cameraEntity.hasValue() || !heroEntity.hasValue()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor World3D preview is missing camera or hero node");
        }
        if (auto status = world->setPerspectiveCamera3D(
                cameraEntity,
                Tina::Scene::PerspectiveCamera3D{
                    .verticalFovDegrees = 55.0F,
                    .nearPlaneMeters = 0.1F,
                    .farPlaneMeters = 100.0F,
                    .normalizedViewport = viewportNormalized_.value_or(
                        Tina::Render::RenderNormalizedViewport{}),
                    .active = true,
                });
            !status) {
            return status;
        }
        if (auto status = world->updateWorldTransforms(); !status) {
            return status;
        }
        const Tina::Scene::LocalTransform* heroTransform = world->localTransform(heroEntity);
        if (heroTransform == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor World3D preview hero transform is missing");
        }

        ++counters_.runtimePreviewInstantiations;
        counters_.documentRevision = document3D_.revision();
        counters_.documentEntityCount = document3D_.nodeCount();
        counters_.documentUndoDepth = document3D_.undoDepth();
        counters_.documentRedoDepth = document3D_.redoDepth();
        counters_.cookPreviewBytes = document3D_.payloadBytes().size();
        counters_.finalPlayerPositionX = heroTransform->position.x;
        counters_.finalPlayerPositionY = heroTransform->position.y;
        counters_.finalPlayerPositionZ = heroTransform->position.z;
        const EulerDegrees heroRotation = eulerDegreesFromQuaternion(
            heroTransform->rotation.x, heroTransform->rotation.y,
            heroTransform->rotation.z, heroTransform->rotation.w);
        counters_.finalPlayerRotationXDegrees = heroRotation.x;
        counters_.finalPlayerRotationYDegrees = heroRotation.y;
        counters_.finalPlayerRotationDegrees = heroRotation.z;
        counters_.finalPlayerScaleX = heroTransform->scale.x;
        counters_.finalPlayerScaleY = heroTransform->scale.y;
        counters_.finalPlayerScaleZ = heroTransform->scale.z;
        previewWorld_.emplace(std::move(*world));
        previewBindings_.clear();
        preview3DBindings_ = std::move(bindings);
        previewCamera3D_ = cameraEntity;
        previewRevision_ = document3D_.revision();
        previewResolvedMeshCount_ = resolvedMeshCount;
        counters_.catalogResolved3DMeshes = resolvedMeshCount;
        counters_.world3DWorkspaceReady = true;
        counters_.runtimePreviewValid = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status refreshAuthoringUi(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        publishWorkspaceSessionCounters();
        const bool dirty = counters_.documentDirty;
        const bool pathConfigured = counters_.documentPathConfigured;
        const bool selectionEditable = stableEntityIdForHierarchyItem(selectionKey_) != 0U &&
                                       !tileMapEditingContext();
        const bool selectionEditable3D = selectionEditable && workspaceMode_ == WorkspaceMode::World3D;

        if (auto status = refreshWorkspaceChrome(tree); !status) {
            return status;
        }
        if (auto status = refreshViewportToolUi(tree); !status) {
            return status;
        }
        if (auto status = refreshAnimationTimelineUi(tree); !status) {
            return status;
        }
        if (auto status = publishInspector(tree, selectionKey_); !status) {
            return status;
        }
        std::string documentStatus = "Revision ";
        documentStatus += std::to_string(activeDocumentRevision());
        documentStatus += " | Undo ";
        documentStatus += std::to_string(activeUndoDepth());
        documentStatus += " | Redo ";
        documentStatus += std::to_string(activeRedoDepth());
        documentStatus += pathConfigured ? (dirty ? " | Modified" : " | Saved") : " | Unsaved";
        if (auto status = tree.setText(inspectorDocument_, documentStatus); !status) {
            return status;
        }
        std::string statusDocument = workspaceMode_ == WorkspaceMode::World2D
                                         ? "World2D v1  |  "
                                         : "Prefab v2  |  ";
        statusDocument += std::to_string(activeDocumentItemCount());
        statusDocument += workspaceMode_ == WorkspaceMode::World2D
                              ? " entities  |  Revision "
                              : " nodes  |  Revision ";
        statusDocument += std::to_string(activeDocumentRevision());
        statusDocument += pathConfigured ? (dirty ? "  |  Modified" : "  |  Saved") : "  |  Unsaved";
        if (auto status = tree.setText(statusDocument_, statusDocument); !status) {
            return status;
        }
        const std::string_view pathStatus = pathConfigured
                                                ? (dirty ? "Save target configured | Modified"
                                                         : "Save target configured | Saved")
                                                : "No save path | Unsaved";
        if (auto status = tree.setText(toolbarPath_, pathStatus); !status) {
            return status;
        }
        std::string statusPreview = "Runtime preview: ";
        statusPreview += counters_.runtimePreviewValid ? "valid" : "invalid";
        statusPreview += "  |  Cook ";
        statusPreview += std::to_string(activeDocumentBytes().size());
        statusPreview += " B  |  Catalog ";
        statusPreview += assetResources_.projectCatalogConfigured ? "project" : "built-in";
        statusPreview += "  |  Resolved ";
        statusPreview += std::to_string(workspaceMode_ == WorkspaceMode::World2D
                                            ? previewResolvedSpriteCount_
                                            : previewResolvedMeshCount_);
        if (workspaceMode_ == WorkspaceMode::World2D) {
            statusPreview += " + ";
            statusPreview += std::to_string(counters_.tileMapEmittedSprites);
            statusPreview += " tiles";
        }
        if (auto status = tree.setText(statusPreview_, statusPreview); !status) {
            return status;
        }
        std::string selectionSummary = "Selected: ";
        selectionSummary += hierarchyLabel(workspaceMode_, selectionKey_);
        const u32 selectedEntityId = stableEntityIdForHierarchyItem(selectionKey_);
        if (selectedEntityId != 0U) {
            selectionSummary += "  |  ID ";
            selectionSummary += std::to_string(selectedEntityId);
        }
        if (auto status = tree.setText(hierarchySelectionSummary_, selectionSummary); !status) {
            return status;
        }
        std::string statusSelection = "Selected: ";
        statusSelection += hierarchyLabel(workspaceMode_, selectionKey_);
        if (auto status = tree.setText(statusSelection_, statusSelection); !status) {
            return status;
        }
        if (auto status = tree.setText(authoringHint_, authoringFeedback_); !status) {
            return status;
        }
        auto tileMapRoot = Tina::AssetFormat::parseTileMapPayload(
            tileMapDocument_.rootPayloadBytes());
        if (!tileMapRoot) {
            return Tina::Core::failure(std::move(tileMapRoot.error()));
        }
        std::string tileMapStatus = std::to_string(tileMapRoot->widthCells);
        tileMapStatus += " x ";
        tileMapStatus += std::to_string(tileMapRoot->heightCells);
        tileMapStatus += " | Layers ";
        tileMapStatus += std::to_string(tileMapDocument_.layerCount());
        tileMapStatus += " | Chunks ";
        tileMapStatus += std::to_string(tileMapDocument_.chunkCount());
        tileMapStatus += " | Cells ";
        tileMapStatus += std::to_string(tileMapDocument_.nonEmptyCellCount());
        tileMapStatus += " | Rev ";
        tileMapStatus += std::to_string(tileMapDocument_.revision());
        if (auto status = tree.setText(tileMapStatus_, tileMapStatus); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorPositionX_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorPositionY_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorPositionZ_, selectionEditable3D); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorRotationX_, selectionEditable3D); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorRotationY_, selectionEditable3D); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorRotationZ_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorScaleX_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorScaleY_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(inspectorScaleZ_, selectionEditable3D); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(applyTransformButton_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(moveButton_, selectionEditable); !status) {
            return status;
        }
        const bool tileMapControlsEnabled = tileMapEditingContext();
        if (auto status = tree.setEnabled(paintTileButton_, tileMapControlsEnabled); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(eraseTileButton_, tileMapControlsEnabled); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(toggleTileLayerButton_, tileMapControlsEnabled); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(addTileLayerButton_, tileMapControlsEnabled); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(addObjectLayerButton_, tileMapControlsEnabled); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(cookTileMapButton_, tileMapControlsEnabled); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(undoButton_, activeCanUndo()); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(redoButton_, activeCanRedo()); !status) {
            return status;
        }
        return tree.setEnabled(saveButton_, pathConfigured && dirty);
    }

    [[nodiscard]] Tina::Core::Status publishInspector(Tina::PrimaryWindowUITreeUpdater& tree,
                                                      UI::UITreeViewItemKey key)
    {
        std::string name = "Name: ";
        name += hierarchyLabel(workspaceMode_, key);
        if (auto status = tree.setText(inspectorName_, name); !status) {
            return status;
        }
        std::string kind = "Kind: ";
        kind += hierarchyKind(workspaceMode_, key);
        if (auto status = tree.setText(inspectorKind_, kind); !status) {
            return status;
        }
        std::string note = "Note: ";
        note += hierarchyAuthoringNote(workspaceMode_, key);
        if (workspaceMode_ == WorkspaceMode::World2D && key == TileMapKey) {
            note += " Layers=";
            note += std::to_string(tileMapDocument_.layerCount());
            note += ", chunks=";
            note += std::to_string(tileMapDocument_.chunkCount());
            note += ", cells=";
            note += std::to_string(tileMapDocument_.nonEmptyCellCount());
        }
        float positionX = 0.0F;
        float positionY = 0.0F;
        float positionZ = 0.0F;
        EulerDegrees rotationDegrees{};
        float scaleX = 1.0F;
        float scaleY = 1.0F;
        float scaleZ = 1.0F;
        bool hasEntity = false;
        const u32 stableEntityId = workspaceMode_ == WorkspaceMode::World2D && key == TileMapKey
                                       ? 0U
                                       : stableEntityIdForHierarchyItem(key);
        if (stableEntityId != 0U) {
            if (workspaceMode_ == WorkspaceMode::World2D) {
                std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
                auto snapshot = document_.parseCurrentSnapshot(storage);
                if (!snapshot) {
                    return Tina::Core::failure(std::move(snapshot.error()));
                }
                const auto entity = std::find_if(
                    storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
                        return candidate.stableEntityId == stableEntityId;
                    });
                if (entity != storage.end()) {
                    hasEntity = true;
                    positionX = entity->positionX;
                    positionY = entity->positionY;
                    positionZ = entity->positionZ;
                    rotationDegrees = eulerDegreesFromQuaternion(
                        entity->rotationX, entity->rotationY, entity->rotationZ,
                        entity->rotationW);
                    scaleX = entity->scaleX;
                    scaleY = entity->scaleY;
                    scaleZ = entity->scaleZ;
                    note += " X=";
                    note += std::to_string(entity->positionX);
                }
            } else {
                std::vector<Tina::AssetFormat::PrefabNodeView> storage;
                auto prefab = document3D_.parseCurrentPrefab(storage);
                if (!prefab) {
                    return Tina::Core::failure(std::move(prefab.error()));
                }
                const auto node = std::find_if(
                    storage.begin(), storage.end(), [stableEntityId](const auto& candidate) {
                        return candidate.stableNodeId == stableEntityId;
                    });
                if (node != storage.end()) {
                    hasEntity = true;
                    positionX = node->positionX;
                    positionY = node->positionY;
                    positionZ = node->positionZ;
                    rotationDegrees = eulerDegreesFromQuaternion(
                        node->rotationX, node->rotationY, node->rotationZ,
                        node->rotationW);
                    scaleX = node->scaleX;
                    scaleY = node->scaleY;
                    scaleZ = node->scaleZ;
                    note += " XYZ=";
                    note += std::to_string(node->positionX);
                    note += ",";
                    note += std::to_string(node->positionY);
                    note += ",";
                    note += std::to_string(node->positionZ);
                }
            }
        }
        if (auto status = tree.setText(inspectorNote_, note); !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorPositionX_, hasEntity ? std::to_string(positionX) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorPositionY_, hasEntity ? std::to_string(positionY) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorPositionZ_, hasEntity ? std::to_string(positionZ) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorRotationX_,
                                       hasEntity ? std::to_string(rotationDegrees.x) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorRotationY_,
                                       hasEntity ? std::to_string(rotationDegrees.y) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorRotationZ_,
                                       hasEntity ? std::to_string(rotationDegrees.z) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorScaleX_, hasEntity ? std::to_string(scaleX) : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorScaleY_, hasEntity ? std::to_string(scaleY) : "n/a");
            !status) {
            return status;
        }
        return tree.setText(inspectorScaleZ_, hasEntity ? std::to_string(scaleZ) : "n/a");
    }

    [[nodiscard]] UI::UITreeViewDataSource hierarchyDataSource() noexcept
    {
        return UI::UITreeViewDataSource{
            .state = this,
            .itemCount = &EditorWorkspaceState::hierarchyItemCount,
            .resolveItem = &EditorWorkspaceState::resolveHierarchyItem,
            .setItemExpanded = &EditorWorkspaceState::setHierarchyExpanded,
        };
    }

    [[nodiscard]] static u64 hierarchyItemCount(const void* state) noexcept
    {
        const auto* self = static_cast<const EditorWorkspaceState*>(state);
        u64 count = 1;
        if (!self->sceneExpanded_) {
            return count;
        }
        count += 2;
        if (self->playerExpanded_) {
            count += 2;
        }
        count += 2;
        return count;
    }

    static bool resolveHierarchyItem(const void* state, u64 logicalIndex,
                                     UI::UITreeViewItemDescriptor& output) noexcept
    {
        const auto* self = static_cast<const EditorWorkspaceState*>(state);
        u64 cursor = 0;
        const auto emit = [&](UI::UITreeViewItemKey key, u32 level, bool expandable, bool expanded) noexcept {
            if (cursor++ != logicalIndex) {
                return false;
            }
            output = UI::UITreeViewItemDescriptor{
                .key = key,
                .label = hierarchyLabel(self->workspaceMode_, key),
                .level = level,
                .enabled = true,
                .expandable = expandable,
                .expanded = expanded,
            };
            return true;
        };
        if (emit(SceneRootKey, 0, true, self->sceneExpanded_)) {
            return true;
        }
        if (!self->sceneExpanded_) {
            return false;
        }
        if (emit(CameraKey, 1, false, false)) {
            return true;
        }
        if (emit(PlayerKey, 1, true, self->playerExpanded_)) {
            return true;
        }
        if (self->playerExpanded_) {
            if (emit(PlayerSpriteKey, 2, false, false)) {
                return true;
            }
            if (emit(PlayerTransformKey, 2, false, false)) {
                return true;
            }
        }
        if (emit(LightsKey, 1, false, false)) {
            return true;
        }
        if (emit(TileMapKey, 1, false, false)) {
            return true;
        }
        return false;
    }

    static bool setHierarchyExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept
    {
        auto* self = static_cast<EditorWorkspaceState*>(state);
        if (key == SceneRootKey) {
            self->sceneExpanded_ = expanded;
            if (!expanded) {
                self->playerExpanded_ = false;
            }
            return true;
        }
        if (key == PlayerKey) {
            if (!self->sceneExpanded_) {
                return false;
            }
            self->playerExpanded_ = expanded;
            return true;
        }
        return false;
    }

    EditorLaunchOptions options_;
    LifecycleCounters& counters_;
    Tina::Editor::World2DAuthoringDocument document_;
    Tina::Editor::World3DAuthoringDocument document3D_;
    Tina::Editor::TileMapAuthoringDocument tileMapDocument_;
    Tina::Editor::SpriteAnimationAuthoringDocument spriteAnimationDocument_;
    WorkspaceMode workspaceMode_ = WorkspaceMode::World2D;
    WorkspaceSessionState world2DSession_{};
    WorkspaceSessionState world3DSession_{};
    EditorAssetResources& assetResources_;
    EditorRenderDeviceAccess& renderDeviceAccess_;
    Tina::UI::UIRootOwner uiRoot_{};
    UI::UINodeId hierarchyTree_{};
    UI::UINodeId inspectorName_{};
    UI::UINodeId inspectorKind_{};
    UI::UINodeId inspectorNote_{};
    UI::UINodeId inspectorDocument_{};
    UI::UINodeId hierarchySelectionSummary_{};
    UI::UINodeId viewportPreviewLayer_{};
    UI::UINodeId inspectorPositionX_{};
    UI::UINodeId inspectorPositionY_{};
    UI::UINodeId inspectorPositionZ_{};
    UI::UINodeId inspectorRotationX_{};
    UI::UINodeId inspectorRotationY_{};
    UI::UINodeId inspectorRotationZ_{};
    UI::UINodeId inspectorScaleX_{};
    UI::UINodeId inspectorScaleY_{};
    UI::UINodeId inspectorScaleZ_{};
    UI::UINodeId authoringHint_{};
    UI::UINodeId tileMapStatus_{};
    UI::UINodeId animationStatus_{};
    UI::UINodeId animationSelection_{};
    UI::UINodeId statusDocument_{};
    UI::UINodeId statusPreview_{};
    UI::UINodeId statusSelection_{};
    UI::UINodeId toolbarPath_{};
    UI::UINodeId toolbarDocument_{};
    UI::UINodeId breadcrumb_{};
    UI::UINodeId viewportTitle_{};
    UI::UINodeId viewportMode_{};
    UI::UINodeId gridStatus_{};
    UI::UINodeId previewTitle_{};
    UI::UINodeId previewEntities_{};
    UI::UINodeId previewCook_{};
    UI::UINodeId previewAssetStatus_{};
    UI::UINodeId cameraStatus_{};
    UI::UINodeId viewportToolStatus_{};
    UI::UINodeId documentFormat_{};
    std::array<UI::UINodeId, 3> componentLabels_{};
    UI::UINodeId mode2DButton_{};
    UI::UINodeId mode3DButton_{};
    std::array<UI::UINodeId, 2> selectToolButtons_{};
    std::array<UI::UINodeId, 2> moveToolButtons_{};
    UI::UINodeId moveButton_{};
    UI::UINodeId tilePaintToolButton_{};
    UI::UINodeId tileEraseToolButton_{};
    UI::UINodeId applyTransformButton_{};
    UI::UINodeId undoButton_{};
    UI::UINodeId redoButton_{};
    UI::UINodeId saveButton_{};
    UI::UINodeId paintTileButton_{};
    UI::UINodeId eraseTileButton_{};
    UI::UINodeId toggleTileLayerButton_{};
    UI::UINodeId addTileLayerButton_{};
    UI::UINodeId addObjectLayerButton_{};
    UI::UINodeId cookTileMapButton_{};
    UI::UINodeId animationModeButton_{};
    UI::UINodeId animationPlayButton_{};
    UI::UINodeId animationCookButton_{};
    UI::UINodeId animationPreviousButton_{};
    UI::UINodeId animationNextButton_{};
    UI::UINodeId animationAddButton_{};
    UI::UINodeId animationDuplicateButton_{};
    UI::UINodeId animationDeleteButton_{};
    UI::UINodeId animationMoveLeftButton_{};
    UI::UINodeId animationMoveRightButton_{};
    UI::UINodeId animationCycleSpriteButton_{};
    UI::UINodeId animationDurationDecreaseButton_{};
    UI::UINodeId animationDurationIncreaseButton_{};
    UI::UINodeId animationUndoButton_{};
    UI::UINodeId animationRedoButton_{};
    std::array<UI::UINodeId, AnimationVisibleFrameSlots> animationFrameButtons_{};
    UI::UIStyleClassId dockClass_{};
    UI::UIStyleClassId viewportClass_{};
    UI::UIStyleTokenId viewportToken_{};
    UI::UITreeViewItemKey selectionKey_ = UI::InvalidUITreeViewItemKey;
    ViewportToolMode viewportToolMode_ = ViewportToolMode::Select;
    ViewportGizmoTransaction viewportGizmo_{};
    std::array<UI::UIRoutedPointerListenerToken, 4> viewportPointerListeners_{};
    bool sceneExpanded_ = true;
    bool playerExpanded_ = true;
    bool queuedFirstSelection_ = false;
    bool queuedSecondSelection_ = false;
    bool pendingAutoTransformInput_ = false;
    u32 autoAuthoringStage_ = 0;
    UI::UILogicalPoint autoGizmoStart_{};
    mutable std::optional<Tina::Scene::World> previewWorld_{};
    mutable std::optional<Tina::Asset::TileMapInstance> previewTileMap_{};
    Tina::Asset::AssetHandle previewTilesetAsset_{};
    std::vector<Tina::AssetFormat::TileMapLayerId> previewTileMapLayerIds_{};
    u32 tileMapWidthCells_ = 0;
    u32 tileMapHeightCells_ = 0;
    Tina::AssetFormat::TileMapLayerId activeTileMapLayerId_ = InitialTileMapLayerId;
    u32 tileBrushX_ = 2;
    u32 tileBrushY_ = 2;
    Tina::Core::u16 selectedTileId_ = 1;
    std::optional<Tina::Editor::TileMapAuthoringCellEdit> lastPaintedTile_{};
    std::optional<Tina::Editor::TileMapAuthoringCellEdit> pendingTileCellEdit_{};
    Tina::AssetFormat::TileMapLayerId pendingTileLayerId_ = 0;
    std::vector<Tina::Scene::World2DEntityBinding> previewBindings_{};
    std::vector<World3DPreviewBinding> preview3DBindings_{};
    Tina::Scene::EntityId previewCamera3D_{};
    std::optional<Tina::Asset::Sprite2DBindingRegistry> spriteBindings_{};
    std::optional<Tina::Asset::Mesh3DBindingRegistry> mesh3DBindings_{};
    std::optional<Tina::Scene::SpriteAnimator2D> animationAnimator_{};
    std::vector<Tina::Asset::AssetHandle> loadedPreviewHandles_{};
    std::vector<Tina::Asset::AssetHandle> boundSpriteAssets_{};
    std::vector<Tina::Asset::AssetHandle> boundTilesetAssets_{};
    std::vector<Tina::Asset::AssetHandle> boundMeshAssets_{};
    std::vector<Tina::Asset::AssetHandle> boundMaterialAssets_{};
    u64 previewResolvedSpriteCount_ = 0;
    u64 previewResolvedMeshCount_ = 0;
    u64 previewRevision_ = 0;
    u32 animationSelectedFrameIndex_ = 0;
    u32 animationVisibleFrameStart_ = 0;
    bool animationPlaying_ = false;
    bool animationPreviewAvailable_ = false;
    bool pendingAnimationTimelineRefresh_ = false;
    UI::UILogicalRect viewportLogicalRect_{};
    std::optional<Tina::Render::RenderNormalizedViewport> viewportNormalized_{};
    u32 surfacePixelWidth_ = WindowLogicalWidth;
    u32 surfacePixelHeight_ = WindowLogicalHeight;
    std::string authoringFeedback_ = "One validated revision per command";
    std::optional<u64> pendingSelectionIndex_{};
    std::optional<EditorCommand> pendingEditorCommand_{};
    std::optional<u32> pendingAnimationFrameSelection_{};
    std::optional<ViewportToolMode> pendingViewportToolMode_{};
    std::optional<UI::UIStraightSrgba8Color> pendingViewportTokenColor_{};
};

class EditorApplication final : public Tina::IGameApplication {
  public:
    EditorApplication(EditorLaunchOptions options, LifecycleCounters& counters,
                      EditorAssetResources& assetResources,
                      EditorRenderDeviceAccess& renderDeviceAccess) noexcept
        : options_(options), counters_(counters), assetResources_(assetResources),
          renderDeviceAccess_(renderDeviceAccess)
    {
    }

    Tina::Core::Result<std::unique_ptr<Tina::IGameState>> createInitialState(Tina::GameStartupContext&) override
    {
        auto initialDocuments = createAuthoringDocuments(options_);
        if (!initialDocuments) {
            return Tina::Core::failure(std::move(initialDocuments.error()));
        }
        try {
            std::unique_ptr<Tina::IGameState> state =
                std::make_unique<EditorWorkspaceState>(
                    options_, counters_, std::move(initialDocuments->world2D),
                    std::move(initialDocuments->world3D),
                    std::move(initialDocuments->tileMap),
                    std::move(initialDocuments->spriteAnimation),
                    std::move(initialDocuments->world2DSession),
                    std::move(initialDocuments->world3DSession), assetResources_,
                    renderDeviceAccess_);
            return state;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Tina Editor could not create workspace sessions");
        }
    }

    void onShutdown(Tina::GameShutdownContext&) noexcept override
    {
        ++counters_.applicationShutdowns;
    }

  private:
    EditorLaunchOptions options_;
    LifecycleCounters& counters_;
    EditorAssetResources& assetResources_;
    EditorRenderDeviceAccess& renderDeviceAccess_;
};

[[nodiscard]] Tina::EngineConfig createEngineConfig()
{
    Tina::EngineConfig config = Tina::EngineConfig::Defaults();
    config.applicationName = "Tina Editor";
    config.primaryWindow.title = "Tina Editor - 2D / 3D";
    config.primaryWindow.initialLogicalExtent = {WindowLogicalWidth, WindowLogicalHeight};
    config.primaryWindow.initiallyVisible = true;
    config.renderSceneCapacities.spriteCapacity = EditorViewportSpriteCapacity;
    config.renderSceneCapacities.mesh3DItemCapacity = 8;
    config.renderSceneCapacities.mesh3DBatchCapacity = 4;
    return config;
}

[[nodiscard]] std::string runExitReasonName(Tina::RunExitReason exitReason)
{
    switch (exitReason) {
    case Tina::RunExitReason::GameRequestedExitAfterCurrentFrame:
        return "GameRequestedExitAfterCurrentFrame";
    case Tina::RunExitReason::PrimaryWindowRequestedClose:
        return "PrimaryWindowRequestedClose";
    case Tina::RunExitReason::GameStateStackBecameEmpty:
        return "GameStateStackBecameEmpty";
    }
    return "Unknown";
}

[[nodiscard]] Tina::Core::Status verifyLifecycle(Tina::RunExitReason exitReason, const EditorLaunchOptions& options,
                                                 const LifecycleCounters& counters)
{
    const bool world2D = options.initialWorkspace == WorkspaceMode::World2D;
    const bool world2DPathConfigured = !options.world2DDocumentPathUtf8.empty();
    const bool world3DPathConfigured = !options.world3DDocumentPathUtf8.empty();
    const bool projectCatalogConfigured = !options.catalogRootUtf8.empty();
    const bool documentPathConfigured = world2D ? world2DPathConfigured
                                                : world3DPathConfigured;
    const bool activeDocumentLoaded = world2D ? counters.world2DDocumentLoaded
                                              : counters.world3DDocumentLoaded;
    const bool activeDocumentDirty = world2D ? counters.world2DDocumentDirty
                                             : counters.world3DDocumentDirty;
    const u64 activeSavedSnapshotBytes = world2D ? counters.world2DSavedSnapshotBytes
                                                 : counters.world3DSavedSnapshotBytes;
    const auto uneditedSessionMatches = [](bool pathConfigured, bool loaded, bool dirty,
                                           u64 savedBytes) noexcept {
        if (!pathConfigured) {
            return !loaded && dirty && savedBytes == 0;
        }
        return loaded ? !dirty && savedBytes > 0 : dirty && savedBytes == 0;
    };
    const u64 expectedCookBytes =
        world2D
            ? Tina::AssetFormat::World2DSnapshotWire::HeaderBytes +
                  InitialAuthoringEntityCount *
                      Tina::AssetFormat::World2DSnapshotWire::EntityBytes
            : Tina::AssetFormat::PrefabWire::HeaderBytes +
                  InitialAuthoringEntityCount * Tina::AssetFormat::PrefabWire::NodeBytes;
    if (exitReason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Tina Editor stopped for an unexpected reason");
    }
    if (counters.frameUpdates != options.targetFrameCount || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
        counters.uiRootsReleased != 1 || !counters.selectionVerified || counters.hierarchyLogicalItems == 0 ||
        !counters.stylesheetInstalled || counters.styleRegisteredClasses != 2 ||
        counters.styleRegisteredTokens != 2 || counters.styleActiveRules != 2 ||
        counters.authoringActionsWired != EditorActionCount || !counters.runtimePreviewValid ||
        counters.editorLayoutRegions != EditorLayoutRegionCount || !counters.viewportLayoutReady ||
        !counters.gpuViewportReady || !counters.inspectorScrollConfigured ||
        counters.documentPathConfigured != documentPathConfigured ||
        counters.documentLoaded != activeDocumentLoaded ||
        counters.documentDirty != activeDocumentDirty ||
        counters.documentSaved != (documentPathConfigured && !activeDocumentDirty) ||
        counters.savedSnapshotBytes != activeSavedSnapshotBytes ||
        counters.finalWorkspaceWorld2D != world2D ||
        counters.world2DDocumentPathConfigured != world2DPathConfigured ||
        counters.world3DDocumentPathConfigured != world3DPathConfigured ||
        !counters.catalogReady ||
        counters.projectCatalogConfigured != projectCatalogConfigured ||
        counters.builtInPreviewCatalog == projectCatalogConfigured ||
        counters.runtimePreviewInstantiations < 1 || counters.renderExtractions != options.targetFrameCount ||
        (options.targetFrameCount > 1 &&
         (world2D ? counters.gpuViewportSprites !=
                        counters.catalogResolved2DSprites + counters.tileMapEmittedSprites
                  : counters.gpuViewportMeshes != counters.catalogResolved3DMeshes)) ||
        (world2D ? !counters.world2DWorkspaceReady : !counters.world3DWorkspaceReady) ||
        counters.viewportLogicalWidth <= 0.0F || counters.viewportLogicalHeight <= 0.0F ||
        counters.viewportNormalizedX < 0.0F || counters.viewportNormalizedY < 0.0F ||
        counters.viewportNormalizedWidth <= 0.0F || counters.viewportNormalizedHeight <= 0.0F ||
        static_cast<double>(counters.viewportNormalizedX) + counters.viewportNormalizedWidth > 1.0 ||
        static_cast<double>(counters.viewportNormalizedY) + counters.viewportNormalizedHeight > 1.0 ||
        counters.documentEntityCount != InitialAuthoringEntityCount ||
        counters.cookPreviewBytes != expectedCookBytes) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Tina Editor lifecycle counters did not match contract");
    }
    if (!projectCatalogConfigured &&
        (counters.catalogEntryCount != 8 || counters.catalogAssetsLoaded != 7 ||
         counters.catalogGpuTextures != 1 || counters.catalogGpuMeshes != 1 ||
         counters.catalogSpriteBindings != 1 || counters.catalogMeshBindings != 1 ||
         counters.catalogMaterialBindings != 1 || counters.catalogUnresolvedReferences != 0 ||
         ((world2D || options.autoDemo) &&
          counters.catalogResolved2DSprites != GpuViewportSpriteCount) ||
         ((world2D || options.autoDemo) &&
          (counters.tileMapLayerCount != 2 ||
           counters.tileMapChunkCount != InitialTileMapChunkCount ||
           counters.tileMapAuthoredCells != InitialTileMapCellCount ||
           counters.tileMapCookArtifacts != InitialTileMapChunkCount + 1U ||
           counters.tileMapCookPreviewBytes == 0 ||
           counters.tileMapEmittedSprites != InitialTileMapCellCount)) ||
         counters.animationFrameCount != 4 ||
         counters.animationCookPreviewBytes == 0 ||
         counters.animationPreviewFrameIndex >= counters.animationFrameCount ||
         ((!world2D || options.autoDemo) &&
          counters.catalogResolved3DMeshes != GpuViewportMeshCount))) {
        std::string message = "Tina Editor built-in Catalog counters mismatch: entries=";
        message += std::to_string(counters.catalogEntryCount);
        message += ", loaded=";
        message += std::to_string(counters.catalogAssetsLoaded);
        message += ", gpuTextures=";
        message += std::to_string(counters.catalogGpuTextures);
        message += ", gpuMeshes=";
        message += std::to_string(counters.catalogGpuMeshes);
        message += ", spriteBindings=";
        message += std::to_string(counters.catalogSpriteBindings);
        message += ", meshBindings=";
        message += std::to_string(counters.catalogMeshBindings);
        message += ", materialBindings=";
        message += std::to_string(counters.catalogMaterialBindings);
        message += ", unresolved=";
        message += std::to_string(counters.catalogUnresolvedReferences);
        message += ", resolved2D=";
        message += std::to_string(counters.catalogResolved2DSprites);
        message += ", resolved3D=";
        message += std::to_string(counters.catalogResolved3DMeshes);
        message += ", tileLayers=";
        message += std::to_string(counters.tileMapLayerCount);
        message += ", tileChunks=";
        message += std::to_string(counters.tileMapChunkCount);
        message += ", tileCells=";
        message += std::to_string(counters.tileMapAuthoredCells);
        message += ", tileSprites=";
        message += std::to_string(counters.tileMapEmittedSprites);
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   std::move(message));
    }
    if (options.autoDemo) {
        const bool dimensionSpecificTransformMatches =
            world2D
                ? std::abs(counters.finalPlayerPositionX - 4.5F) <= 0.001F &&
                      std::abs(counters.finalPlayerPositionY + 2.25F) <= 0.001F &&
                      counters.finalPlayerPositionZ == 0.0F &&
                      std::abs(counters.finalPlayerRotationXDegrees) <= 0.001F &&
                      std::abs(counters.finalPlayerRotationYDegrees) <= 0.001F &&
                      counters.finalPlayerScaleZ == 1.0F &&
                      std::abs(counters.viewportGizmoWorldDeltaX - 2.0F) <= 0.001F &&
                      std::abs(counters.viewportGizmoWorldDeltaY + 1.0F) <= 0.001F &&
                      std::abs(counters.viewportGizmoWorldDeltaZ) <= 0.001F
                : std::abs(counters.finalPlayerPositionX - 4.5F) <= 0.001F &&
                      std::abs(counters.finalPlayerPositionY + 1.25F) <= 0.001F &&
                      std::abs(counters.finalPlayerPositionZ - 2.5F) <= 0.001F &&
                      std::abs(counters.finalPlayerRotationXDegrees - 15.0F) <= 0.001F &&
                      std::abs(counters.finalPlayerRotationYDegrees - 25.0F) <= 0.001F &&
                      counters.finalPlayerScaleZ == 1.5F &&
                      std::abs(counters.viewportGizmoWorldDeltaX - 2.0F) <= 0.001F &&
                      std::abs(counters.viewportGizmoWorldDeltaY) <= 0.001F &&
                      std::abs(counters.viewportGizmoWorldDeltaZ - 1.0F) <= 0.001F;
        if (counters.hierarchySelectionChanges < 1 || counters.styleTokenUpdates < 2 ||
            counters.authoringEdits != 4 ||
            counters.inspectorTransactions != 1 || counters.inspectorRejectedTransactions != 0 ||
            counters.viewportGizmoBegins != 1 || counters.viewportGizmoPreviews != 2 ||
            counters.viewportGizmoCommits != 1 || counters.viewportGizmoCancels != 0 ||
            counters.viewportGizmoRejects != 0 ||
            counters.authoringUndos != 2 || counters.authoringRedos != 2 ||
            counters.animationEdits != 1 || counters.animationUndos != 1 ||
            counters.animationRedos != 1 || counters.animationDocumentRevision != 4 ||
            counters.animationPreviewFrameIndex != 1 ||
            counters.documentRevision != 7 ||
            counters.gpuViewportDocumentRevision != counters.documentRevision ||
            counters.documentUndoDepth != 3 || counters.documentRedoDepth != 0 ||
            std::abs(counters.finalPlayerRotationDegrees - 30.0F) > 0.001F ||
            counters.finalPlayerScaleX != 1.25F || counters.finalPlayerScaleY != 0.75F ||
            !dimensionSpecificTransformMatches) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Tina Editor automatic authoring demo did not finish");
        }
        if (counters.finalSelectionKey != TileMapKey || counters.finalSelectionIndex != 6) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor automatic hierarchy selection did not finish");
        }
        if (counters.workspaceSwitches != 2) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor workspace round-trip did not execute two mode switches");
        }
        if (counters.runtimePreviewInstantiations != 8 ||
            !counters.world2DWorkspaceReady || !counters.world3DWorkspaceReady) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor workspace round-trip did not validate both runtime previews");
        }
        if (documentPathConfigured) {
            if (counters.authoringSaves != 1 || !counters.documentSaved || counters.documentDirty ||
                counters.savedSnapshotBytes != counters.cookPreviewBytes ||
                activeSavedSnapshotBytes != counters.cookPreviewBytes) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "Tina Editor automatic save did not finish");
            }
        } else if (counters.authoringSaves != 0 || counters.documentSaved || !counters.documentDirty ||
                   counters.savedSnapshotBytes != 0) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Tina Editor reported an unexpected saved document");
        }
        const bool inactiveSessionMatches =
            world2D ? uneditedSessionMatches(
                          world3DPathConfigured, counters.world3DDocumentLoaded,
                          counters.world3DDocumentDirty, counters.world3DSavedSnapshotBytes)
                    : uneditedSessionMatches(
                          world2DPathConfigured, counters.world2DDocumentLoaded,
                          counters.world2DDocumentDirty, counters.world2DSavedSnapshotBytes);
        if (!inactiveSessionMatches) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor changed the inactive workspace session during mode round-trip");
        }
    } else if (!uneditedSessionMatches(
                   world2DPathConfigured, counters.world2DDocumentLoaded,
                   counters.world2DDocumentDirty, counters.world2DSavedSnapshotBytes) ||
               !uneditedSessionMatches(
                   world3DPathConfigured, counters.world3DDocumentLoaded,
                   counters.world3DDocumentDirty, counters.world3DSavedSnapshotBytes)) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Tina Editor initial workspace sessions did not preserve their open baselines");
    }
    return Tina::Core::success();
}

[[nodiscard]] int runEditor(int argumentCount, char** arguments)
{
    auto optionsResult = parseOptions(argumentCount, arguments);
    if (!optionsResult) {
        writeError(optionsResult.error());
        return 2;
    }
    const EditorLaunchOptions options = *optionsResult;

    LifecycleCounters counters{};
    EditorAssetResources assetResources{};
    if (auto status = prepareEditorCatalog(options, assetResources); !status) {
        writeError(status.error());
        return 1;
    }
    EditorRenderDeviceAccess renderDeviceAccess{};
    Tina::Desktop::CreateEngineOptions desktopOptions{};
    desktopOptions.wrapWindowSurfaceRenderDevice =
        [&renderDeviceAccess](std::unique_ptr<Tina::Render::IRenderDevice> device)
            -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
            renderDeviceAccess.set(device.get());
            return device;
        };
    auto hostResult = Tina::Desktop::CreateEngine(createEngineConfig(), std::move(desktopOptions));
    if (!hostResult) {
        writeError(hostResult.error());
        return 1;
    }

    EditorApplication application{options, counters, assetResources, renderDeviceAccess};
    auto runResult = (*hostResult)->run(application);
    if (!runResult) {
        writeError(runResult.error());
        return 1;
    }

    auto lifecycleStatus = verifyLifecycle(*runResult, options, counters);
    if (!lifecycleStatus) {
        writeError(lifecycleStatus.error());
        return 1;
    }

    std::cout << "{\"status\":\"ok\",\"application\":\"TinaEditor\",\"readOnly\":false"
              << ",\"editorModule\":true,\"supports2D\":true,\"supports3D\":true,\"initialWorkspace\":";
    writeJsonString(std::cout, options.initialWorkspace == WorkspaceMode::World2D ? "2d" : "3d");
    std::cout << ",\"finalWorkspace\":";
    writeJsonString(std::cout, counters.finalWorkspaceWorld2D ? "2d" : "3d");
    std::cout << ",\"stylesheetInstalled\":"
              << (counters.stylesheetInstalled ? "true" : "false") << ",\"frames\":" << counters.frameUpdates
              << ",\"targetFrames\":" << options.targetFrameCount
              << ",\"frameDelayMs\":" << options.frameDelayMilliseconds
              << ",\"autoDemo\":" << (options.autoDemo ? "true" : "false") << ",\"exit\":";
    writeJsonString(std::cout, runExitReasonName(*runResult));
    std::cout << ",\"documentPathConfigured\":"
              << (counters.documentPathConfigured ? "true" : "false")
              << ",\"world2DDocumentPath\":";
    writeJsonString(std::cout, options.world2DDocumentPathUtf8);
    std::cout << ",\"world3DDocumentPath\":";
    writeJsonString(std::cout, options.world3DDocumentPathUtf8);
    std::cout << ",\"catalogRoot\":";
    writeJsonString(std::cout, options.catalogRootUtf8);
    std::cout << ",\"documentLoaded\":" << (counters.documentLoaded ? "true" : "false")
              << ",\"world2DDocumentPathConfigured\":"
              << (counters.world2DDocumentPathConfigured ? "true" : "false")
              << ",\"world3DDocumentPathConfigured\":"
              << (counters.world3DDocumentPathConfigured ? "true" : "false")
              << ",\"world2DDocumentLoaded\":"
              << (counters.world2DDocumentLoaded ? "true" : "false")
              << ",\"world3DDocumentLoaded\":"
              << (counters.world3DDocumentLoaded ? "true" : "false")
              << ",\"world2DDocumentDirty\":"
              << (counters.world2DDocumentDirty ? "true" : "false")
              << ",\"world3DDocumentDirty\":"
              << (counters.world3DDocumentDirty ? "true" : "false")
              << ",\"stateEnters\":" << counters.stateEnters << ",\"stateExits\":" << counters.stateExits
              << ",\"applicationShutdowns\":" << counters.applicationShutdowns
              << ",\"uiRootsCreated\":" << counters.uiRootsCreated
              << ",\"uiRootsReleased\":" << counters.uiRootsReleased
              << ",\"hierarchySelectionChanges\":" << counters.hierarchySelectionChanges
              << ",\"hierarchyLogicalItems\":" << counters.hierarchyLogicalItems
              << ",\"styleRegisteredClasses\":" << counters.styleRegisteredClasses
              << ",\"styleRegisteredTokens\":" << counters.styleRegisteredTokens
              << ",\"styleActiveRules\":" << counters.styleActiveRules
              << ",\"styleRevision\":" << counters.styleRevision
              << ",\"styleTokenUpdates\":" << counters.styleTokenUpdates
              << ",\"authoringActionsWired\":" << counters.authoringActionsWired
              << ",\"authoringEdits\":" << counters.authoringEdits
              << ",\"authoringUndos\":" << counters.authoringUndos
              << ",\"authoringRedos\":" << counters.authoringRedos
              << ",\"authoringSaves\":" << counters.authoringSaves
              << ",\"inspectorTransactions\":" << counters.inspectorTransactions
              << ",\"inspectorRejectedTransactions\":" << counters.inspectorRejectedTransactions
              << ",\"viewportGizmoBegins\":" << counters.viewportGizmoBegins
              << ",\"viewportGizmoPreviews\":" << counters.viewportGizmoPreviews
              << ",\"viewportGizmoCommits\":" << counters.viewportGizmoCommits
              << ",\"viewportGizmoCancels\":" << counters.viewportGizmoCancels
              << ",\"viewportGizmoRejects\":" << counters.viewportGizmoRejects
              << ",\"savedSnapshotBytes\":" << counters.savedSnapshotBytes
              << ",\"world2DSavedSnapshotBytes\":" << counters.world2DSavedSnapshotBytes
              << ",\"world3DSavedSnapshotBytes\":" << counters.world3DSavedSnapshotBytes
              << ",\"editorLayoutRegions\":" << counters.editorLayoutRegions
              << ",\"viewportLayoutReady\":" << (counters.viewportLayoutReady ? "true" : "false")
              << ",\"inspectorScrollConfigured\":"
              << (counters.inspectorScrollConfigured ? "true" : "false")
              << ",\"renderExtractions\":" << counters.renderExtractions
              << ",\"gpuViewportSprites\":" << counters.gpuViewportSprites
              << ",\"gpuViewportMeshes\":" << counters.gpuViewportMeshes
              << ",\"catalogReady\":" << (counters.catalogReady ? "true" : "false")
              << ",\"projectCatalogConfigured\":"
              << (counters.projectCatalogConfigured ? "true" : "false")
              << ",\"builtInPreviewCatalog\":"
              << (counters.builtInPreviewCatalog ? "true" : "false")
              << ",\"catalogEntryCount\":" << counters.catalogEntryCount
              << ",\"catalogAssetsLoaded\":" << counters.catalogAssetsLoaded
              << ",\"catalogGpuTextures\":" << counters.catalogGpuTextures
              << ",\"catalogGpuMeshes\":" << counters.catalogGpuMeshes
              << ",\"catalogSpriteBindings\":" << counters.catalogSpriteBindings
              << ",\"catalogMeshBindings\":" << counters.catalogMeshBindings
              << ",\"catalogMaterialBindings\":" << counters.catalogMaterialBindings
              << ",\"catalogUnresolvedReferences\":"
              << counters.catalogUnresolvedReferences
              << ",\"catalogResolved2DSprites\":" << counters.catalogResolved2DSprites
              << ",\"catalogResolved3DMeshes\":" << counters.catalogResolved3DMeshes
              << ",\"tileMapDocumentRevision\":" << counters.tileMapDocumentRevision
              << ",\"tileMapLayerCount\":" << counters.tileMapLayerCount
              << ",\"tileMapChunkCount\":" << counters.tileMapChunkCount
              << ",\"tileMapAuthoredCells\":" << counters.tileMapAuthoredCells
              << ",\"tileMapCookArtifacts\":" << counters.tileMapCookArtifacts
              << ",\"tileMapCookPreviewBytes\":" << counters.tileMapCookPreviewBytes
              << ",\"tileMapEmittedSprites\":" << counters.tileMapEmittedSprites
              << ",\"tileMapEdits\":" << counters.tileMapEdits
              << ",\"tileMapUndos\":" << counters.tileMapUndos
              << ",\"tileMapRedos\":" << counters.tileMapRedos
              << ",\"animationDocumentRevision\":" << counters.animationDocumentRevision
              << ",\"animationFrameCount\":" << counters.animationFrameCount
              << ",\"animationCookPreviewBytes\":" << counters.animationCookPreviewBytes
              << ",\"animationPreviewFrameIndex\":" << counters.animationPreviewFrameIndex
              << ",\"animationEdits\":" << counters.animationEdits
              << ",\"animationUndos\":" << counters.animationUndos
              << ",\"animationRedos\":" << counters.animationRedos
              << ",\"animationPlaybackTransitions\":" << counters.animationPlaybackTransitions
              << ",\"workspaceSwitches\":" << counters.workspaceSwitches
              << ",\"world2DWorkspaceReady\":"
              << (counters.world2DWorkspaceReady ? "true" : "false")
              << ",\"world3DWorkspaceReady\":"
              << (counters.world3DWorkspaceReady ? "true" : "false")
              << ",\"gpuViewportDocumentRevision\":" << counters.gpuViewportDocumentRevision
              << ",\"gpuViewportReady\":" << (counters.gpuViewportReady ? "true" : "false")
              << ",\"viewportLogicalX\":" << counters.viewportLogicalX
              << ",\"viewportLogicalY\":" << counters.viewportLogicalY
              << ",\"viewportLogicalWidth\":" << counters.viewportLogicalWidth
              << ",\"viewportLogicalHeight\":" << counters.viewportLogicalHeight
              << ",\"viewportNormalizedX\":" << counters.viewportNormalizedX
              << ",\"viewportNormalizedY\":" << counters.viewportNormalizedY
              << ",\"viewportNormalizedWidth\":" << counters.viewportNormalizedWidth
              << ",\"viewportNormalizedHeight\":" << counters.viewportNormalizedHeight
              << ",\"viewportGizmoWorldDeltaX\":" << counters.viewportGizmoWorldDeltaX
              << ",\"viewportGizmoWorldDeltaY\":" << counters.viewportGizmoWorldDeltaY
              << ",\"viewportGizmoWorldDeltaZ\":" << counters.viewportGizmoWorldDeltaZ
              << ",\"runtimePreviewInstantiations\":" << counters.runtimePreviewInstantiations
              << ",\"runtimePreviewValid\":" << (counters.runtimePreviewValid ? "true" : "false")
              << ",\"documentRevision\":" << counters.documentRevision
              << ",\"documentEntityCount\":" << counters.documentEntityCount
              << ",\"documentUndoDepth\":" << counters.documentUndoDepth
              << ",\"documentRedoDepth\":" << counters.documentRedoDepth
              << ",\"documentSaved\":" << (counters.documentSaved ? "true" : "false")
              << ",\"documentDirty\":" << (counters.documentDirty ? "true" : "false")
              << ",\"cookPreviewBytes\":" << counters.cookPreviewBytes
              << ",\"finalPlayerPositionX\":" << counters.finalPlayerPositionX
              << ",\"finalPlayerPositionY\":" << counters.finalPlayerPositionY
              << ",\"finalPlayerPositionZ\":" << counters.finalPlayerPositionZ
              << ",\"finalPlayerRotationXDegrees\":" << counters.finalPlayerRotationXDegrees
              << ",\"finalPlayerRotationYDegrees\":" << counters.finalPlayerRotationYDegrees
              << ",\"finalPlayerRotationDegrees\":" << counters.finalPlayerRotationDegrees
              << ",\"finalPlayerScaleX\":" << counters.finalPlayerScaleX
              << ",\"finalPlayerScaleY\":" << counters.finalPlayerScaleY
              << ",\"finalPlayerScaleZ\":" << counters.finalPlayerScaleZ
              << ",\"finalSelectionKey\":" << counters.finalSelectionKey
              << ",\"finalSelectionIndex\":" << counters.finalSelectionIndex
              << ",\"selectionVerified\":" << (counters.selectionVerified ? "true" : "false") << "}\n";
    return 0;
}

} // namespace

namespace Tina::EditorApp {

int runEditorApplication(int argumentCount, char** arguments)
{
    try {
        return runEditor(argumentCount, arguments);
    } catch (const std::bad_alloc&) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::OutOfMemory,
                                "Tina Editor ran out of memory"};
        writeError(error);
        return 1;
    } catch (const std::exception& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "An exception crossed the Tina Editor boundary"};
        error.addContext("Tina::EditorApp", exception.what() != nullptr ? exception.what() : "");
        writeError(error);
        return 1;
    } catch (...) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Internal,
                                "A non-standard exception crossed the Tina Editor boundary"};
        writeError(error);
        return 1;
    }
}

} // namespace Tina::EditorApp
