// Tina Editor desktop composition: shared retained tool chrome backed by
// validated World2D and World3D authoring documents and Scene GPU previews.

#include "EditorFileDialog.hpp"
#include "EditorSourceImportLaunchOptions.hpp"
#include "EditorSourceImportService.hpp"

#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageChangeDetector.hpp>
#include <tina/asset/CatalogPackagePublish.hpp>
#include <tina/asset/Mesh3DBindingRegistry.hpp>
#include <tina/asset/SourceImportCapture.hpp>
#include <tina/asset/SourceImportPlan.hpp>
#include <tina/asset/Sprite2DBindingRegistry.hpp>
#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/PrefabPayload.hpp>
#include <tina/asset_format/SourceImportMetadataFormat.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/asset_format/World2DSnapshot.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/error/Error.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/editor/EditorDocumentTabs.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/EditorMarqueeSelection.hpp>
#include <tina/editor/EditorPlaySession.hpp>
#include <tina/editor/EditorProjectCreation.hpp>
#include <tina/editor/EditorSceneOperations.hpp>
#include <tina/editor/EditorTransformGizmo.hpp>
#include <tina/editor/EditorViewportGrid.hpp>
#include <tina/editor/EditorViewportNavigation.hpp>
#include <tina/editor/ProjectAssetBrowser.hpp>
#include <tina/editor/SpriteAnimationAuthoringDocument.hpp>
#include <tina/editor/SpriteAnimationAuthoringFile.hpp>
#include <tina/editor/TileMapAuthoringDocument.hpp>
#include <tina/editor/TileMapAuthoringFile.hpp>
#include <tina/editor/TileMapGameplaySpawnPlan.hpp>
#include <tina/editor/World2DAuthoringDocument.hpp>
#include <tina/editor/World2DAuthoringFile.hpp>
#include <tina/editor/World3DAuthoringDocument.hpp>
#include <tina/editor/World3DAuthoringFile.hpp>
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
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UITreeView.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <memory_resource>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

namespace UI = Tina::UI;

using Tina::Core::u8;
using Tina::Core::u32;
using Tina::Core::u64;

inline constexpr u64 DefaultFrameCount = 0;
inline constexpr u32 DefaultFrameDelayMilliseconds = 0;
inline constexpr u32 WindowLogicalWidth = 1280;
inline constexpr u32 WindowLogicalHeight = 800;
inline constexpr u32 HierarchyMaterializedCapacity = 16;
inline constexpr u32 AssetBrowserMaterializedCapacity = 12;
inline constexpr u32 AuthoringEntityCapacity = 16;
inline constexpr u32 InitialAuthoringEntityCount = 5;
inline constexpr u32 AnimationVisibleFrameSlots = 6;
inline constexpr u32 DocumentTabSlots = 6;
inline constexpr u32 AutomaticAuthoringStageCount = 46;
inline constexpr u64 AutomaticAuthoringFrameReserve = 8;
inline constexpr u64 AutomaticAuthoringMinimumFrameCount =
    static_cast<u64>(AutomaticAuthoringStageCount) +
    AutomaticAuthoringFrameReserve;
inline constexpr u32 EditorLayoutRegionCount = 9;
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
inline constexpr Tina::AssetFormat::TileMapLayerId InitialGameplayObjectLayerId = 2;
inline constexpr u32 EditorGameplaySpawnSchema = 0x5453504EU;
inline constexpr u32 EditorGameplaySpawnVersion = 1;
inline constexpr u32 EditorPlayerArchetypeId = 1;
inline constexpr u32 EditorCrateArchetypeId = 2;
inline constexpr float PreviewWorldWidth = 16.0F;
inline constexpr float PreviewWorldHeight = 9.0F;
inline constexpr float PreviewWorld3DCameraDistance = 8.0F;
inline constexpr float DegreesToRadians = 0.01745329251994329577F;
inline constexpr float RadiansToDegrees = 57.295779513082320876F;
inline constexpr Tina::Core::usize ViewportGridDiagonalSteps = 6;
inline constexpr Tina::Core::usize ViewportGridDiagonalPartsPerStep = 2;
inline constexpr Tina::Core::usize ViewportGridVisualNodeCapacity =
    Tina::Editor::EditorViewportGridSegmentCapacity * ViewportGridDiagonalSteps *
    ViewportGridDiagonalPartsPerStep;
inline constexpr Tina::Core::usize ViewportGizmoVisualNodeCapacity = 64;
inline constexpr Tina::Core::usize ViewportMarqueeCandidateCapacity = 64;
inline constexpr Tina::Core::usize ViewportTransformTargetCapacity =
    ViewportMarqueeCandidateCapacity;
inline constexpr u64 ViewportPrimaryPointerToken = 1;

namespace EditorShortcutActions {

inline constexpr Tina::InputActionId Control{1};
inline constexpr Tina::InputActionId Shift{2};
inline constexpr Tina::InputActionId Save{3};
inline constexpr Tina::InputActionId Undo{4};
inline constexpr Tina::InputActionId Redo{5};
inline constexpr Tina::InputActionId Duplicate{6};
inline constexpr Tina::InputActionId DeleteSelection{7};
inline constexpr Tina::InputActionId Switch2D{8};
inline constexpr Tina::InputActionId Switch3D{9};
inline constexpr Tina::InputActionId FrameAll{10};
inline constexpr Tina::InputActionId FocusSelection{11};
inline constexpr Tina::InputActionId Play{12};
inline constexpr Tina::InputActionId Step{13};
inline constexpr Tina::InputActionId Stop{14};
inline constexpr Tina::InputActionId Escape{15};

} // namespace EditorShortcutActions

[[nodiscard]] Tina::Core::AssetId editorAssetId(u8 marker)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(marker);
    return *Tina::Core::AssetId::fromBytes(bytes);
}

void appendGameplayU32(std::vector<std::byte>& bytes, u32 value)
{
    for (u32 shift = 0; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] Tina::Core::Result<std::vector<std::byte>>
encodeEditorGameplaySpawns(
    std::span<const Tina::Editor::TileMapGameplaySpawnRecord> records)
{
    constexpr u32 HeaderBytes = 8;
    constexpr u32 RecordBytes = 28;
    std::vector<std::byte> bytes;
    bytes.reserve(HeaderBytes + records.size() * RecordBytes);
    appendGameplayU32(bytes, static_cast<u32>(records.size()));
    appendGameplayU32(bytes, RecordBytes);
    for (const Tina::Editor::TileMapGameplaySpawnRecord& record : records) {
        appendGameplayU32(bytes, record.stableObjectId);
        appendGameplayU32(bytes, record.gameArchetypeId);
        appendGameplayU32(bytes, static_cast<u32>(record.kind));
        appendGameplayU32(bytes, std::bit_cast<u32>(record.x));
        appendGameplayU32(bytes, std::bit_cast<u32>(record.y));
        appendGameplayU32(bytes, std::bit_cast<u32>(record.width));
        appendGameplayU32(bytes, std::bit_cast<u32>(record.height));
    }
    return bytes;
}

enum class WorkspaceMode : u8 {
    World2D,
    World3D,
};

enum class ViewportToolMode : u8 {
    Select,
    Translate,
    Rotate,
    Scale,
    TilePaint,
    TileErase,
};

[[nodiscard]] constexpr std::string_view viewportViewPresetName(
    Tina::Editor::EditorViewport3DViewPreset preset) noexcept
{
    using Preset = Tina::Editor::EditorViewport3DViewPreset;
    switch (preset) {
    case Preset::Perspective:
        return "Perspective";
    case Preset::Top:
        return "Top";
    case Preset::Bottom:
        return "Bottom";
    case Preset::Front:
        return "Front";
    case Preset::Back:
        return "Back";
    case Preset::Left:
        return "Left";
    case Preset::Right:
        return "Right";
    }
    return "Custom";
}

[[nodiscard]] constexpr Tina::Editor::EditorViewport3DViewPreset
nextViewportViewPreset(
    std::optional<Tina::Editor::EditorViewport3DViewPreset> current) noexcept
{
    using Preset = Tina::Editor::EditorViewport3DViewPreset;
    if (!current.has_value()) {
        return Preset::Perspective;
    }
    switch (*current) {
    case Preset::Perspective:
        return Preset::Top;
    case Preset::Top:
        return Preset::Front;
    case Preset::Front:
        return Preset::Right;
    case Preset::Right:
        return Preset::Back;
    case Preset::Back:
        return Preset::Left;
    case Preset::Left:
        return Preset::Bottom;
    case Preset::Bottom:
        return Preset::Perspective;
    }
    return Preset::Perspective;
}

struct ViewportTransformTransaction final {
    struct Target final {
        u32 stableEntityId = 0;
        Tina::Scene::EntityId entity{};
        Tina::Scene::LocalTransform baselineLocal{};
        Tina::Scene::WorldTransform baselineWorld{};
        Tina::Scene::LocalTransform previewLocal{};
    };

    WorkspaceMode workspace = WorkspaceMode::World2D;
    Tina::Platform::PointerId pointer = Tina::Platform::PrimaryPointerId;
    u32 stableEntityId = 0;
    u64 baselineRevision = 0;
    u64 selectionRevision = 0;
    Tina::Scene::Vec3 pivot{};
    Tina::Scene::LocalTransform baselineTransform{};
    Tina::Scene::LocalTransform previewTransform{};
    std::array<Target, ViewportTransformTargetCapacity> targets{};
    Tina::Core::usize targetCount = 0;
    Tina::Core::usize selectionCount = 0;
    Tina::Editor::EditorTransformGizmoDelta delta{};
    u64 processedGizmoRevision = 0;
    bool captured = false;
    bool baselineReady = false;
    bool previewPublished = false;
    bool commitRequested = false;
    bool cancelRequested = false;
};

struct ViewportNavigationDrag final {
    Tina::Platform::PointerId pointer = Tina::Platform::PrimaryPointerId;
    Tina::Platform::PointerButton button = Tina::Platform::PointerButton::Middle;
    bool captured = false;
};

struct ViewportMarqueeTransaction final {
    Tina::Platform::PointerId pointer = Tina::Platform::PrimaryPointerId;
    UI::UILogicalPoint start{};
    UI::UILogicalPoint current{};
    Tina::Editor::EditorMarqueeSelectionMode mode =
        Tina::Editor::EditorMarqueeSelectionMode::Replace;
    bool captured = false;
    bool commitRequested = false;
    bool cancelRequested = false;
};

struct ViewportProjectedPoint final {
    UI::UILogicalPoint screen{};
    float cameraDepth = 0.0F;
    bool projectable = false;
};

inline constexpr UI::UITreeViewItemKey HierarchyDocumentRootKey =
    (std::numeric_limits<UI::UITreeViewItemKey>::max)();

struct EditorLaunchOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    std::string world2DDocumentPathUtf8{};
    std::string world3DDocumentPathUtf8{};
    std::string catalogRootUtf8{};
    Tina::EditorApp::Detail::EditorSourceImportLaunchOptions sourceImport{};
    WorkspaceMode initialWorkspace = WorkspaceMode::World2D;
    bool autoDemo = false;
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

[[nodiscard]] constexpr Tina::AssetFormat::TargetPlatform editorTargetPlatform() noexcept;

[[nodiscard]] bool pathComponentEquals(const std::filesystem::path& left,
                                       const std::filesystem::path& right) noexcept
{
#if defined(_WIN32)
    const auto& leftText = left.native();
    const auto& rightText = right.native();
    if (leftText.size() != rightText.size() ||
        leftText.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    return ::CompareStringOrdinal(leftText.data(), static_cast<int>(leftText.size()),
                                  rightText.data(), static_cast<int>(rightText.size()),
                                  TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

[[nodiscard]] bool pathIsSameOrDescendant(const std::filesystem::path& candidate,
                                          const std::filesystem::path& ancestor) noexcept
{
    auto candidatePart = candidate.begin();
    for (auto ancestorPart = ancestor.begin(); ancestorPart != ancestor.end();
         ++ancestorPart, ++candidatePart) {
        if (candidatePart == candidate.end() ||
            !pathComponentEquals(*candidatePart, *ancestorPart)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool pathsReferToSameLocation(const std::filesystem::path& left,
                                            const std::filesystem::path& right) noexcept
{
    return pathIsSameOrDescendant(left, right) &&
           pathIsSameOrDescendant(right, left);
}

[[nodiscard]] Tina::Core::Status validatePhysicalProjectDirectory(
    const std::filesystem::path& path, std::string_view label)
{
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(path, statusError);
    if (statusError) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                "Editor could not inspect a project directory"};
        error.setNativeCode(statusError.value());
        error.addContext(label, pathToUtf8(path));
        return Tina::Core::failure(std::move(error));
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                "Editor project layout requires physical directories"};
        error.addContext(label, pathToUtf8(path));
        return Tina::Core::failure(std::move(error));
    }
#if defined(_WIN32)
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD nativeCode = ::GetLastError();
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                "Editor could not inspect Win32 project directory attributes"};
        error.setNativeCode(static_cast<Tina::Core::i64>(nativeCode));
        error.addContext(label, pathToUtf8(path));
        return Tina::Core::failure(std::move(error));
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::InvalidArgument,
                                "Editor project directories must not be junctions or reparse points"};
        error.addContext(label, pathToUtf8(path));
        return Tina::Core::failure(std::move(error));
    }
#endif
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Result<Tina::Editor::EditorProjectWorkspace>
openExistingEditorProjectWorkspace(std::string_view projectRootUtf8)
{
    if (projectRootUtf8.empty() || !Tina::Core::isStrictUtf8WithoutNul(projectRootUtf8)) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Editor project root must be strict UTF-8 without NUL");
    }

    try {
        const std::filesystem::path projectRoot =
            std::filesystem::u8path(projectRootUtf8.begin(), projectRootUtf8.end());
        const std::filesystem::path sourceRoot = projectRoot / "Source";
        const std::filesystem::path catalogRoot = projectRoot / "Catalog";
        for (const auto& directory : std::array{
                 std::pair{std::cref(projectRoot), std::string_view{"projectRoot"}},
                 std::pair{std::cref(sourceRoot), std::string_view{"sourceRoot"}},
                 std::pair{std::cref(catalogRoot), std::string_view{"cookedCatalogRoot"}},
             }) {
            if (auto status = validatePhysicalProjectDirectory(directory.first.get(),
                                                               directory.second);
                !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
        }

        std::error_code canonicalError;
        const auto physicalProject = std::filesystem::weakly_canonical(projectRoot,
                                                                        canonicalError);
        if (canonicalError) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                       "Editor could not resolve the physical project root");
        }
        const auto physicalSource = std::filesystem::weakly_canonical(sourceRoot,
                                                                       canonicalError);
        if (canonicalError) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                       "Editor could not resolve the physical Source root");
        }
        const auto physicalCatalog = std::filesystem::weakly_canonical(catalogRoot,
                                                                        canonicalError);
        if (canonicalError) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                       "Editor could not resolve the physical Catalog root");
        }
        if (physicalSource == physicalProject || physicalCatalog == physicalProject ||
            !pathIsSameOrDescendant(physicalSource, physicalProject) ||
            !pathIsSameOrDescendant(physicalCatalog, physicalProject) ||
            pathIsSameOrDescendant(physicalSource, physicalCatalog) ||
            pathIsSameOrDescendant(physicalCatalog, physicalSource)) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::PermissionDenied,
                "Editor Source and Catalog directories escaped their project root");
        }

        const std::string canonicalProject = pathToUtf8(projectRoot.lexically_normal());
        const std::string canonicalSource = pathToUtf8(sourceRoot.lexically_normal());
        const std::string canonicalCatalog = pathToUtf8(catalogRoot.lexically_normal());
        return Tina::Editor::EditorProjectWorkspace::Create({
            .projectRootUtf8 = canonicalProject,
            .sourceRootUtf8 = canonicalSource,
            .cookedCatalogRootUtf8 = canonicalCatalog,
            .targetPlatform = editorTargetPlatform(),
        });
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Editor project open allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                "Editor project open filesystem validation failed"};
        error.setNativeCode(exception.code().value());
        return Tina::Core::failure(std::move(error));
    }
}

[[nodiscard]] constexpr Tina::AssetFormat::TargetPlatform editorTargetPlatform() noexcept
{
#if defined(_WIN32)
    return Tina::AssetFormat::TargetPlatform::WindowsX64;
#else
    return Tina::AssetFormat::TargetPlatform::LinuxX64;
#endif
}

struct EditorSourceImportCachePaths final {
    std::filesystem::path root{};
    std::filesystem::path stages{};
    std::filesystem::path baselineState{};
    std::filesystem::path activeCatalogPointer{};
};

struct EditorSourceImportStagePaths final {
    std::string catalogRootUtf8{};
    std::string statePathUtf8{};
};

struct ResolvedEditorProjectCatalog final {
    std::string catalogRootUtf8{};
    std::string sourceImportStatePathUtf8{};
    std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit> sourceImportUnits{};
};

[[nodiscard]] EditorSourceImportCachePaths sourceImportCachePaths(
    const Tina::Editor::EditorProjectWorkspace& workspace)
{
    const auto projectRoot = std::filesystem::u8path(
        workspace.projectRootUtf8().begin(), workspace.projectRootUtf8().end());
    const auto root = projectRoot / ".tina" / "cache" / "source-import";
    return {
        .root = root,
        .stages = root / "stages",
        .baselineState = root / "baseline-state.tmeta",
        .activeCatalogPointer = root / "active-catalog.path",
    };
}

[[nodiscard]] Tina::Core::Status ensurePhysicalDirectory(
    const std::filesystem::path& directory, const std::filesystem::path& physicalProjectRoot,
    std::string_view label)
{
    std::error_code createError;
    (void)std::filesystem::create_directory(directory, createError);
    if (createError) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                "Editor could not create the source-import tool cache"};
        error.setNativeCode(createError.value());
        error.addContext(label, pathToUtf8(directory));
        return Tina::Core::failure(std::move(error));
    }
    if (auto status = validatePhysicalProjectDirectory(directory, label); !status) {
        return status;
    }
    std::error_code canonicalError;
    const auto physicalDirectory = std::filesystem::weakly_canonical(directory, canonicalError);
    if (canonicalError || !pathIsSameOrDescendant(physicalDirectory, physicalProjectRoot)) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::PermissionDenied,
            "Editor source-import tool cache escaped the project root");
    }
    return Tina::Core::success();
}

[[nodiscard]] Tina::Core::Result<EditorSourceImportCachePaths>
ensureSourceImportCache(const Tina::Editor::EditorProjectWorkspace& workspace)
{
    try {
        const auto paths = sourceImportCachePaths(workspace);
        const auto projectRoot = std::filesystem::u8path(
            workspace.projectRootUtf8().begin(), workspace.projectRootUtf8().end());
        std::error_code canonicalError;
        const auto physicalProjectRoot =
            std::filesystem::weakly_canonical(projectRoot, canonicalError);
        if (canonicalError) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Io,
                "Editor could not resolve the source-import project root");
        }
        const std::array directories{
            projectRoot / ".tina",
            projectRoot / ".tina" / "cache",
            paths.root,
            paths.stages,
        };
        constexpr std::array<std::string_view, directories.size()> labels{
            "toolRoot", "cacheRoot", "sourceImportRoot", "sourceImportStages",
        };
        for (u32 index = 0; index < directories.size(); ++index) {
            if (auto status = ensurePhysicalDirectory(
                    directories[index], physicalProjectRoot, labels[index]);
                !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
        }
        return paths;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Editor source-import cache path allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                "Editor source-import cache filesystem operation failed"};
        error.setNativeCode(exception.code().value());
        return Tina::Core::failure(std::move(error));
    }
}

[[nodiscard]] Tina::Core::Result<EditorSourceImportStagePaths>
createSourceImportStagePaths(const Tina::Editor::EditorProjectWorkspace& workspace)
{
    auto cache = ensureSourceImportCache(workspace);
    if (!cache) {
        return Tina::Core::failure(std::move(cache.error()));
    }
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    constexpr u32 MaximumAttempts = 256;
    for (u32 attempt = 0; attempt < MaximumAttempts; ++attempt) {
        try {
            const auto parent = cache->stages /
                                ("stage_" + std::to_string(seed) + "_" +
                                 std::to_string(attempt));
            std::error_code createError;
            if (!std::filesystem::create_directory(parent, createError)) {
                if (!createError) {
                    continue;
                }
                Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                        "Editor could not reserve a source-import stage path"};
                error.setNativeCode(createError.value());
                error.addContext("stageRoot", pathToUtf8(parent));
                return Tina::Core::failure(std::move(error));
            }

            auto rollback = Tina::Core::makeScopeExit([&parent]() noexcept {
                std::error_code cleanupError;
                (void)std::filesystem::remove(parent, cleanupError);
            });
            if (auto status = validatePhysicalProjectDirectory(parent, "sourceImportStage");
                !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
            std::error_code canonicalError;
            const auto physicalStages =
                std::filesystem::weakly_canonical(cache->stages, canonicalError);
            if (canonicalError) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Io,
                    "Editor could not resolve the source-import stages root");
            }
            const auto physicalParent =
                std::filesystem::weakly_canonical(parent, canonicalError);
            if (canonicalError || physicalParent.parent_path() != physicalStages) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::PermissionDenied,
                    "Editor source-import stage escaped the stages root");
            }

            EditorSourceImportStagePaths result{
                .catalogRootUtf8 = pathToUtf8(parent / "catalog"),
                .statePathUtf8 = pathToUtf8(parent / "import-state.tmeta"),
            };
            rollback.release();
            return result;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor source-import stage path allocation failed");
        }
    }
    return Tina::Core::failure(Tina::Core::CoreErrorCode::AlreadyExists,
                               "Editor exhausted source-import stage path attempts");
}

[[nodiscard]] Tina::Core::Result<std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit>>
restoreSourceImportUnits(
    const Tina::Editor::EditorProjectWorkspace& workspace,
    const Tina::AssetFormat::SourceImportMetadataView& metadata)
{
    std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit> units;
    units.reserve(metadata.header().unitCount);
    const auto sourceRoot = std::filesystem::u8path(
        workspace.sourceRootUtf8().begin(), workspace.sourceRootUtf8().end())
                                .lexically_normal();
    for (u32 unitIndex = 0; unitIndex < metadata.header().unitCount; ++unitIndex) {
        const auto metadataUnit = metadata.unit(unitIndex);
        if (!metadataUnit) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Editor source-import metadata unit disappeared after validation");
        }

        Tina::EditorApp::Detail::EditorSourceImportUnitKind kind{};
        if (metadataUnit->importerKind ==
            static_cast<u32>(Tina::Asset::SourceImporterKind::CatalogRecipe)) {
            kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::CatalogRecipe;
        } else if (metadataUnit->importerKind ==
                   static_cast<u32>(Tina::Asset::SourceImporterKind::Gltf)) {
            kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::Gltf;
        } else {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::InvalidArgument,
                "Editor source-import metadata contains an unsupported importer kind");
        }

        std::optional<std::string_view> primaryPath{};
        for (u32 inputIndex = 0; inputIndex < metadataUnit->inputCount; ++inputIndex) {
            const auto input = metadata.unitInputForUnit(unitIndex, inputIndex);
            if (!input) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Editor source-import metadata input disappeared after validation");
            }
            if (!Tina::AssetFormat::hasSourceImportInputFlag(
                    input->flags, Tina::AssetFormat::SourceImportInputFlags::Primary)) {
                continue;
            }
            if (primaryPath.has_value()) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Editor source-import metadata unit has multiple primary inputs");
            }
            primaryPath = metadata.sourcePath(input->sourceIndex);
        }
        if (!primaryPath.has_value()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::InvalidArgument,
                "Editor source-import metadata unit has no primary input");
        }

        const auto relativePath = std::filesystem::u8path(
            primaryPath->begin(), primaryPath->end());
        if (relativePath.is_absolute()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::PermissionDenied,
                "Editor source-import metadata primary path must be source-root relative");
        }
        const auto absolutePath = (sourceRoot / relativePath).lexically_normal();
        if (absolutePath == sourceRoot ||
            !pathIsSameOrDescendant(absolutePath, sourceRoot)) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::PermissionDenied,
                "Editor source-import metadata primary path escaped the Source root");
        }
        units.push_back({
            .kind = kind,
            .sourcePathUtf8 = pathToUtf8(absolutePath),
        });
    }
    return units;
}

[[nodiscard]] Tina::Core::Result<ResolvedEditorProjectCatalog> resolveProjectCatalog(
    const Tina::Editor::EditorProjectWorkspace& workspace)
{
    try {
        const auto cache = sourceImportCachePaths(workspace);
        std::error_code existsError;
        const auto pointerStatus =
            std::filesystem::symlink_status(cache.activeCatalogPointer, existsError);
        if (existsError == std::errc::no_such_file_or_directory) {
            existsError.clear();
            return ResolvedEditorProjectCatalog{
                .catalogRootUtf8 = std::string{workspace.cookedCatalogRootUtf8()},
                .sourceImportStatePathUtf8 = pathToUtf8(cache.baselineState),
            };
        }
        if (existsError) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Io,
                "Editor could not inspect the active import Catalog pointer");
        }
        if (!std::filesystem::exists(pointerStatus)) {
            return ResolvedEditorProjectCatalog{
                .catalogRootUtf8 = std::string{workspace.cookedCatalogRootUtf8()},
                .sourceImportStatePathUtf8 = pathToUtf8(cache.baselineState),
            };
        }
        if (!std::filesystem::is_regular_file(pointerStatus)) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::InvalidArgument,
                "Editor active import Catalog pointer must be a physical regular file");
        }

        std::pmr::unsynchronized_pool_resource validationMemory;
        auto pointerBytes = Tina::Core::readFile(
            pathToUtf8(cache.activeCatalogPointer),
            {.maxBytes = 4096, .memoryResource = &validationMemory});
        if (!pointerBytes) {
            return Tina::Core::failure(std::move(pointerBytes.error()));
        }
        const std::string_view pointerText{
            reinterpret_cast<const char*>(pointerBytes->data()), pointerBytes->size()};
        if (pointerText.empty() || !Tina::Core::isStrictUtf8WithoutNul(pointerText)) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::InvalidArgument,
                "Editor active import Catalog pointer is not strict UTF-8");
        }
        const auto catalogRoot = std::filesystem::u8path(pointerText.begin(), pointerText.end());
        if (!catalogRoot.is_absolute()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::InvalidArgument,
                "Editor active import Catalog pointer must be absolute");
        }
        if (auto status = validatePhysicalProjectDirectory(catalogRoot, "activeImportCatalog");
            !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        const auto stageRoot = catalogRoot.parent_path().lexically_normal();
        if (stageRoot.empty()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::PermissionDenied,
                "Editor active import Catalog is not owned by a direct stage directory");
        }
        if (auto status = validatePhysicalProjectDirectory(stageRoot, "activeImportStage");
            !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        const auto statePath = stageRoot / "import-state.tmeta";
        const bool hasState = std::filesystem::is_regular_file(
            std::filesystem::symlink_status(statePath, existsError));
        if (existsError || !hasState) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::NotFound,
                "Editor active import Catalog has no committed stage state");
        }

        const std::array cacheDirectories{
            cache.root.parent_path().parent_path(),
            cache.root.parent_path(),
            cache.root,
            cache.stages,
        };
        constexpr std::array<std::string_view, cacheDirectories.size()> cacheLabels{
            "toolRoot", "cacheRoot", "sourceImportRoot", "sourceImportStages",
        };
        for (u32 index = 0; index < cacheDirectories.size(); ++index) {
            if (auto status = validatePhysicalProjectDirectory(
                    cacheDirectories[index], cacheLabels[index]);
                !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
        }

        std::error_code canonicalError;
        const auto physicalCatalog =
            std::filesystem::weakly_canonical(catalogRoot, canonicalError);
        if (canonicalError) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Io,
                "Editor could not resolve the active import Catalog");
        }
        const auto physicalStages =
            std::filesystem::weakly_canonical(cache.stages, canonicalError);
        if (canonicalError) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Io,
                "Editor could not resolve the source-import stage root");
        }
        const auto physicalStage =
            std::filesystem::weakly_canonical(stageRoot, canonicalError);
        if (canonicalError ||
            !pathsReferToSameLocation(physicalStage.parent_path(), physicalStages)) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::PermissionDenied,
                "Editor active import stage escaped the source-import stages root");
        }
        const auto projectRoot = std::filesystem::u8path(
            workspace.projectRootUtf8().begin(), workspace.projectRootUtf8().end());
        const auto physicalProject =
            std::filesystem::weakly_canonical(projectRoot, canonicalError);
        if (canonicalError ||
            !pathIsSameOrDescendant(physicalStages, physicalProject) ||
            !pathIsSameOrDescendant(physicalCatalog, physicalStages)) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::PermissionDenied,
                "Editor active import Catalog escaped the project tool cache");
        }

        auto stateBytes = Tina::Core::readFile(
            pathToUtf8(statePath),
            {.maxBytes = Tina::Core::MaxReadFileBytes,
             .memoryResource = &validationMemory});
        if (!stateBytes) {
            return Tina::Core::failure(std::move(stateBytes.error()));
        }
        auto metadata = Tina::AssetFormat::parseSourceImportMetadataView(*stateBytes);
        if (!metadata) {
            return Tina::Core::failure(std::move(metadata.error()));
        }
        Tina::Asset::CatalogPackageOpenConfig openConfig{};
        openConfig.manifest.catalog.maxEntries = 4096;
        openConfig.manifest.catalog.maxDependencies = 16384;
        openConfig.manifest.catalog.maxDependenciesPerAsset = 4096;
        openConfig.manifest.catalog.memoryResource = &validationMemory;
        openConfig.validation.file.memoryResource = &validationMemory;
        openConfig.validation.verifyTypedPayload = true;
        auto catalog = Tina::Asset::openCatalogPackage(pathToUtf8(catalogRoot), openConfig);
        if (!catalog) {
            return Tina::Core::failure(std::move(catalog.error()));
        }
        auto revision = Tina::Asset::captureCatalogPackageRevision(
            pathToUtf8(catalogRoot),
            {.scratchMemoryResource = &validationMemory});
        if (!revision) {
            return Tina::Core::failure(std::move(revision.error()));
        }
        const Tina::AssetFormat::SourceImportManifestRevision importRevision{
            .manifestDigest = revision->manifestDigest,
            .manifestBytes = revision->manifestBytes,
        };
        if (auto status = Tina::Asset::validateSourceImportCatalogBinding(
                *metadata, importRevision);
            !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        if (auto status = Tina::Asset::validateSourceImportCatalogOutputs(
                *metadata, *catalog);
            !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        auto units = restoreSourceImportUnits(workspace, *metadata);
        if (!units) {
            return Tina::Core::failure(std::move(units.error()));
        }
        return ResolvedEditorProjectCatalog{
            .catalogRootUtf8 = pathToUtf8(catalogRoot.lexically_normal()),
            .sourceImportStatePathUtf8 = pathToUtf8(statePath.lexically_normal()),
            .sourceImportUnits = std::move(*units),
        };
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Editor active import Catalog path allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                "Editor active import Catalog resolution failed"};
        error.setNativeCode(exception.code().value());
        return Tina::Core::failure(std::move(error));
    }
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

    recipe += "spriteanim ";
    appendId(editorAssetId(0x10U));
    recipe += " Loop ";
    appendId(editorAssetId(0x22U));
    recipe += ":0.10 ";
    appendId(editorAssetId(0x23U));
    recipe += ":0.15\n";
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
    std::string sourceImportStatePathUtf8{};
    std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit> initialSourceImportUnits{};
    std::optional<Tina::Editor::EditorProjectWorkspace> initialProjectWorkspace{};
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
    resources.projectCatalogConfigured =
        !options.catalogRootUtf8.empty() || !options.sourceImport.projectRootUtf8.empty();
    resources.builtInPreviewCatalog = !resources.projectCatalogConfigured;
    if (!options.sourceImport.projectRootUtf8.empty()) {
        auto workspace = openExistingEditorProjectWorkspace(
            options.sourceImport.projectRootUtf8);
        if (!workspace) {
            return Tina::Core::failure(std::move(workspace.error()));
        }
        auto resolvedCatalog = resolveProjectCatalog(*workspace);
        if (!resolvedCatalog) {
            return Tina::Core::failure(std::move(resolvedCatalog.error()));
        }
        resources.catalogRootUtf8 = std::move(resolvedCatalog->catalogRootUtf8);
        resources.sourceImportStatePathUtf8 =
            std::move(resolvedCatalog->sourceImportStatePathUtf8);
        resources.initialSourceImportUnits =
            std::move(resolvedCatalog->sourceImportUnits);
        resources.initialProjectWorkspace.emplace(std::move(*workspace));
    } else if (resources.projectCatalogConfigured) {
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
    u32 automaticTransformStableId = 0;
    u32 automaticFinalSelectionStableId = 0;
    u64 projectAssetSelectionChanges = 0;
    u64 projectAssetOpenCount = 0;
    u64 projectAssetVisibleItems = 0;
    u64 documentTabCount = 0;
    u64 documentTabSwitches = 0;
    u64 tabOwnedDocumentLoads = 0;
    u64 tabOwnedDocumentSwaps = 0;
    u64 previewAssetBindingRefreshes = 0;
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
    u64 tileMapGameplayGenerations = 0;
    u64 tileMapGameplaySpawnRecords = 0;
    u64 tileMapGameplayBytes = 0;
    u64 tileMapGameplaySourceRevision = 0;
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
    bool editorActionsReady = false;
    u64 authoringEdits = 0;
    u64 authoringUndos = 0;
    u64 authoringRedos = 0;
    u64 authoringSaves = 0;
    u64 inspectorTransactions = 0;
    u64 inspectorRejectedTransactions = 0;
    u64 viewportGizmoBegins = 0;
    u64 viewportGizmoPreviews = 0;
    u64 viewportGizmoCommits = 0;
    u64 viewportTranslateGizmoCommits = 0;
    u64 viewportRotateGizmoCommits = 0;
    u64 viewportScaleGizmoCommits = 0;
    u64 viewportGroupGizmoCommits = 0;
    u64 viewportGroupRotateGizmoCommits = 0;
    u64 viewportGroupScaleGizmoCommits = 0;
    u64 viewportMaximumGizmoTargets = 0;
    u64 viewportGizmoCancels = 0;
    u64 viewportGizmoRejects = 0;
    u64 viewportNavigationBatches = 0;
    u64 viewportPan2DInputs = 0;
    u64 viewportZoom2DInputs = 0;
    u64 viewportOrbit3DInputs = 0;
    u64 viewportPan3DInputs = 0;
    u64 viewportDolly3DInputs = 0;
    u64 viewportMarqueeCommits = 0;
    u64 viewportMarqueeReplaceCommits = 0;
    u64 viewportMarqueeAddCommits = 0;
    u64 viewportMarqueeToggleCommits = 0;
    u64 viewportMarqueeSelectionChanges = 0;
    u64 viewportMarqueeAddedItems = 0;
    u64 viewportMarqueeRemovedItems = 0;
    u64 viewportMarqueeMaximumSelection = 0;
    u64 viewportMarqueeCancels = 0;
    u64 viewportMarqueeRejects = 0;
    u64 sceneAddCommands = 0;
    u64 sceneDuplicateCommands = 0;
    u64 sceneReparentRootCommands = 0;
    u64 sceneReparentCommands = 0;
    u64 sceneDeleteCommands = 0;
    u32 automaticAddedStableId = 0;
    u32 automaticDuplicatedStableId = 0;
    u32 automaticAuthoringStage = 0;
    u64 playStarts = 0;
    u64 playPauses = 0;
    u64 playStepRequests = 0;
    u64 playResumes = 0;
    u64 playStops = 0;
    u64 playSimulationSteps = 0;
    u64 playMaximumSimulationTick = 0;
    u64 viewportGridRevision = 0;
    u64 viewportGridSegments = 0;
    u64 viewportGridMinorLines = 0;
    u64 viewportGridMajorLines = 0;
    u64 viewportGridAxisLines = 0;
    u64 savedSnapshotBytes = 0;
    u64 world2DSavedSnapshotBytes = 0;
    u64 world3DSavedSnapshotBytes = 0;
    u64 runtimePreviewInstantiations = 0;
    u64 documentRevision = 0;
    u64 documentEntityCount = 0;
    u64 documentUndoDepth = 0;
    u64 documentRedoDepth = 0;
    u64 cookPreviewBytes = 0;
    float selectedTransformPositionX = 0.0F;
    float selectedTransformPositionY = 0.0F;
    float selectedTransformPositionZ = 0.0F;
    float selectedTransformRotationXDegrees = 0.0F;
    float selectedTransformRotationYDegrees = 0.0F;
    float selectedTransformRotationZDegrees = 0.0F;
    float selectedTransformScaleX = 1.0F;
    float selectedTransformScaleY = 1.0F;
    float selectedTransformScaleZ = 1.0F;
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
    float viewportGizmoRotationDegrees = 0.0F;
    float viewportGizmoScaleFactorX = 1.0F;
    float viewportGizmoScaleFactorY = 1.0F;
    float viewportGizmoScaleFactorZ = 1.0F;
    float viewportZoomPercent = 100.0F;
    u64 editorLayoutRegions = 0;
    bool selectionVerified = false;
    bool stylesheetInstalled = false;
    bool runtimePreviewValid = false;
    bool gpuViewportReady = false;
    bool viewportGridReady = false;
    bool viewportGrid2DObserved = false;
    bool viewportGrid3DObserved = false;
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
    bool projectAssetBrowserReady = false;
    bool documentTabsReady = false;
    u32 projectSwitches = 0;
    u64 sourceImportIntendedUnits = 0;
    u64 sourceImportStarts = 0;
    u64 sourceImportCompletions = 0;
    u64 sourceImportFailures = 0;
    u64 sourceImportBusyRetries = 0;
    u64 sourceImportCatalogReloads = 0;
    u64 sourceImportUnitsTotal = 0;
    u64 sourceImportUnitsRecooked = 0;
    u64 sourceImportUnitsRemoved = 0;
    u64 sourceImportObjectsReused = 0;
    u64 sourceImportObjectsCooked = 0;
    bool sourceImportRunning = false;
    bool sourceImportReady = false;
    bool sourceImportStateCommitted = false;
};

enum class EditorCommand : u32 {
    SwitchToWorld2D,
    SwitchToWorld3D,
    MoveSelectedPositiveX,
    ApplyTransform,
    Undo,
    Redo,
    Save,
    SaveAs,
    SceneAdd,
    SceneDuplicate,
    SceneDelete,
    SceneReparentRoot,
    SceneReparent,
    SceneFocus,
    PlayStartOrResume,
    PlayPause,
    PlayStep,
    PlayStop,
    PaintTile,
    EraseTile,
    ToggleTileLayer,
    AddTileLayer,
    AddObjectLayer,
    CookTileMapPreview,
    GenerateTileMapGameplay,
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
    ProjectFilterAll,
    ProjectFilter2D,
    ProjectFilter3D,
    ProjectFilterMedia,
    ViewportCyclePreset,
    ViewportResetView,
    RefreshProjectCatalog,
    CreateProject,
    OpenProject,
    ImportSource,
    OpenSelectedProjectAsset,
    CloseActiveDocument,
    DirtyCloseSave,
    DirtyCloseDiscard,
    DirtyCloseCancel,
};

[[nodiscard]] bool editorShortcutStarted(
    const Tina::FrameActionSnapshot& snapshot,
    Tina::InputActionId action) noexcept
{
    for (const Tina::FrameActionTransition& transition : snapshot.transitions) {
        const auto* input = std::get_if<Tina::InputActionTransition>(&transition);
        if (input != nullptr && input->action == action &&
            input->kind == Tina::InputActionTransitionKind::Started) {
            return true;
        }
    }
    return false;
}

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

struct InspectorTransformInput final {
    std::optional<float> positionX{};
    std::optional<float> positionY{};
    std::optional<float> positionZ{};
    std::optional<float> rotationX{};
    std::optional<float> rotationY{};
    std::optional<float> rotationZ{};
    std::optional<float> scaleX{};
    std::optional<float> scaleY{};
    std::optional<float> scaleZ{};
};

using InspectorMixedTransformFlags = std::array<bool, 9>;

enum class InspectorTransformField : u8 {
    PositionX = 0,
    PositionY = 1,
    PositionZ = 2,
    RotationX = 3,
    RotationY = 4,
    RotationZ = 5,
    ScaleX = 6,
    ScaleY = 7,
    ScaleZ = 8,
};

[[nodiscard]] constexpr std::size_t inspectorTransformFieldIndex(
    InspectorTransformField field) noexcept
{
    return static_cast<std::size_t>(field);
}

[[nodiscard]] Tina::Core::Result<std::optional<float>>
parseInspectorTransformValue(std::string_view text, std::string_view fieldName)
{
    if (text == "Mixed") {
        return std::optional<float>{};
    }
    float value = 0.0F;
    if (parseFiniteFloat(text, value)) {
        return value;
    }
    try {
        std::string message{fieldName};
        message += " must be a finite decimal or Mixed";
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation, message);
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Inspector transform validation message allocation failed");
    }
}

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
    bool hasAutoDemo = false;
    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        auto sourceImportOption =
            Tina::EditorApp::Detail::parseEditorSourceImportLaunchOption(
                argument, options.sourceImport);
        if (!sourceImportOption) {
            return Tina::Core::failure(std::move(sourceImportOption.error()));
        }
        if (*sourceImportOption) {
            continue;
        }
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
        if (argument == "--auto-demo") {
            if (hasAutoDemo) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Duplicate --auto-demo argument");
            }
            options.autoDemo = true;
            hasAutoDemo = true;
            continue;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Unsupported command-line argument");
    }
    if (auto status = Tina::EditorApp::Detail::validateEditorSourceImportLaunchOptions(
            options.sourceImport);
        !status) {
        return Tina::Core::failure(std::move(status.error()));
    }
    if (!options.sourceImport.projectRootUtf8.empty() && hasCatalogRoot) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "--project-root and --catalog-root are mutually exclusive");
    }
    if (options.autoDemo && options.targetFrameCount == 0) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "--auto-demo requires an explicit --frames value");
    }
    if (options.autoDemo &&
        options.targetFrameCount < AutomaticAuthoringMinimumFrameCount) {
        std::string message = "--auto-demo requires --frames of at least ";
        message += std::to_string(AutomaticAuthoringMinimumFrameCount);
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   std::move(message));
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

[[nodiscard]] UI::UILayoutStyle dirtyCloseModalLayout(
    UI::UIVisibility visibility) noexcept
{
    UI::UILayoutStyle style = fixedSize(520.0F, 214.0F);
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.horizontal = UI::UIAxisAlignment::Center;
    style.overlay.vertical = UI::UIAxisAlignment::Center;
    style.visibility = visibility;
    style.padding = UI::UIEdgeSpacing::All(18.0F);
    style.flexContainer.direction = UI::UIFlexDirection::Column;
    style.flexContainer.gap.row = 10.0F;
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

[[nodiscard]] u32 stableEntityIdForHierarchyItem(UI::UITreeViewItemKey key) noexcept
{
    return key <= (std::numeric_limits<u32>::max)()
               ? static_cast<u32>(key)
               : 0;
}

struct EditorHierarchyRow final {
    UI::UITreeViewItemKey key = UI::InvalidUITreeViewItemKey;
    u32 stableId = 0;
    u32 parentStableId = 0;
    u32 level = 0;
    bool expandable = false;
    std::string label{};
};

struct SavedTileMapChunkBaseline final {
    Tina::Core::AssetId assetId{};
    std::vector<std::byte> payloadBytes{};
};

struct SavedDocumentBaseline final {
    Tina::Core::AssetId assetId{};
    std::vector<std::byte> primaryBytes{};
    std::vector<SavedTileMapChunkBaseline> tileMapChunks{};
    bool captured = false;

    [[nodiscard]] Tina::Core::usize byteCount() const noexcept
    {
        Tina::Core::usize total = primaryBytes.size();
        for (const SavedTileMapChunkBaseline& chunk : tileMapChunks) {
            total += chunk.payloadBytes.size();
        }
        return total;
    }
};

[[nodiscard]] Tina::Core::Result<SavedDocumentBaseline>
captureSavedBaseline(const Tina::Editor::World2DAuthoringDocument& document)
{
    try {
        SavedDocumentBaseline baseline{};
        const std::span<const std::byte> bytes = document.snapshotBytes();
        baseline.primaryBytes.assign(bytes.begin(), bytes.end());
        baseline.captured = true;
        return baseline;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "World2D saved baseline allocation failed");
    }
}

[[nodiscard]] Tina::Core::Result<SavedDocumentBaseline>
captureSavedBaseline(const Tina::Editor::World3DAuthoringDocument& document)
{
    try {
        SavedDocumentBaseline baseline{};
        const std::span<const std::byte> bytes = document.payloadBytes();
        baseline.primaryBytes.assign(bytes.begin(), bytes.end());
        baseline.captured = true;
        return baseline;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "World3D saved baseline allocation failed");
    }
}

[[nodiscard]] Tina::Core::Result<SavedDocumentBaseline>
captureSavedBaseline(const Tina::Editor::TileMapAuthoringDocument& document)
{
    try {
        SavedDocumentBaseline baseline{};
        baseline.assetId = document.tileMapId();
        const std::span<const std::byte> rootBytes = document.rootPayloadBytes();
        baseline.primaryBytes.assign(rootBytes.begin(), rootBytes.end());
        baseline.tileMapChunks.reserve(document.chunkCount());
        for (Tina::Core::usize index = 0; index < document.chunkCount(); ++index) {
            const auto chunk = document.chunkPayloadAt(index);
            if (!chunk) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "TileMap chunk disappeared while capturing the saved baseline");
            }
            SavedTileMapChunkBaseline savedChunk{.assetId = chunk->assetId};
            savedChunk.payloadBytes.assign(chunk->payloadBytes.begin(),
                                           chunk->payloadBytes.end());
            baseline.tileMapChunks.push_back(std::move(savedChunk));
        }
        baseline.captured = true;
        return baseline;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "TileMap saved baseline allocation failed");
    }
}

[[nodiscard]] Tina::Core::Result<SavedDocumentBaseline>
captureSavedBaseline(const Tina::Editor::SpriteAnimationAuthoringDocument& document)
{
    try {
        SavedDocumentBaseline baseline{};
        baseline.assetId = document.clipId();
        const std::span<const std::byte> bytes = document.payloadBytes();
        baseline.primaryBytes.assign(bytes.begin(), bytes.end());
        baseline.captured = true;
        return baseline;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "SpriteAnimation saved baseline allocation failed");
    }
}

[[nodiscard]] bool baselineBytesMatch(const std::vector<std::byte>& baseline,
                                      std::span<const std::byte> current) noexcept
{
    return baseline.size() == current.size() &&
           std::equal(baseline.begin(), baseline.end(), current.begin());
}

[[nodiscard]] bool savedBaselineMatches(
    const SavedDocumentBaseline& baseline,
    const Tina::Editor::World2DAuthoringDocument& document) noexcept
{
    return baseline.captured &&
           baselineBytesMatch(baseline.primaryBytes, document.snapshotBytes());
}

[[nodiscard]] bool savedBaselineMatches(
    const SavedDocumentBaseline& baseline,
    const Tina::Editor::World3DAuthoringDocument& document) noexcept
{
    return baseline.captured &&
           baselineBytesMatch(baseline.primaryBytes, document.payloadBytes());
}

[[nodiscard]] bool savedBaselineMatches(
    const SavedDocumentBaseline& baseline,
    const Tina::Editor::TileMapAuthoringDocument& document) noexcept
{
    if (!baseline.captured || baseline.assetId != document.tileMapId() ||
        !baselineBytesMatch(baseline.primaryBytes, document.rootPayloadBytes()) ||
        baseline.tileMapChunks.size() != document.chunkCount()) {
        return false;
    }
    for (Tina::Core::usize index = 0; index < baseline.tileMapChunks.size(); ++index) {
        const auto current = document.chunkPayloadAt(index);
        if (!current || current->assetId != baseline.tileMapChunks[index].assetId ||
            !baselineBytesMatch(baseline.tileMapChunks[index].payloadBytes,
                                current->payloadBytes)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool savedBaselineMatches(
    const SavedDocumentBaseline& baseline,
    const Tina::Editor::SpriteAnimationAuthoringDocument& document) noexcept
{
    return baseline.captured && baseline.assetId == document.clipId() &&
           baselineBytesMatch(baseline.primaryBytes, document.payloadBytes());
}

struct WorkspaceSessionState final {
    Tina::Editor::EditorDocumentKey key{};
    std::string documentPathUtf8{};
    SavedDocumentBaseline savedBaseline{};
    Tina::AssetFormat::TargetPlatform targetPlatform =
        Tina::AssetFormat::TargetPlatform::WindowsX64;
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

// A Catalog-backed authoring tab keeps its own document while another tab of
// the same kind is active. The active document of each kind remains in the
// existing members so the established preview/editor code can use one owner.
using TabAuthoringDocument =
    std::variant<Tina::Editor::World3DAuthoringDocument,
                 Tina::Editor::TileMapAuthoringDocument,
                 Tina::Editor::SpriteAnimationAuthoringDocument>;

struct LoadedProjectAssetDocument final {
    std::optional<TabAuthoringDocument> document{};
    Tina::AssetFormat::TargetPlatform targetPlatform =
        Tina::AssetFormat::TargetPlatform::WindowsX64;
};

struct SuspendedTabAuthoringDocument final {
    Tina::Editor::EditorDocumentKey key{};
    TabAuthoringDocument document;
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
    world2DSession.key = {
        .kind = Tina::Editor::EditorDocumentKind::World2D,
    };
    world3DSession.key = {
        .kind = Tina::Editor::EditorDocumentKind::World3D,
    };
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
            auto baseline = captureSavedBaseline(*document);
            if (!baseline) {
                return Tina::Core::failure(std::move(baseline.error()));
            }
            world2DSession.savedBaseline = std::move(*baseline);
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
            auto baseline = captureSavedBaseline(*world3D);
            if (!baseline) {
                return Tina::Core::failure(std::move(baseline.error()));
            }
            world3DSession.savedBaseline = std::move(*baseline);
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
                            .name = "Player Spawn",
                            .x = 1.5F,
                            .y = 1.5F,
                            .properties = {{.key = "archetype", .value = "player"}},
                        },
                        Tina::Editor::TileMapAuthoringObject{
                            .stableObjectId = 2,
                            .kind = Tina::AssetFormat::TileMapObjectKind::Rectangle,
                            .name = "Crate Spawn",
                            .x = 4.0F,
                            .y = 1.0F,
                            .width = 1.0F,
                            .height = 1.0F,
                            .properties = {{.key = "archetype", .value = "crate"}},
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

[[nodiscard]] Tina::Core::Result<Tina::Editor::ProjectAssetBrowserModel>
createProjectAssetBrowser(const Tina::Asset::CatalogSnapshot& catalog)
{
    try {
        std::vector<Tina::Editor::ProjectAssetDescriptor> assets;
        assets.reserve(catalog.entryCount());
        for (u32 index = 0; index < catalog.entryCount(); ++index) {
            const auto entry = catalog.entry(index);
            if (!entry) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "Tina Editor Catalog entry disappeared");
            }
            const auto idText = entry->assetId.canonicalText();
            std::string displayName{Tina::Editor::projectAssetKindLabel(entry->assetKind)};
            displayName += "  ";
            displayName.append(idText.data(), 8U);
            std::vector<Tina::AssetFormat::AssetDependency> dependencies;
            dependencies.reserve(entry->dependencyCount);
            for (u32 dependencyIndex = 0;
                 dependencyIndex < entry->dependencyCount;
                 ++dependencyIndex) {
                const auto dependency = catalog.dependency(index, dependencyIndex);
                if (!dependency) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::Internal,
                        "Tina Editor Catalog dependency disappeared");
                }
                dependencies.push_back(Tina::AssetFormat::AssetDependency{
                    .assetId = dependency->assetId,
                    .expectedKind = dependency->expectedKind,
                    .flags = dependency->flags,
                });
            }
            assets.push_back(Tina::Editor::ProjectAssetDescriptor{
                .assetId = entry->assetId,
                .assetKind = entry->assetKind,
                .assetTypeVersion = entry->assetTypeVersion,
                .dependencyCount = entry->dependencyCount,
                .cookedFileBytes = entry->cookedFileBytes,
                .displayName = std::move(displayName),
                .dependencies = std::move(dependencies),
            });
        }
        return Tina::Editor::ProjectAssetBrowserModel::Create(
            assets, Tina::Editor::ProjectAssetBrowserConfig{
                        .itemCapacity = (std::max)(Tina::Core::usize{4096},
                                                  assets.size()),
                        .dependencyCapacity = (std::max)(
                            Tina::Core::usize{16384},
                            static_cast<Tina::Core::usize>(catalog.dependencyCount())),
                    });
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Tina Editor Project browser allocation failed");
    }
}

[[nodiscard]] Tina::Core::Result<Tina::Editor::ProjectAssetBrowserModel>
createProjectAssetBrowser(const EditorAssetResources& resources)
{
    if (!resources.system.has_value() || resources.system->catalog() == nullptr) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Tina Editor Project browser requires an open Catalog");
    }
    return createProjectAssetBrowser(*resources.system->catalog());
}

[[nodiscard]] Tina::Core::Result<Tina::Editor::EditorDocumentTabs>
createEditorDocumentTabs(WorkspaceMode initialWorkspace)
{
    const std::array initialTabs{
        Tina::Editor::EditorDocumentTabDesc{
            .key = {.kind = Tina::Editor::EditorDocumentKind::World2D},
            .title = "World2D",
            .pinned = true,
        },
        Tina::Editor::EditorDocumentTabDesc{
            .key = {.kind = Tina::Editor::EditorDocumentKind::World3D},
            .title = "World3D",
            .pinned = true,
        },
        Tina::Editor::EditorDocumentTabDesc{
            .key = {.kind = Tina::Editor::EditorDocumentKind::TileMap2D,
                    .assetId = editorAssetId(0x42U)},
            .title = "TileMap",
            .pinned = true,
        },
        Tina::Editor::EditorDocumentTabDesc{
            .key = {.kind = Tina::Editor::EditorDocumentKind::SpriteAnimation2D,
                    .assetId = editorAssetId(0x50U)},
            .title = "Animation",
            .pinned = true,
        },
    };
    auto tabs = Tina::Editor::EditorDocumentTabs::Create(
        initialTabs, Tina::Editor::EditorDocumentTabsConfig{
                         .tabCapacity = DocumentTabSlots,
                         .titleByteCapacity = 96,
                     });
    if (!tabs) {
        return Tina::Core::failure(std::move(tabs.error()));
    }
    if (auto status = tabs->activate(initialWorkspace == WorkspaceMode::World2D ? 0U : 1U);
        !status) {
        return Tina::Core::failure(std::move(status.error()));
    }
    return tabs;
}

[[nodiscard]] Tina::Asset::CatalogPackageStageConfig createEditorImportStageConfig(
    std::pmr::memory_resource* memoryResource) noexcept
{
    Tina::Asset::CatalogPackageStageConfig config{};
    config.validation.manifest.catalog.maxEntries = 4096;
    config.validation.manifest.catalog.maxDependencies = 16384;
    config.validation.manifest.catalog.maxDependenciesPerAsset = 4096;
    config.validation.manifest.catalog.memoryResource = memoryResource;
    config.validation.validation.file.memoryResource = memoryResource;
    config.validation.validation.verifyTypedPayload = true;
    return config;
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
                         Tina::Editor::ProjectAssetBrowserModel projectAssets,
                         Tina::Editor::EditorDocumentTabs documentTabs,
                         EditorAssetResources& assetResources,
                         EditorRenderDeviceAccess& renderDeviceAccess)
        : options_(std::move(options)), counters_(counters), document_(std::move(world2D)),
          document3D_(std::move(world3D)), tileMapDocument_(std::move(tileMap)),
          spriteAnimationDocument_(std::move(spriteAnimation)),
          workspaceMode_(options_.initialWorkspace),
          world2DSession_(std::move(world2DSession)),
          world3DSession_(std::move(world3DSession)),
          projectAssets_(std::move(projectAssets)),
          documentTabs_(std::move(documentTabs)), assetResources_(assetResources),
          renderDeviceAccess_(renderDeviceAccess),
          sourceImportService_(
              Tina::EditorApp::Detail::makeEditorSourceImportPipelineWorker(
                  createEditorImportStageConfig(&sourceImportMemory_)))
    {
        hierarchyRows_.reserve(AuthoringEntityCapacity + 1U);
        collapsedHierarchyIds_.reserve(AuthoringEntityCapacity + 1U);
        activeProjectWorkspace_ = std::move(assetResources_.initialProjectWorkspace);
        sourceImportStartPending_ = options_.sourceImport.importOnStart;
        if (options_.sourceImport.intendedUnits.empty()) {
            sourceImportUnits_ = std::move(assetResources_.initialSourceImportUnits);
        } else {
            sourceImportUnits_.reserve(options_.sourceImport.intendedUnits.size());
            for (const auto& unit : options_.sourceImport.intendedUnits) {
                const bool isRecipe =
                    unit.kind ==
                    Tina::EditorApp::Detail::EditorSourceImportLaunchUnitKind::CatalogRecipe;
                sourceImportUnits_.push_back({
                    .kind = isRecipe
                                ? Tina::EditorApp::Detail::EditorSourceImportUnitKind::CatalogRecipe
                                : Tina::EditorApp::Detail::EditorSourceImportUnitKind::Gltf,
                    .sourcePathUtf8 = unit.pathUtf8,
                });
            }
        }
        counters_.sourceImportIntendedUnits = sourceImportUnits_.size();
        if (const auto* tab = documentTabs_.tab(1); tab != nullptr) {
            world3DDocumentOwnerKey_ = tab->key;
        }
        if (const auto* tab = documentTabs_.tab(2); tab != nullptr) {
            tileMapDocumentOwnerKey_ = tab->key;
        }
        if (const auto* tab = documentTabs_.tab(3); tab != nullptr) {
            spriteAnimationDocumentOwnerKey_ = tab->key;
        }
    }

    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override
    {
        ++counters_.stateEnters;
        auto playSession = Tina::Editor::EditorPlaySession::Create();
        if (!playSession) {
            return Tina::Core::failure(std::move(playSession.error()));
        }
        playSession_.emplace(std::move(*playSession));
        if (auto status = initializePinnedDocumentSessions(); !status) {
            return status;
        }
        if (auto status = rebuildHierarchyModel(); !status) {
            return status;
        }
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
        UI::UILayoutStyle pathStyle = fixedSize(0.0F, 30.0F);
        pathStyle.size.width = UI::UILayoutLength::Auto();
        pathStyle.flexItem.grow = 1.0F;
        pathStyle.flexItem.shrink = 1.0F;
        pathStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        const WorkspaceSessionState& initialSession = activeWorkspaceSession();
        if (auto status = storeNode(createTextEdit(toolbar,
                                                   initialSession.documentPathUtf8,
                                                   pathStyle, true),
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
        if (auto status = storeNode(createButton(toolbar, "Play", fixedSize(52.0F, 30.0F)),
                                    playButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "Pause", fixedSize(56.0F, 30.0F), false),
                                    pauseButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "Step", fixedSize(48.0F, 30.0F), false),
                                    stepButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(toolbar, "Stop", fixedSize(48.0F, 30.0F), false),
                                    stopButton_);
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
        if (auto status = storeNode(createButton(toolbar, "Save As", fixedSize(72.0F, 30.0F)),
                                    saveAsButton_);
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
                                                    ? "Scene / World2D"
                                                    : "Scene / World3D",
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
                                    translateToolButtons_[0]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(contextBar, "Rotate", fixedSize(62.0F, 26.0F)),
                                    rotateToolButtons_[0]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(contextBar, "Scale", fixedSize(56.0F, 26.0F)),
                                    scaleToolButtons_[0]);
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

        UI::UINodeId documentTabsBar{};
        UI::UILayoutStyle documentTabsBarStyle = fillWidth(34.0F);
        documentTabsBarStyle.flexItem.shrink = 0.0F;
        documentTabsBarStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        documentTabsBarStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        documentTabsBarStyle.flexContainer.gap.column = 6.0F;
        documentTabsBarStyle.padding = UI::UIEdgeSpacing::HorizontalVertical(8.0F, 3.0F);
        if (auto status = storeNode(createPanel(rootNode, documentTabsBarStyle,
                                                UI::UIStyleRoleId::PanelSurface, dockClass_),
                                    documentTabsBar);
            !status) {
            return status;
        }
        UI::UINodeId documentTabsTitle{};
        if (auto status = storeNode(createLabel(documentTabsBar, "Documents",
                                                fixedSize(78.0F, 22.0F), secondaryText),
                                    documentTabsTitle);
            !status) {
            return status;
        }
        UI::UILayoutStyle documentTabStyle = fixedSize(0.0F, 28.0F);
        documentTabStyle.size.width = UI::UILayoutLength::Auto();
        documentTabStyle.flexItem.grow = 1.0F;
        documentTabStyle.flexItem.shrink = 1.0F;
        documentTabStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        for (u32 index = 0; index < DocumentTabSlots; ++index) {
            const auto* tab = documentTabs_.tab(index);
            if (auto status = storeNode(createButton(documentTabsBar,
                                                     tab != nullptr ? tab->title : "Empty",
                                                     documentTabStyle,
                                                     tab != nullptr &&
                                                         index != documentTabs_.activeIndex()),
                                        documentTabButtons_[index]);
                !status) {
                return status;
            }
        }
        if (auto status = storeNode(createButton(documentTabsBar, "Close",
                                                 fixedSize(58.0F, 28.0F), false),
                                    closeDocumentButton_);
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
        std::string hierarchyCountText =
            std::to_string(hierarchyRows_.empty() ? 0U : hierarchyRows_.size() - 1U);
        hierarchyCountText += " nodes";
        if (auto status = storeNode(createLabel(hierarchyHeader, hierarchyCountText,
                                                fixedSize(72.0F, 20.0F),
                                                secondaryText),
                                    hierarchyCount_);
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
        UI::UILayoutStyle hierarchyActionsStyle = fillWidth(66.0F);
        hierarchyActionsStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        hierarchyActionsStyle.flexContainer.gap.row = 6.0F;
        if (auto status = storeNode(createPanel(left, hierarchyActionsStyle,
                                                UI::UIStyleRoleId::None),
                                    hierarchyActions);
            !status) {
            return status;
        }
        UI::UINodeId hierarchyPrimaryActions{};
        UI::UILayoutStyle hierarchyActionRowStyle = fillWidth(30.0F);
        hierarchyActionRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        hierarchyActionRowStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(hierarchyActions,
                                                hierarchyActionRowStyle,
                                                UI::UIStyleRoleId::None),
                                    hierarchyPrimaryActions);
            !status) {
            return status;
        }
        UI::UINodeId hierarchySecondaryActions{};
        if (auto status = storeNode(createPanel(hierarchyActions,
                                                hierarchyActionRowStyle,
                                                UI::UIStyleRoleId::None),
                                    hierarchySecondaryActions);
            !status) {
            return status;
        }
        UI::UILayoutStyle hierarchyActionStyle = fixedSize(0.0F, 30.0F);
        hierarchyActionStyle.size.width = UI::UILayoutLength::Auto();
        hierarchyActionStyle.flexItem.grow = 1.0F;
        hierarchyActionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(createButton(hierarchyPrimaryActions, "+",
                                                 hierarchyActionStyle),
                                    addEntityButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(hierarchyPrimaryActions, "Duplicate",
                                                 hierarchyActionStyle, false),
                                    duplicateEntityButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(hierarchyPrimaryActions, "Delete",
                                                 hierarchyActionStyle, false),
                                    deleteEntityButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(hierarchySecondaryActions, "To Root",
                                                 hierarchyActionStyle, false),
                                    reparentEntityRootButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(hierarchySecondaryActions, "Focus",
                                                 hierarchyActionStyle, false),
                                    focusEntityButton_);
            !status) {
            return status;
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

        UI::UINodeId projectHeader{};
        UI::UILayoutStyle projectHeaderStyle = fillWidth(28.0F);
        projectHeaderStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        projectHeaderStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        projectHeaderStyle.flexContainer.justifyContent = UI::UIJustifyContent::SpaceBetween;
        if (auto status = storeNode(createPanel(left, projectHeaderStyle, UI::UIStyleRoleId::None),
                                    projectHeader);
            !status) {
            return status;
        }
        UI::UINodeId projectTitle{};
        if (auto status = storeNode(createLabel(projectHeader, "Project Assets",
                                                fixedSize(118.0F, 24.0F), sectionText),
                                    projectTitle);
            !status) {
            return status;
        }
        std::string initialProjectCount = std::to_string(projectAssets_.visibleItemCount());
        initialProjectCount += " / ";
        initialProjectCount += std::to_string(projectAssets_.itemCount());
        if (auto status = storeNode(createLabel(projectHeader, initialProjectCount,
                                                fixedSize(58.0F, 20.0F), secondaryText),
                                    projectAssetCount_);
            !status) {
            return status;
        }

        UI::UINodeId projectFilters{};
        UI::UILayoutStyle projectFiltersStyle = fillWidth(28.0F);
        projectFiltersStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        projectFiltersStyle.flexContainer.gap.column = 4.0F;
        if (auto status = storeNode(createPanel(left, projectFiltersStyle,
                                                UI::UIStyleRoleId::None),
                                    projectFilters);
            !status) {
            return status;
        }
        const std::array<std::string_view, 4> projectFilterLabels{"All", "2D", "3D", "Media"};
        for (u32 index = 0; index < projectFilterLabels.size(); ++index) {
            UI::UILayoutStyle filterStyle = fixedSize(0.0F, 28.0F);
            filterStyle.size.width = UI::UILayoutLength::Auto();
            filterStyle.flexItem.grow = 1.0F;
            filterStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
            if (auto status = storeNode(createButton(projectFilters, projectFilterLabels[index],
                                                     filterStyle, index != 0U),
                                        projectFilterButtons_[index]);
                !status) {
                return status;
            }
        }

        UI::UILayoutStyle projectListStyle = growingRegion();
        projectListStyle.minMax.minHeight = UI::UILayoutLength::Px(128.0F);
        auto projectList = tree->createElement(
            left, UI::makeListViewElement(
                      {.materializedItemCapacity = AssetBrowserMaterializedCapacity},
                      projectListStyle));
        if (!projectList) {
            return Tina::Core::failure(std::move(projectList.error()));
        }
        projectAssetList_ = *projectList;
        if (auto status = tree->setListViewStyle(
                projectAssetList_,
                UI::UIListViewStyle{
                    .rowHeight = 32.0F,
                    .overscanRows = 1,
                    .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                    .wheelStep = 32.0F,
                });
            !status) {
            return status;
        }
        if (auto status = tree->setListViewPaint(projectAssetList_,
                                                 UI::makeListViewPaint(productTheme));
            !status) {
            return status;
        }
        if (auto status = tree->setListViewDataSource(projectAssetList_,
                                                      projectAssetDataSource());
            !status) {
            return status;
        }
        if (projectAssets_.visibleItemCount() != 0U) {
            if (auto status = tree->setListViewSelectedIndex(projectAssetList_, 0U); !status) {
                return status;
            }
            observedProjectAssetSelectionIndex_ = 0U;
        }

        UI::UINodeId projectLifecycleActions{};
        UI::UILayoutStyle projectLifecycleActionsStyle = fillWidth(30.0F);
        projectLifecycleActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        projectLifecycleActionsStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(left, projectLifecycleActionsStyle,
                                                UI::UIStyleRoleId::None),
                                    projectLifecycleActions);
            !status) {
            return status;
        }
        UI::UILayoutStyle projectLifecycleActionStyle = fixedSize(0.0F, 30.0F);
        projectLifecycleActionStyle.size.width = UI::UILayoutLength::Auto();
        projectLifecycleActionStyle.flexItem.grow = 1.0F;
        projectLifecycleActionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(createButton(projectLifecycleActions, "New Project",
                                                 projectLifecycleActionStyle),
                                    createProjectButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(projectLifecycleActions, "Open Project",
                                                 projectLifecycleActionStyle),
                                    openProjectButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(
                                        projectLifecycleActions, "Import Source",
                                        projectLifecycleActionStyle,
                                        activeProjectWorkspace_.has_value()),
                                    importSourceButton_);
            !status) {
            return status;
        }

        UI::UINodeId projectActions{};
        UI::UILayoutStyle projectActionsStyle = fillWidth(30.0F);
        projectActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        projectActionsStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(left, projectActionsStyle,
                                                UI::UIStyleRoleId::None),
                                    projectActions);
            !status) {
            return status;
        }
        UI::UILayoutStyle projectActionStyle = fixedSize(0.0F, 30.0F);
        projectActionStyle.size.width = UI::UILayoutLength::Auto();
        projectActionStyle.flexItem.grow = 1.0F;
        projectActionStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(createButton(projectActions, "Open Asset",
                                                 projectActionStyle,
                                                 projectAssets_.visibleItemCount() != 0U),
                                    openProjectAssetButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(
                                        projectActions, "Refresh", projectActionStyle,
                                        assetResources_.projectCatalogConfigured),
                                    refreshProjectCatalogButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(projectActions, "Catalog",
                                                fixedSize(52.0F, 20.0F), accentText),
                                    projectAssetSource_);
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
        if (auto status = storeNode(
                createButton(viewportHeader,
                             workspaceMode_ == WorkspaceMode::World2D
                                 ? "Orthographic"
                                 : "View: Custom",
                             fixedSize(136.0F, 28.0F),
                             workspaceMode_ == WorkspaceMode::World3D),
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
                                    translateToolButtons_[1]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportTools, "Rotate", fixedSize(62.0F, 28.0F)),
                                    rotateToolButtons_[1]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportTools, "Scale", fixedSize(56.0F, 28.0F)),
                                    scaleToolButtons_[1]);
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
        UI::UINodeId viewportOptions{};
        UI::UILayoutStyle viewportOptionsStyle = fillWidth(32.0F);
        viewportOptionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        viewportOptionsStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        viewportOptionsStyle.flexContainer.gap.column = 6.0F;
        if (auto status = storeNode(createPanel(center, viewportOptionsStyle,
                                                UI::UIStyleRoleId::PanelSurface),
                                    viewportOptions);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportOptions, "World", fixedSize(56.0F, 28.0F)),
                                    orientationButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportOptions, "Snap Off", fixedSize(64.0F, 28.0F)),
                                    snapButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportOptions, "Replace", fixedSize(58.0F, 28.0F), false),
                                    marqueeModeButtons_[0]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportOptions, "Add", fixedSize(40.0F, 28.0F)),
                                    marqueeModeButtons_[1]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportOptions, "Toggle", fixedSize(52.0F, 28.0F)),
                                    marqueeModeButtons_[2]);
            !status) {
            return status;
        }
        if (auto status = storeNode(createButton(viewportOptions, "Frame All", fixedSize(64.0F, 28.0F)),
                                    frameAllButton_);
            !status) {
            return status;
        }
        UI::UILayoutStyle zoomSliderStyle = fixedSize(0.0F, 24.0F);
        zoomSliderStyle.size.width = UI::UILayoutLength::Auto();
        zoomSliderStyle.flexItem.grow = 1.0F;
        zoomSliderStyle.flexItem.shrink = 1.0F;
        zoomSliderStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        UI::UIElementDescriptor zoomSliderDesc = UI::makeSliderElement(zoomSliderStyle);
        if (auto status = storeNode(tree->createElement(viewportOptions, zoomSliderDesc), zoomSlider_); !status) {
            return status;
        }
        if (auto status = tree->setSliderRange(zoomSlider_, 25.0F, 400.0F, 25.0F); !status) {
            return status;
        }
        if (auto status = tree->setSliderValue(zoomSlider_, viewportZoomPercent_); !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(viewportOptions, "100%", fixedSize(42.0F, 20.0F), accentText),
                                    zoomValue_);
            !status) {
            return status;
        }

        UI::UINodeId viewportCanvas{};
        UI::UILayoutStyle viewportCanvasStyle = growingRegion();
        viewportCanvasStyle.minMax.minHeight = UI::UILayoutLength::Px(220.0F);
        viewportCanvasStyle.padding = UI::UIEdgeSpacing::All(5.0F);
        viewportCanvasStyle.flexContainer.gap.row = 4.0F;
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
                                                fixedSize(160.0F, 20.0F), secondaryText),
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
        viewportSceneAreaStyle.flexContainer.justifyContent = UI::UIJustifyContent::Start;
        viewportSceneAreaStyle.flexContainer.alignItems = UI::UIAxisAlignment::Stretch;
        if (auto status = storeNode(createPanel(viewportCanvas, viewportSceneAreaStyle,
                                                UI::UIStyleRoleId::None),
                                    viewportSceneArea);
            !status) {
            return status;
        }
        UI::UINodeId previewFrame{};
        UI::UILayoutStyle previewFrameStyle = growingRegion();
        previewFrameStyle.minMax.minHeight = UI::UILayoutLength::Px(180.0F);
        previewFrameStyle.flexContainer.direction = UI::UIFlexDirection::Column;
        previewFrameStyle.flexContainer.justifyContent = UI::UIJustifyContent::Start;
        previewFrameStyle.flexContainer.alignItems = UI::UIAxisAlignment::Stretch;
        if (auto status = storeNode(createPanel(viewportSceneArea, previewFrameStyle,
                                                UI::UIStyleRoleId::None),
                                    previewFrame);
            !status) {
            return status;
        }
        UI::UILayoutStyle previewWorldLayerStyle = growingRegion();
        previewWorldLayerStyle.minMax.minHeight = UI::UILayoutLength::Px(180.0F);
        previewWorldLayerStyle.placement = UI::UILayoutPlacement::Flow;
        if (auto status = storeNode(createPanel(previewFrame, previewWorldLayerStyle,
                                                UI::UIStyleRoleId::None),
                                    viewportPreviewLayer_);
            !status) {
            return status;
        }
        for (UI::UINodeId& gridNode : viewportGridNodes_) {
            UI::UILayoutStyle gridLineStyle = fixedSize(1.0F, 1.0F);
            gridLineStyle.placement = UI::UILayoutPlacement::Overlay;
            gridLineStyle.visibility = UI::UIVisibility::Collapsed;
            UI::UIElementDescriptor gridLine = UI::makePanelElement(gridLineStyle);
            gridLine.visual.boxPaint = UI::makeSolidBox(UI::rgb(0x7A8797, 64));
            gridLine.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
            gridLine.semantics.mode = UI::UISemanticsMode::Exclude;
            if (auto status = storeNode(
                    tree->createElement(viewportPreviewLayer_, gridLine), gridNode);
                !status) {
                return status;
            }
        }
        for (UI::UINodeId& gizmoNode : viewportGizmoVisualNodes_) {
            UI::UILayoutStyle gizmoStyle = fixedSize(4.0F, 4.0F);
            gizmoStyle.placement = UI::UILayoutPlacement::Overlay;
            gizmoStyle.visibility = UI::UIVisibility::Collapsed;
            UI::UIElementDescriptor gizmoPoint = UI::makePanelElement(gizmoStyle);
            gizmoPoint.visual.boxPaint = UI::makeSolidBox(UI::rgb(0xE16060, 230));
            gizmoPoint.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
            gizmoPoint.semantics.mode = UI::UISemanticsMode::Exclude;
            if (auto status = storeNode(
                    tree->createElement(viewportPreviewLayer_, gizmoPoint), gizmoNode);
                !status) {
                return status;
            }
        }
        UI::UILayoutStyle marqueeStyle = fixedSize(1.0F, 1.0F);
        marqueeStyle.placement = UI::UILayoutPlacement::Overlay;
        marqueeStyle.visibility = UI::UIVisibility::Collapsed;
        UI::UIElementDescriptor marquee = UI::makePanelElement(marqueeStyle);
        marquee.visual.boxPaint = UI::makeSolidBox(UI::rgb(0x4C9AFF, 52));
        marquee.pointerHitPolicy = UI::UIPointerHitPolicy::Ignore;
        marquee.semantics.mode = UI::UISemanticsMode::Exclude;
        if (auto status = storeNode(
                tree->createElement(viewportPreviewLayer_, marquee), viewportMarqueeNode_);
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
                                                    ? "Camera2D | MMB Pan | Wheel Zoom"
                                                    : "Camera3D | MMB Pan | RMB Orbit | Wheel Dolly",
                                                fixedSize(246.0F, 20.0F), bodyText),
                                    cameraStatus_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(viewportFooter, "Select | Local | Snap",
                                                fixedSize(190.0F, 20.0F), secondaryText),
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
        if (auto status = storeNode(createLabel(inspectorHeader, "Selection", fixedSize(88.0F, 20.0F),
                                                accentText),
                                    inspectorMode_);
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
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(42.0F), secondaryText),
                                    inspectorAssetPath_);
            !status) {
            return status;
        }
        if (auto status = storeNode(createLabel(inspectorContent, {}, fillWidth(22.0F), sectionText),
                                    inspectorDependencySummary_);
            !status) {
            return status;
        }
        auto inspectorDependencies = tree->createElement(
            inspectorContent,
            UI::makeListViewElement(
                {.materializedItemCapacity = 6}, fillWidth(156.0F)));
        if (!inspectorDependencies) {
            return Tina::Core::failure(std::move(inspectorDependencies.error()));
        }
        inspectorDependencyList_ = *inspectorDependencies;
        if (auto status = tree->setListViewStyle(
                inspectorDependencyList_,
                UI::UIListViewStyle{
                    .rowHeight = 36.0F,
                    .overscanRows = 1,
                    .scrollBarVisibility = UI::UIScrollBarVisibility::Auto,
                    .wheelStep = 36.0F,
                });
            !status) {
            return status;
        }
        if (auto status = tree->setListViewPaint(
                inspectorDependencyList_, UI::makeListViewPaint(productTheme));
            !status) {
            return status;
        }
        if (auto status = tree->setListViewDataSource(
                inspectorDependencyList_, inspectorDependencyDataSource());
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

        UI::UINodeId hierarchyInspectorTitle{};
        if (auto status = storeNode(
                createLabel(inspectorContent, "Hierarchy", fillWidth(22.0F), sectionText),
                hierarchyInspectorTitle);
            !status) {
            return status;
        }
        UI::UINodeId parentRow{};
        UI::UILayoutStyle parentRowStyle = fillWidth(30.0F);
        parentRowStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        parentRowStyle.flexContainer.alignItems = UI::UIAxisAlignment::Center;
        parentRowStyle.flexContainer.gap.column = 8.0F;
        if (auto status = storeNode(
                createPanel(inspectorContent, parentRowStyle, UI::UIStyleRoleId::None),
                parentRow);
            !status) {
            return status;
        }
        UI::UINodeId parentCaption{};
        if (auto status = storeNode(
                createLabel(parentRow, "Parent Stable ID", fixedSize(116.0F, 20.0F), secondaryText),
                parentCaption);
            !status) {
            return status;
        }
        UI::UILayoutStyle parentValueStyle = fixedSize(0.0F, 30.0F);
        parentValueStyle.size.width = UI::UILayoutLength::Auto();
        parentValueStyle.flexItem.grow = 1.0F;
        parentValueStyle.flexItem.shrink = 1.0F;
        parentValueStyle.flexItem.basis = UI::UILayoutLength::Px(0.0F);
        if (auto status = storeNode(
                createTextEdit(parentRow, "0", parentValueStyle, false),
                inspectorParentStableId_);
            !status) {
            return status;
        }
        if (auto status = storeNode(
                createButton(inspectorContent, "Apply Parent", fillWidth(34.0F)),
                reparentEntityButton_);
            !status) {
            return status;
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
        UI::UINodeId tileMapGameplayRow{};
        if (auto status = createTileMapActionRow(tileMapGameplayRow); !status) {
            return status;
        }
        if (auto status = storeNode(createButton(tileMapGameplayRow, "Generate Gameplay",
                                                 fixedSize(214.0F, 30.0F)),
                                    generateTileMapGameplayButton_);
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

        if (auto status = storeNode(
                tree->createElement(
                    rootNode,
                    UI::makeModalElement(
                        dirtyCloseModalLayout(UI::UIVisibility::Collapsed))),
                dirtyCloseModal_);
            !status) {
            return status;
        }
        if (auto status = storeNode(
                createLabel(dirtyCloseModal_, "Save changes before closing?",
                            fillWidth(28.0F), sectionText),
                dirtyCloseTitle_);
            !status) {
            return status;
        }
        if (auto status = storeNode(
                createLabel(dirtyCloseModal_,
                            "The current canonical document has unsaved changes.",
                            fillWidth(24.0F), bodyText),
                dirtyCloseMessage_);
            !status) {
            return status;
        }
        if (auto status = storeNode(
                createTextEdit(dirtyCloseModal_, {}, fillWidth(32.0F), true),
                dirtyClosePath_);
            !status) {
            return status;
        }
        UI::UINodeId dirtyCloseActions{};
        UI::UILayoutStyle dirtyCloseActionsStyle = fillWidth(34.0F);
        dirtyCloseActionsStyle.flexContainer.direction = UI::UIFlexDirection::Row;
        dirtyCloseActionsStyle.flexContainer.justifyContent =
            UI::UIJustifyContent::End;
        dirtyCloseActionsStyle.flexContainer.gap.column = 8.0F;
        if (auto status = storeNode(
                createPanel(dirtyCloseModal_, dirtyCloseActionsStyle,
                            UI::UIStyleRoleId::None),
                dirtyCloseActions);
            !status) {
            return status;
        }
        if (auto status = storeNode(
                createButton(dirtyCloseActions, "Save", fixedSize(86.0F, 32.0F)),
                dirtyCloseSaveButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(
                createButton(dirtyCloseActions, "Discard", fixedSize(86.0F, 32.0F)),
                dirtyCloseDiscardButton_);
            !status) {
            return status;
        }
        if (auto status = storeNode(
                createButton(dirtyCloseActions, "Cancel", fixedSize(86.0F, 32.0F)),
                dirtyCloseCancelButton_);
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
                viewportMode_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::ViewportCyclePreset);
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
        if (auto status = tree->setButtonAction(
                saveAsButton_, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::SaveAs);
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
        const std::array sceneCommandBindings{
            std::pair{addEntityButton_, EditorCommand::SceneAdd},
            std::pair{duplicateEntityButton_, EditorCommand::SceneDuplicate},
            std::pair{deleteEntityButton_, EditorCommand::SceneDelete},
            std::pair{reparentEntityRootButton_, EditorCommand::SceneReparentRoot},
            std::pair{reparentEntityButton_, EditorCommand::SceneReparent},
            std::pair{focusEntityButton_, EditorCommand::SceneFocus},
        };
        for (const auto& [button, command] : sceneCommandBindings) {
            if (auto status = bindEditorCommand(button, command); !status) {
                return status;
            }
        }
        const std::array playCommandBindings{
            std::pair{playButton_, EditorCommand::PlayStartOrResume},
            std::pair{pauseButton_, EditorCommand::PlayPause},
            std::pair{stepButton_, EditorCommand::PlayStep},
            std::pair{stopButton_, EditorCommand::PlayStop},
        };
        for (const auto& [button, command] : playCommandBindings) {
            if (auto status = bindEditorCommand(button, command); !status) {
                return status;
            }
        }
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
        if (auto status = bindEditorCommand(generateTileMapGameplayButton_,
                                            EditorCommand::GenerateTileMapGameplay);
            !status) {
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
        for (const UI::UINodeId button : translateToolButtons_) {
            if (auto status = tree->setButtonAction(
                    button, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                        queueViewportToolMode(ViewportToolMode::Translate);
                    }});
                !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : rotateToolButtons_) {
            if (auto status = tree->setButtonAction(
                    button, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                        queueViewportToolMode(ViewportToolMode::Rotate);
                    }});
                !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : scaleToolButtons_) {
            if (auto status = tree->setButtonAction(
                    button, UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                        queueViewportToolMode(ViewportToolMode::Scale);
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
        if (auto status = tree->setSliderChangeCallback(
                zoomSlider_,
                UI::UISliderChangeCallback{
                    [this](const UI::UISliderChangeEvent& event) noexcept {
                        pendingViewportZoomPercent_ =
                            std::clamp(event.value, 25.0F, 400.0F);
                    }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                frameAllButton_,
                UI::UIButtonActionCallback{
                    [this](const UI::UIButtonActionEvent&) noexcept {
                        queueEditorCommand(EditorCommand::ViewportResetView);
                    }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                orientationButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    pendingGizmoOrientationToggle_ = true;
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                snapButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    pendingGizmoSnapToggle_ = true;
                }});
            !status) {
            return status;
        }
        constexpr std::array marqueeModes{
            Tina::Editor::EditorMarqueeSelectionMode::Replace,
            Tina::Editor::EditorMarqueeSelectionMode::Add,
            Tina::Editor::EditorMarqueeSelectionMode::Toggle,
        };
        for (u32 index = 0; index < marqueeModeButtons_.size(); ++index) {
            if (auto status = tree->setButtonAction(
                    marqueeModeButtons_[index],
                    UI::UIButtonActionCallback{
                        [this, mode = marqueeModes[index]](
                            const UI::UIButtonActionEvent&) noexcept {
                            pendingMarqueeSelectionMode_ = mode;
                        }});
                !status) {
                return status;
            }
        }
        const std::array projectFilterCommands{
            EditorCommand::ProjectFilterAll,
            EditorCommand::ProjectFilter2D,
            EditorCommand::ProjectFilter3D,
            EditorCommand::ProjectFilterMedia,
        };
        for (u32 index = 0; index < projectFilterButtons_.size(); ++index) {
            if (auto status = tree->setButtonAction(
                    projectFilterButtons_[index],
                    UI::UIButtonActionCallback{
                        [this, command = projectFilterCommands[index]](
                            const UI::UIButtonActionEvent&) noexcept {
                            queueEditorCommand(command);
                        }});
                !status) {
                return status;
            }
        }
        if (auto status = tree->setButtonAction(
                openProjectAssetButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::OpenSelectedProjectAsset);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                refreshProjectCatalogButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::RefreshProjectCatalog);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                createProjectButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::CreateProject);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                openProjectButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::OpenProject);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                importSourceButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::ImportSource);
                }});
            !status) {
            return status;
        }
        if (auto status = tree->setButtonAction(
                closeDocumentButton_,
                UI::UIButtonActionCallback{[this](const UI::UIButtonActionEvent&) noexcept {
                    queueEditorCommand(EditorCommand::CloseActiveDocument);
                }});
            !status) {
            return status;
        }
        const std::array dirtyCloseBindings{
            std::pair{dirtyCloseSaveButton_, EditorCommand::DirtyCloseSave},
            std::pair{dirtyCloseDiscardButton_, EditorCommand::DirtyCloseDiscard},
            std::pair{dirtyCloseCancelButton_, EditorCommand::DirtyCloseCancel},
        };
        for (const auto& [button, command] : dirtyCloseBindings) {
            if (auto status = tree->setButtonAction(
                    button,
                    UI::UIButtonActionCallback{
                        [this, command](const UI::UIButtonActionEvent&) noexcept {
                            queueEditorCommand(command);
                        }});
                !status) {
                return status;
            }
        }
        for (u32 index = 0; index < documentTabButtons_.size(); ++index) {
            if (auto status = tree->setButtonAction(
                    documentTabButtons_[index],
                    UI::UIButtonActionCallback{
                        [this, index](const UI::UIButtonActionEvent&) noexcept {
                            pendingDocumentTabActivation_ = index;
                        }});
                !status) {
                return status;
            }
        }
        if (auto status = tree->setPointerHitPolicy(viewportPreviewLayer_,
                                                    UI::UIPointerHitPolicy::Targetable);
            !status) {
            return status;
        }
        if (auto status = registerViewportPointerListeners(*tree); !status) {
            return status;
        }
        counters_.editorActionsReady = true;
        counters_.editorLayoutRegions = EditorLayoutRegionCount;
        counters_.viewportLayoutReady = true;
        counters_.projectAssetBrowserReady = true;
        counters_.documentTabsReady = true;
        counters_.projectAssetVisibleItems = projectAssets_.visibleItemCount();
        counters_.documentTabCount = documentTabs_.tabCount();
        auto initialSelection = tree->treeViewSelection(hierarchyTree_);
        if (!initialSelection) {
            return Tina::Core::failure(std::move(initialSelection.error()));
        }
        selectionKey_ = initialSelection->key;
        synchronizeViewportSelectionFromHierarchy();
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
        if (sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Running) {
            (void)sourceImportService_.cancel();
        }
        counters_.sourceImportRunning = false;
        if (playSession_.has_value()) {
            (void)playSession_->stop();
            playSession_.reset();
        }
        resetViewportInteractionState();
        viewportNavigation_.reset();
        viewport2DNavigationInitialized_ = false;
        viewport3DNavigationInitialized_ = false;
        pendingTileCellEdit_.reset();
        pendingTileLayerId_ = 0;
        for (auto& listener : viewportPointerListeners_) {
            listener.reset();
        }
        viewportNormalized_.reset();
        previewBindings_.clear();
        preview3DBindings_.clear();
        previewCamera2D_ = {};
        previewCamera3D_ = {};
        previewTileMap_.reset();
        previewWorld_.reset();
        animationAnimator_.reset();
        releasePreviewAssetBindings();
        if (uiRoot_) {
            uiRoot_.reset();
            ++counters_.uiRootsReleased;
        }
        if (sourceImportCatalogCommitted_) {
            assetResources_.system.reset();
        }
        cleanupFailedSourceImportStage();
        sourceImportCatalogCommitted_ = false;
        ++counters_.stateExits;
    }

    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override
    {
        return {};
    }

    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override
    {
        ++counters_.frameUpdates;
        if (playSessionActive()) {
            auto steps = playSession_->advance(
                context.frameTiming().updateDelta.count());
            if (!steps) {
                return Tina::Core::failure(std::move(steps.error()));
            }
            counters_.playSimulationSteps += *steps;
            counters_.playMaximumSimulationTick = (std::max)(
                counters_.playMaximumSimulationTick,
                playSession_->snapshot().simulationTickCount);
        }
        if (auto status = processEditorShortcuts(context.frameActions()); !status) {
            return status;
        }
        if (pendingProjectSwitch_.has_value()) {
            auto workspace = std::move(*pendingProjectSwitch_);
            pendingProjectSwitch_.reset();
            if (auto status = switchLiveProjectCatalog(std::move(workspace)); !status) {
                return status;
            }
        }
        if (auto status = updateSourceImport(); !status) {
            return status;
        }
        if (catalogRefreshPending_) {
            catalogRefreshPending_ = false;
            if (auto status = refreshProjectCatalog(); !status) {
                return status;
            }
            previewAssetBindingsRefreshPending_ = false;
            projectBrowserUiRefreshPending_ = true;
        }
        if (previewAssetBindingsRefreshPending_) {
            releasePreviewAssetBindings();
            if (auto status = preparePreviewAssetBindings(); !status) {
                return status;
            }
            if (auto status = validateRuntimePreview(); !status) {
                return status;
            }
            ++counters_.previewAssetBindingRefreshes;
            previewAssetBindingsRefreshPending_ = false;
        }
        if (options_.autoDemo &&
            sourceImportService_.state() ==
                Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle) {
            if (automaticDemoStartFrame_ == 0U) {
                automaticDemoStartFrame_ = counters_.frameUpdates;
            }
            const u64 first =
                options_.targetFrameCount > AutomaticAuthoringMinimumFrameCount
                    ? options_.targetFrameCount -
                          AutomaticAuthoringMinimumFrameCount
                    : u64{1};
            const u64 second = (std::max)(
                first + u64{1},
                options_.targetFrameCount - options_.targetFrameCount / u64{6});
            if (!queuedFirstSelection_ && counters_.frameUpdates >= first) {
                const auto stableId = automaticHierarchyStableId(false);
                if (stableId.has_value()) {
                    pendingSelectionStableId_ = *stableId;
                    counters_.automaticTransformStableId = *stableId;
                    pendingViewportTokenColor_ = UI::rgb(0x1A3348);
                    pendingViewportToolMode_ = ViewportToolMode::Translate;
                    queuedFirstSelection_ = true;
                }
            } else if (queuedFirstSelection_ && !queuedSecondStyleUpdate_ &&
                       counters_.frameUpdates >= second) {
                pendingViewportTokenColor_ = UI::rgb(0x0C141E);
                queuedSecondStyleUpdate_ = true;
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
                    if (queueAutomaticViewportNavigation()) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 3:
                    if (prepareAutomaticViewportGizmo(
                            Tina::Editor::EditorTransformGizmoMode::Translate)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 4: {
                    const auto point = automaticViewportGizmoPoint(0.5F);
                    if (point.has_value() && updateViewportGizmo(
                                                 Tina::Platform::PrimaryPointerId,
                                                 *point)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                }
                case 5: {
                    const auto point = automaticViewportGizmoPoint(1.0F);
                    if (point.has_value() && requestViewportGizmoCommit(
                                                 Tina::Platform::PrimaryPointerId,
                                                 *point)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                }
                case 6:
                    if (prepareAutomaticViewportMarquee(
                            Tina::Editor::EditorMarqueeSelectionMode::Replace)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 7:
                    if (prepareAutomaticViewportMarquee(
                            Tina::Editor::EditorMarqueeSelectionMode::Add)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 8:
                    if (prepareAutomaticViewportGizmo(
                            Tina::Editor::EditorTransformGizmoMode::Rotate)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 9: {
                    const auto point = automaticViewportGizmoPoint(0.5F);
                    if (point.has_value() && updateViewportGizmo(
                                                 Tina::Platform::PrimaryPointerId,
                                                 *point)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                }
                case 10: {
                    const auto point = automaticViewportGizmoPoint(1.0F);
                    if (point.has_value() && requestViewportGizmoCommit(
                                                 Tina::Platform::PrimaryPointerId,
                                                 *point)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                }
                case 11:
                    if (prepareAutomaticViewportGizmo(
                            Tina::Editor::EditorTransformGizmoMode::Scale)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 12: {
                    const auto point = automaticViewportGizmoPoint(0.5F);
                    if (point.has_value() && updateViewportGizmo(
                                                 Tina::Platform::PrimaryPointerId,
                                                 *point)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                }
                case 13: {
                    const auto point = automaticViewportGizmoPoint(1.0F);
                    if (point.has_value() && requestViewportGizmoCommit(
                                                 Tina::Platform::PrimaryPointerId,
                                                 *point)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                }
                case 14:
                    if (prepareAutomaticViewportMarquee(
                            Tina::Editor::EditorMarqueeSelectionMode::Toggle)) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 15:
                    (void)queueAutoCommand(EditorCommand::Undo);
                    break;
                case 16:
                    (void)queueAutoCommand(EditorCommand::Redo);
                    break;
                case 17:
                    (void)queueAutoCommand(EditorCommand::SceneAdd);
                    break;
                case 18:
                    if (counters_.automaticAddedStableId != 0U &&
                        stableEntityIdForHierarchyItem(selectionKey_) ==
                            counters_.automaticAddedStableId) {
                        (void)queueAutoCommand(EditorCommand::SceneDuplicate);
                    }
                    break;
                case 19:
                    if (counters_.automaticDuplicatedStableId != 0U &&
                        stableEntityIdForHierarchyItem(selectionKey_) ==
                            counters_.automaticDuplicatedStableId) {
                        (void)queueAutoCommand(EditorCommand::SceneReparentRoot);
                    }
                    break;
                case 20:
                    if (counters_.automaticAddedStableId != 0U &&
                        counters_.automaticDuplicatedStableId != 0U &&
                        stableEntityIdForHierarchyItem(selectionKey_) ==
                            counters_.automaticDuplicatedStableId) {
                        if (queueAutoCommand(EditorCommand::SceneReparent)) {
                            pendingAutoParentStableId_ =
                                counters_.automaticAddedStableId;
                        }
                    }
                    break;
                case 21: {
                    const EditorHierarchyRow* duplicated =
                        hierarchyRow(counters_.automaticDuplicatedStableId);
                    if (duplicated != nullptr &&
                        duplicated->parentStableId ==
                            counters_.automaticAddedStableId &&
                        stableEntityIdForHierarchyItem(selectionKey_) ==
                            counters_.automaticDuplicatedStableId) {
                        (void)queueAutoCommand(EditorCommand::SceneDelete);
                    }
                    break;
                }
                case 22:
                    if (stableEntityIdForHierarchyItem(selectionKey_) ==
                        counters_.automaticAddedStableId) {
                        (void)queueAutoCommand(EditorCommand::SceneDelete);
                    }
                    break;
                case 23:
                    (void)queueAutoCommand(EditorCommand::PlayStartOrResume);
                    break;
                case 24:
                    if (playSession_.has_value() &&
                        playSession_->snapshot().state ==
                            Tina::Editor::EditorPlayState::Playing) {
                        (void)queueAutoCommand(EditorCommand::PlayPause);
                    }
                    break;
                case 25:
                    if (playSession_.has_value() &&
                        playSession_->snapshot().state ==
                            Tina::Editor::EditorPlayState::Paused) {
                        automaticPlayStepBaseline_ =
                            playSession_->snapshot().simulationTickCount;
                        (void)queueAutoCommand(EditorCommand::PlayStep);
                    }
                    break;
                case 26:
                    if (playSession_.has_value() &&
                        playSession_->snapshot().state ==
                            Tina::Editor::EditorPlayState::Paused &&
                        !playSession_->snapshot().stepPending &&
                        playSession_->snapshot().simulationTickCount >
                            automaticPlayStepBaseline_) {
                        (void)queueAutoCommand(EditorCommand::PlayStartOrResume);
                    }
                    break;
                case 27:
                    if (playSession_.has_value() &&
                        playSession_->snapshot().state ==
                            Tina::Editor::EditorPlayState::Playing) {
                        (void)queueAutoCommand(EditorCommand::PlayStop);
                    }
                    break;
                case 28:
                    if (playSession_.has_value() &&
                        playSession_->snapshot().state ==
                            Tina::Editor::EditorPlayState::Editing &&
                        counters_.playStops != 0U) {
                        ++autoAuthoringStage_;
                    }
                    break;
                case 29:
                    if (options_.initialWorkspace != WorkspaceMode::World2D) {
                        ++autoAuthoringStage_;
                    } else if (tileMapEditingContext()) {
                        (void)queueAutoCommand(EditorCommand::GenerateTileMapGameplay);
                    } else if (!pendingDocumentTabActivation_.has_value()) {
                        // Gameplay generation belongs to the TileMap tab. Queue the
                        // tab activation and retry this stage after the retained UI
                        // publishes it; the initial 2D tab is World2D.
                        pendingDocumentTabActivation_ = 2U;
                    }
                    break;
                case 30:
                    if (options_.initialWorkspace == WorkspaceMode::World2D &&
                        tileMapEditingContext()) {
                        // Return to the World2D document before exercising Save;
                        // the TileMap tab owns a separate session/path.
                        (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                    } else if (!activeWorkspaceSession().hasDocumentPath()) {
                        ++autoAuthoringStage_;
                    } else {
                        (void)queueAutoCommand(EditorCommand::Save);
                    }
                    break;
                case 31:
                    (void)queueAutoCommand(options_.initialWorkspace == WorkspaceMode::World2D
                                               ? EditorCommand::SwitchToWorld3D
                                               : EditorCommand::SwitchToWorld2D);
                    break;
                case 32: {
                    const WorkspaceMode otherWorkspace =
                        options_.initialWorkspace == WorkspaceMode::World2D
                            ? WorkspaceMode::World3D
                            : WorkspaceMode::World2D;
                    if (workspaceMode_ == otherWorkspace &&
                        !pendingEditorCommand_.has_value() &&
                        queueAutomaticViewportNavigation()) {
                        ++autoAuthoringStage_;
                    }
                    break;
                }
                case 33:
                    if (workspaceMode_ == WorkspaceMode::World2D) {
                        ++autoAuthoringStage_;
                    } else {
                        (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                    }
                    break;
                case 34:
                    if (workspaceMode_ == WorkspaceMode::World2D) {
                        ++autoAuthoringStage_;
                    } else {
                        (void)queueAutoCommand(EditorCommand::SwitchToWorld2D);
                    }
                    break;
                case 35:
                    (void)queueAutoCommand(EditorCommand::AnimationNextFrame);
                    break;
                case 36:
                    (void)queueAutoCommand(EditorCommand::AnimationCycleMode);
                    break;
                case 37:
                    (void)queueAutoCommand(EditorCommand::AnimationUndo);
                    break;
                case 38:
                    (void)queueAutoCommand(EditorCommand::AnimationRedo);
                    break;
                case 39:
                    (void)queueAutoCommand(EditorCommand::AnimationCookPreview);
                    break;
                case 40:
                    if (options_.initialWorkspace == WorkspaceMode::World2D) {
                        ++autoAuthoringStage_;
                    } else {
                        (void)queueAutoCommand(EditorCommand::SwitchToWorld3D);
                    }
                    break;
                case 41:
                    (void)queueAutoCommand(EditorCommand::OpenSelectedProjectAsset);
                    break;
                case 42:
                    if (counters_.tabOwnedDocumentLoads != 0U) {
                        const auto* activeTab = documentTabs_.activeTab();
                        if (activeTab == nullptr) {
                            break;
                        }
                        switch (activeTab->key.kind) {
                        case Tina::Editor::EditorDocumentKind::World3D:
                            pendingDocumentTabActivation_ = 1U;
                            break;
                        case Tina::Editor::EditorDocumentKind::TileMap2D:
                            pendingDocumentTabActivation_ = 2U;
                            break;
                        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
                            pendingDocumentTabActivation_ = 3U;
                            break;
                        case Tina::Editor::EditorDocumentKind::World2D:
                        case Tina::Editor::EditorDocumentKind::AssetInspector:
                        default:
                            break;
                        }
                    }
                    ++autoAuthoringStage_;
                    break;
                case 43:
                    (void)queueAutoCommand(options_.initialWorkspace == WorkspaceMode::World2D
                                               ? EditorCommand::SwitchToWorld2D
                                               : EditorCommand::SwitchToWorld3D);
                    break;
                case 44:
                    if (workspaceMode_ == options_.initialWorkspace &&
                        !pendingEditorCommand_.has_value()) {
                        const auto stableId = automaticHierarchyStableId(true);
                        if (stableId.has_value()) {
                            pendingSelectionStableId_ = *stableId;
                            counters_.automaticFinalSelectionStableId = *stableId;
                            ++autoAuthoringStage_;
                        }
                    }
                    break;
                case 45:
                    // Allow the final workspace/selection mutations to publish
                    // through one complete retained-UI frame before shutdown.
                    ++autoAuthoringStage_;
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
        const bool sourceImportSettled =
            !sourceImportStartPending_ &&
            sourceImportService_.state() ==
                Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
        const bool automaticDemoSettled =
            !options_.autoDemo ||
            autoAuthoringStage_ >= AutomaticAuthoringStageCount;
        counters_.automaticAuthoringStage = autoAuthoringStage_;
        const bool automaticDemoDeadlineReached =
            options_.autoDemo && automaticDemoStartFrame_ != 0U &&
            counters_.frameUpdates >= automaticDemoStartFrame_ &&
            counters_.frameUpdates - automaticDemoStartFrame_ >=
                AutomaticAuthoringMinimumFrameCount - u64{1};
        if (options_.targetFrameCount != 0 &&
            counters_.frameUpdates >= options_.targetFrameCount &&
            sourceImportSettled &&
            (automaticDemoSettled || automaticDemoDeadlineReached)) {
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

        if (!previewCamera2D_.hasValue()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport is missing the independent Camera2D");
        }
        const Tina::Scene::Camera2D* authoredCamera =
            previewWorld_->camera2D(previewCamera2D_);
        if (authoredCamera == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor GPU viewport is missing the Camera2D component");
        }
        Tina::Scene::Camera2D camera = *authoredCamera;
        camera.projection = Tina::Render::FixedWorldHeight2D{
            .heightMeters = viewportWorldHeight(),
        };
        camera.normalizedViewport = *viewportNormalized_;
        camera.pixelSnap = Tina::Render::RenderPixelSnapPolicy::Disabled;
        if (auto status = previewWorld_->setCamera2D(previewCamera2D_, camera); !status) {
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
                previewWorld_->worldTransform(previewCamera2D_);
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
                .halfWidth = viewportWorldWidth() * 0.5F,
                .halfHeight = viewportWorldHeight() * 0.5F,
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
        if (playSessionActive() &&
            observedPlaySessionRevision_ != playSession_->snapshot().revision) {
            observedPlaySessionRevision_ = playSession_->snapshot().revision;
            if (auto status = publishRuntimePreviewStatus(*tree); !status) {
                return status;
            }
        }
        if (pendingViewportSliderValue_.has_value()) {
            const float value = std::exchange(pendingViewportSliderValue_, std::nullopt).value();
            if (auto status = tree->setSliderValue(zoomSlider_, value); !status) {
                return status;
            }
        }
        if (auto status = updateGpuViewport(*tree); !status) {
            return status;
        }
        if (auto status = processViewportNavigation(); !status) {
            return status;
        }
        if (std::exchange(viewportViewModeRefreshPending_, false)) {
            if (auto status = refreshViewportViewModeUi(*tree); !status) {
                return status;
            }
        }
        if (auto status = updateViewportGrid(*tree); !status) {
            return status;
        }
        if (auto status = updateViewportTransformGizmo(*tree); !status) {
            return status;
        }
        if (auto status = updateViewportMarqueeVisual(*tree); !status) {
            return status;
        }
        if (pendingAnimationTimelineRefresh_) {
            pendingAnimationTimelineRefresh_ = false;
            if (auto status = refreshAnimationTimelineUi(*tree); !status) {
                return status;
            }
        }
        if (projectBrowserUiRefreshPending_) {
            projectBrowserUiRefreshPending_ = false;
            if (auto status = tree->setListViewDataSource(
                    projectAssetList_, projectAssetDataSource());
                !status) {
                return status;
            }
            if (auto status = tree->invalidateListViewItems(projectAssetList_); !status) {
                return status;
            }
            const auto selectedIndex = projectAssets_.selectedVisibleIndex();
            if (selectedIndex.has_value()) {
                if (auto status = tree->setListViewSelectedIndex(
                        projectAssetList_, *selectedIndex);
                    !status) {
                    return status;
                }
                observedProjectAssetSelectionIndex_ = *selectedIndex;
            } else {
                if (auto status = tree->clearListViewSelection(projectAssetList_); !status) {
                    return status;
                }
                observedProjectAssetSelectionIndex_.reset();
                assetInspectorActive_ = false;
            }
            synchronizeViewportSelectionFromHierarchy();
            if (auto status = refreshAuthoringUi(*tree); !status) {
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
        if (auto status = processViewportMarquee(*tree); !status) {
            return status;
        }
        if (pendingSelectionStableId_.has_value()) {
            const u32 stableId = *pendingSelectionStableId_;
            pendingSelectionStableId_.reset();
            const auto index = visibleHierarchyIndex(stableId);
            if (!index.has_value()) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::EntityNotFound,
                    "Editor automatic hierarchy target is no longer visible");
            }
            if (auto status = tree->setTreeViewSelectedIndex(hierarchyTree_, *index); !status) {
                return status;
            }
        }
        auto selection = tree->treeViewSelection(hierarchyTree_);
        if (!selection) {
            return Tina::Core::failure(std::move(selection.error()));
        }
        if (selection->key != selectionKey_) {
            selectionKey_ = selection->key;
            assetInspectorActive_ = false;
            if (!preserveViewportSelectionOnHierarchyPublish_) {
                synchronizeViewportSelectionFromHierarchy();
            }
            ++counters_.hierarchySelectionChanges;
            if (auto status = refreshAuthoringUi(*tree); !status) {
                return status;
            }
        }
        preserveViewportSelectionOnHierarchyPublish_ = false;
        auto projectSelection = tree->listViewSelection(projectAssetList_);
        if (!projectSelection) {
            return Tina::Core::failure(std::move(projectSelection.error()));
        }
        if (projectSelection->hasValue() &&
            observedProjectAssetSelectionIndex_ != projectSelection->logicalIndex) {
            if (auto status = projectAssets_.selectVisibleIndex(
                    static_cast<Tina::Core::usize>(projectSelection->logicalIndex));
                !status) {
                return status;
            }
            observedProjectAssetSelectionIndex_ = projectSelection->logicalIndex;
            assetInspectorActive_ = true;
            synchronizeViewportSelectionFromHierarchy();
            ++counters_.projectAssetSelectionChanges;
            if (auto status = refreshAuthoringUi(*tree); !status) {
                return status;
            }
        } else if (!projectSelection->hasValue() &&
                   observedProjectAssetSelectionIndex_.has_value()) {
            observedProjectAssetSelectionIndex_.reset();
            assetInspectorActive_ = false;
            synchronizeViewportSelectionFromHierarchy();
            if (auto status = refreshAuthoringUi(*tree); !status) {
                return status;
            }
        }
        if (pendingDocumentTabActivation_.has_value()) {
            const u32 index = *pendingDocumentTabActivation_;
            pendingDocumentTabActivation_.reset();
            if (playSessionActive()) {
                authoringFeedback_ =
                    "Stop the isolated play session before switching documents";
                if (auto status = refreshAuthoringUi(*tree); !status) {
                    return status;
                }
        } else {
            if (auto status = activateDocumentTab(*tree, index); !status) {
                return status;
            }
        }
        }
        if (pendingViewportToolMode_.has_value()) {
            const ViewportToolMode requestedMode = *pendingViewportToolMode_;
            pendingViewportToolMode_.reset();
            if (requestedMode != viewportToolMode_ && viewportGizmo_.captured) {
                if (viewportTransformGizmo_.snapshot().dragging()) {
                    (void)viewportTransformGizmo_.cancelDrag(
                        ViewportPrimaryPointerToken);
                }
                viewportGizmo_.cancelRequested = true;
                if (auto status = processViewportGizmo(*tree); !status) {
                    return status;
                }
            }
            viewportToolMode_ = requestedMode;
            Tina::Editor::EditorTransformGizmoMode gizmoMode =
                Tina::Editor::EditorTransformGizmoMode::Translate;
            bool transformTool = true;
            if (viewportToolMode_ == ViewportToolMode::Rotate) {
                gizmoMode = Tina::Editor::EditorTransformGizmoMode::Rotate;
            } else if (viewportToolMode_ == ViewportToolMode::Scale) {
                gizmoMode = Tina::Editor::EditorTransformGizmoMode::Scale;
            } else if (viewportToolMode_ != ViewportToolMode::Translate) {
                transformTool = false;
            }
            if (transformTool && viewportTransformGizmo_.setMode(gizmoMode) !=
                                     Tina::Editor::EditorTransformGizmoOperation::Success) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidConfiguration,
                    "editor could not change transform gizmo mode");
            }
            if (auto status = refreshViewportToolUi(*tree); !status) {
                return status;
            }
        }
        if (pendingGizmoOrientationToggle_) {
            pendingGizmoOrientationToggle_ = false;
            if (viewportGizmo_.captured) {
                if (viewportTransformGizmo_.snapshot().dragging()) {
                    (void)viewportTransformGizmo_.cancelDrag(
                        ViewportPrimaryPointerToken);
                }
                viewportGizmo_.cancelRequested = true;
                if (auto status = processViewportGizmo(*tree); !status) {
                    return status;
                }
            }
            const auto orientation = viewportTransformGizmo_.snapshot().orientation ==
                                             Tina::Editor::EditorTransformGizmoOrientation::World
                                         ? Tina::Editor::EditorTransformGizmoOrientation::Local
                                         : Tina::Editor::EditorTransformGizmoOrientation::World;
            if (viewportTransformGizmo_.setOrientation(orientation) !=
                Tina::Editor::EditorTransformGizmoOperation::Success) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidConfiguration,
                    "editor could not change transform gizmo orientation");
            }
            if (auto status = refreshViewportToolUi(*tree); !status) {
                return status;
            }
        }
        if (pendingGizmoSnapToggle_) {
            pendingGizmoSnapToggle_ = false;
            if (viewportGizmo_.captured) {
                if (viewportTransformGizmo_.snapshot().dragging()) {
                    (void)viewportTransformGizmo_.cancelDrag(
                        ViewportPrimaryPointerToken);
                }
                viewportGizmo_.cancelRequested = true;
                if (auto status = processViewportGizmo(*tree); !status) {
                    return status;
                }
            }
            auto snap = viewportTransformGizmo_.snap();
            snap.enabled = !snap.enabled;
            if (viewportTransformGizmo_.setSnap(snap) !=
                Tina::Editor::EditorTransformGizmoOperation::Success) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidConfiguration,
                    "editor could not change transform gizmo snapping");
            }
            if (auto status = refreshViewportToolUi(*tree); !status) {
                return status;
            }
        }
        if (pendingMarqueeSelectionMode_.has_value()) {
            marqueeSelectionMode_ = *pendingMarqueeSelectionMode_;
            pendingMarqueeSelectionMode_.reset();
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
            const char* automaticScale =
                workspaceMode_ == WorkspaceMode::World3D ? "1.25" : "1.0";
            if (auto status = tree->setText(inspectorPositionX_, "2.5"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorPositionY_, "-1.25"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorRotationZ_, "30.0"); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorScaleX_, automaticScale); !status) {
                return status;
            }
            if (auto status = tree->setText(inspectorScaleY_, automaticScale); !status) {
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
                if (auto status = tree->setText(inspectorScaleZ_, automaticScale); !status) {
                    return status;
                }
            }
        }
        if (pendingAutoParentStableId_.has_value() &&
            pendingEditorCommand_ == EditorCommand::SceneReparent) {
            const u32 parentStableId = *pendingAutoParentStableId_;
            pendingAutoParentStableId_.reset();
            if (auto status = tree->setText(
                    inspectorParentStableId_, std::to_string(parentStableId));
                !status) {
                return status;
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
    void resetViewportInteractionState() noexcept
    {
        if (viewportTransformGizmo_.snapshot().dragging()) {
            (void)viewportTransformGizmo_.cancelDrag(ViewportPrimaryPointerToken);
        }
        viewportGizmo_ = {};
        viewportNavigationDrag_ = {};
        viewportMarquee_ = {};
        if (viewportSelectedEntityCount_ != 0U) {
            ++viewportSelectionRevision_;
        }
        viewportSelectedEntityCount_ = 0;
        preserveViewportSelectionOnHierarchyPublish_ = false;
        pendingViewportNavigationCount_ = 0;
        viewportNavigationQueueOverflowed_ = false;
        pendingViewportZoomPercent_.reset();
        pendingGizmoOrientationToggle_ = false;
        pendingGizmoSnapToggle_ = false;
        pendingMarqueeSelectionMode_.reset();
    }

    [[nodiscard]] Tina::Core::Status processEditorShortcuts(
        const Tina::FrameActionSnapshot& actions)
    {
        const bool control = actions.isActive(EditorShortcutActions::Control);
        const bool shift = actions.isActive(EditorShortcutActions::Shift);
        const auto queue = [this](EditorCommand command) noexcept {
            if (!pendingEditorCommand_.has_value()) {
                pendingEditorCommand_ = command;
            }
        };

        // Chords are intentionally limited to control/function keys so text
        // entry in Inspector fields never changes the active viewport tool.
        if (!playSessionActive() && control &&
            editorShortcutStarted(actions, EditorShortcutActions::Save)) {
            queue(shift ? EditorCommand::SaveAs : EditorCommand::Save);
        } else if (!playSessionActive() && control &&
                   editorShortcutStarted(actions, EditorShortcutActions::Undo)) {
            queue(EditorCommand::Undo);
        } else if (!playSessionActive() && control &&
                   editorShortcutStarted(actions, EditorShortcutActions::Redo)) {
            queue(EditorCommand::Redo);
        } else if (!playSessionActive() && control &&
                   editorShortcutStarted(actions, EditorShortcutActions::Duplicate) &&
                   sceneDocumentActive() &&
                   stableEntityIdForHierarchyItem(selectionKey_) != 0U) {
            queue(EditorCommand::SceneDuplicate);
        } else if (!playSessionActive() &&
                   editorShortcutStarted(actions,
                                         EditorShortcutActions::DeleteSelection) &&
                   sceneDocumentActive() &&
                   stableEntityIdForHierarchyItem(selectionKey_) != 0U) {
            queue(EditorCommand::SceneDelete);
        } else if (!playSessionActive() && control &&
                   editorShortcutStarted(actions, EditorShortcutActions::Switch2D)) {
            queue(EditorCommand::SwitchToWorld2D);
        } else if (!playSessionActive() && control &&
                   editorShortcutStarted(actions, EditorShortcutActions::Switch3D)) {
            queue(EditorCommand::SwitchToWorld3D);
        } else if (control &&
                   editorShortcutStarted(actions, EditorShortcutActions::FrameAll)) {
            queue(EditorCommand::ViewportResetView);
        } else if (control &&
                   editorShortcutStarted(actions, EditorShortcutActions::FocusSelection) &&
                   stableEntityIdForHierarchyItem(selectionKey_) != 0U) {
            queue(EditorCommand::SceneFocus);
        } else if (editorShortcutStarted(actions, EditorShortcutActions::Play)) {
            queue(EditorCommand::PlayStartOrResume);
        } else if (editorShortcutStarted(actions, EditorShortcutActions::Step) &&
                   playSessionActive()) {
            queue(EditorCommand::PlayStep);
        } else if (editorShortcutStarted(actions, EditorShortcutActions::Stop) &&
                   playSessionActive()) {
            queue(EditorCommand::PlayStop);
        } else if (editorShortcutStarted(actions, EditorShortcutActions::Escape)) {
            if (pendingDirtyCloseKey_.has_value()) {
                queue(EditorCommand::DirtyCloseCancel);
            } else if (playSessionActive()) {
                queue(EditorCommand::PlayStop);
            } else {
                if (viewportGizmo_.captured) {
                    if (viewportTransformGizmo_.snapshot().dragging()) {
                        (void)viewportTransformGizmo_.cancelDrag(
                            ViewportPrimaryPointerToken);
                    }
                    viewportGizmo_.cancelRequested = true;
                }
                if (viewportMarquee_.captured) {
                    viewportMarquee_.cancelRequested = true;
                }
                viewportNavigationDrag_ = {};
            }
        }
        return Tina::Core::success();
    }

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
        const auto* tab = documentTabs_.activeTab();
        return workspaceMode_ == WorkspaceMode::World2D && tab != nullptr &&
               tab->key.kind == Tina::Editor::EditorDocumentKind::TileMap2D;
    }

    [[nodiscard]] bool playSessionActive() const noexcept
    {
        return playSession_.has_value() && playSession_->active();
    }

    [[nodiscard]] bool authoringEnabled() const noexcept
    {
        return !playSessionActive();
    }

    [[nodiscard]] bool playStartReady() const noexcept
    {
        using ImportState =
            Tina::EditorApp::Detail::EditorSourceImportServiceState;
        return sceneDocumentActive() && counters_.runtimePreviewValid &&
               !sourceImportStartPending_ &&
               sourceImportService_.state() == ImportState::Idle &&
               !sourceImportCatalogCommitted_ &&
               !pendingProjectSwitch_.has_value() &&
               !catalogRefreshPending_ &&
               !projectBrowserUiRefreshPending_ &&
               !previewAssetBindingsRefreshPending_;
    }

    [[nodiscard]] bool sceneDocumentActive() const noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        return tab != nullptr &&
               (tab->key.kind == Tina::Editor::EditorDocumentKind::World2D ||
                tab->key.kind == Tina::Editor::EditorDocumentKind::World3D);
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
        animationSelectedFrameIndex_ = frameIndex;
        counters_.animationPreviewFrameIndex = frameIndex;
        const Tina::Asset::AssetHandle sprite = loadedAsset(
            frame->spriteId, Tina::AssetFormat::AssetKind::Sprite);
        if (!sprite || !containsHandle(boundSpriteAssets_, sprite)) {
            animationPreviewAvailable_ = false;
            return Tina::Core::success();
        }
        animationPreviewAvailable_ = true;
        if (workspaceMode_ != WorkspaceMode::World2D || !previewWorld_.has_value()) {
            return Tina::Core::success();
        }
        const Tina::Scene::World2DEntityBinding* target = findPreviewBinding(selectionKey_);
        if (target == nullptr || previewWorld_->spriteRenderer2D(target->entity) == nullptr) {
            const auto firstSprite = std::find_if(
                previewBindings_.begin(), previewBindings_.end(),
                [this](const Tina::Scene::World2DEntityBinding& binding) {
                    return previewWorld_->spriteRenderer2D(binding.entity) != nullptr;
                });
            target = firstSprite != previewBindings_.end() ? &*firstSprite : nullptr;
        }
        if (target == nullptr) {
            animationPreviewAvailable_ = false;
            return Tina::Core::success();
        }
        const Tina::Scene::SpriteRenderer2D* current =
            previewWorld_->spriteRenderer2D(target->entity);
        Tina::Scene::SpriteRenderer2D updated = *current;
        updated.sprite = sprite;
        return previewWorld_->setSpriteRenderer2D(target->entity, updated);
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
        const bool editable = workspaceMode_ == WorkspaceMode::World2D &&
                              authoringEnabled();
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
        if (auto status = registerListener(
                3, UI::UIRoutedPointerEventKind::PointerCancel,
                UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                    handleViewportPointerCancel(event);
                }});
            !status) {
            return status;
        }
        return registerListener(
            4, UI::UIRoutedPointerEventKind::Wheel,
            UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                handleViewportPointerWheel(event);
            }});
    }

    [[nodiscard]] static Tina::Editor::EditorViewportNavigationConfig
    viewportNavigationConfig() noexcept
    {
        Tina::Editor::EditorViewportNavigationConfig config{};
        config.twoDWorldUnitsPerPixelAtZoomOne = 0.01F;
        config.minimumTwoDZoom = 0.25F;
        config.maximumTwoDZoom = 4.0F;
        config.minimumThreeDDistance = 0.5F;
        config.maximumThreeDDistance = 128.0F;
        return config;
    }

    [[nodiscard]] Tina::Core::Status ensureViewportNavigation()
    {
        if (viewportNavigation_.has_value()) {
            return Tina::Core::success();
        }
        auto navigation = Tina::Editor::EditorViewportNavigation::Create(
            viewportNavigationConfig(),
            viewport2DSessionState_, viewport3DSessionState_);
        if (!navigation) {
            return Tina::Core::failure(std::move(navigation.error()));
        }
        viewportNavigation_.emplace(std::move(*navigation));
        viewport2DSessionState_ = viewportNavigation_->twoD();
        viewport3DSessionState_ = viewportNavigation_->threeD();
        if (!viewport2DNavigationInitialized_) {
            viewport2DFrameAllState_ = viewport2DSessionState_;
        }
        if (!viewport3DNavigationInitialized_) {
            viewport3DFrameAllState_ = viewport3DSessionState_;
        }
        return Tina::Core::success();
    }

    [[nodiscard]] static Tina::Scene::Quaternion viewportOrbitRotation(
        float yawRadians, float pitchRadians) noexcept
    {
        const float halfYaw = yawRadians * 0.5F;
        const float halfPitch = -pitchRadians * 0.5F;
        const Tina::Scene::Quaternion yaw{
            .y = std::sin(halfYaw),
            .w = std::cos(halfYaw),
        };
        const Tina::Scene::Quaternion pitch{
            .x = std::sin(halfPitch),
            .w = std::cos(halfPitch),
        };
        return Tina::Scene::normalized(Tina::Scene::quaternionMultiply(yaw, pitch));
    }

    void syncViewportZoomFromNavigation() noexcept
    {
        if (!viewportNavigation_.has_value()) {
            return;
        }
        if (workspaceMode_ == WorkspaceMode::World2D) {
            viewportZoomPercent_ =
                std::clamp(viewportNavigation_->twoD().zoom * 100.0F, 25.0F, 400.0F);
        } else {
            const float referenceDistance =
                (std::max)(viewport3DFrameAllState_.distance, 0.5F);
            viewportZoomPercent_ = std::clamp(
                referenceDistance * 100.0F /
                    viewportNavigation_->threeD().distance,
                25.0F, 400.0F);
        }
        counters_.viewportZoomPercent = viewportZoomPercent_;
    }

    void persistViewportNavigationState() noexcept
    {
        if (!viewportNavigation_.has_value()) {
            return;
        }
        if (workspaceMode_ == WorkspaceMode::World2D) {
            viewport2DSessionState_ = viewportNavigation_->twoD();
        } else {
            viewport3DSessionState_ = viewportNavigation_->threeD();
        }
    }

    [[nodiscard]] Tina::Core::Status applyViewportNavigationToPreview()
    {
        if (!previewWorld_.has_value()) {
            return Tina::Core::success();
        }
        if (auto status = ensureViewportNavigation(); !status) {
            return status;
        }
        Tina::Scene::EntityId camera{};
        Tina::Scene::LocalTransform transform{};
        if (workspaceMode_ == WorkspaceMode::World2D) {
            camera = previewCamera2D_;
            if (!camera.hasValue()) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "editor viewport navigation has no independent Camera2D");
            }
            const Tina::Scene::LocalTransform* current =
                previewWorld_->localTransform(camera);
            if (current == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "editor viewport navigation cannot resolve Camera2D transform");
            }
            transform = *current;
            transform.position.x = viewportNavigation_->twoD().center.x;
            transform.position.y = viewportNavigation_->twoD().center.y;
        } else {
            camera = previewCamera3D_;
            if (!camera.hasValue()) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "editor viewport navigation has no independent Camera3D");
            }
            const Tina::Scene::LocalTransform* current =
                previewWorld_->localTransform(camera);
            if (current == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "editor viewport navigation cannot resolve Camera3D transform");
            }
            transform = *current;
            const auto& navigation = viewportNavigation_->threeD();
            const float cosinePitch = std::cos(navigation.pitchRadians);
            transform.position = {
                .x = navigation.target.x +
                     std::sin(navigation.yawRadians) * cosinePitch *
                         navigation.distance,
                .y = navigation.target.y +
                     std::sin(navigation.pitchRadians) * navigation.distance,
                .z = navigation.target.z +
                     std::cos(navigation.yawRadians) * cosinePitch *
                         navigation.distance,
            };
            transform.rotation = viewportOrbitRotation(
                navigation.yawRadians, navigation.pitchRadians);
        }
        if (auto status = previewWorld_->setLocalTransform(camera, transform); !status) {
            return status;
        }
        if (auto status = previewWorld_->updateWorldTransforms(); !status) {
            return status;
        }
        syncViewportZoomFromNavigation();
        persistViewportNavigationState();
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status initializeOrApplyViewportNavigation()
    {
        if (auto status = ensureViewportNavigation(); !status) {
            return status;
        }
        if (!previewWorld_.has_value()) {
            return Tina::Core::success();
        }
        auto twoD = viewportNavigation_->twoD();
        auto threeD = viewportNavigation_->threeD();
        bool initialize = false;
        if (workspaceMode_ == WorkspaceMode::World2D &&
            !viewport2DNavigationInitialized_) {
            const Tina::Scene::WorldTransform* camera =
                previewWorld_->worldTransform(previewCamera2D_);
            if (camera == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "editor could not initialize Camera2D navigation");
            }
            twoD.center = {.x = camera->position.x, .y = camera->position.y};
            twoD.zoom = 1.0F;
            viewport2DSessionState_ = twoD;
            viewport2DFrameAllState_ = twoD;
            viewport2DNavigationInitialized_ = true;
            initialize = true;
        } else if (workspaceMode_ == WorkspaceMode::World3D &&
                   !viewport3DNavigationInitialized_) {
            const Tina::Scene::WorldTransform* camera =
                previewWorld_->worldTransform(previewCamera3D_);
            const Tina::Scene::EntityId focusEntity =
                findPreviewEntity(stableEntityIdForHierarchyItem(selectionKey_));
            const Tina::Scene::WorldTransform* focus =
                focusEntity.hasValue() ? previewWorld_->worldTransform(focusEntity)
                                       : nullptr;
            if (camera == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "editor could not initialize Camera3D navigation");
            }
            Tina::Scene::Vec3 direction = Tina::Scene::rotate(
                camera->rotation, {.x = 0.0F, .y = 0.0F, .z = -1.0F});
            float distance = PreviewWorld3DCameraDistance;
            if (focus != nullptr) {
                const Tina::Scene::Vec3 offset = focus->position - camera->position;
                const float lengthSquared = Tina::Scene::dot(offset, offset);
                if (std::isfinite(lengthSquared) && lengthSquared > 0.25F) {
                    distance = std::sqrt(lengthSquared);
                    const float inverseDistance = 1.0F / distance;
                    direction = offset * inverseDistance;
                }
            }
            threeD.target = {
                .x = camera->position.x + direction.x * distance,
                .y = camera->position.y + direction.y * distance,
                .z = camera->position.z + direction.z * distance,
            };
            threeD.yawRadians = std::atan2(-direction.x, -direction.z);
            threeD.pitchRadians = std::asin(
                std::clamp(-direction.y, -1.0F, 1.0F));
            threeD.distance = distance;
            viewport3DSessionState_ = threeD;
            viewport3DFrameAllState_ = threeD;
            viewport3DNavigationInitialized_ = true;
            viewport3DViewPreset_.reset();
            initialize = true;
        }
        if (initialize) {
            if (workspaceMode_ == WorkspaceMode::World2D) {
                if (auto status = viewportNavigation_->set2DView(twoD); !status) {
                    return status;
                }
            } else if (auto status = viewportNavigation_->set3DView(threeD); !status) {
                return status;
            }
        }
        return applyViewportNavigationToPreview();
    }

    [[nodiscard]] Tina::Core::Status focusViewportOnSelection()
    {
        const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableId == 0U || !previewWorld_.has_value()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Select a scene item before focusing the viewport");
        }
        const Tina::Scene::EntityId entity = findPreviewEntity(stableId);
        const Tina::Scene::WorldTransform* transform = entity.hasValue()
            ? previewWorld_->worldTransform(entity)
            : nullptr;
        if (transform == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "The selected scene item is unavailable in the viewport preview");
        }
        if (auto status = ensureViewportNavigation(); !status) {
            return status;
        }
        auto twoD = viewportNavigation_->twoD();
        auto threeD = viewportNavigation_->threeD();
        if (workspaceMode_ == WorkspaceMode::World2D) {
            twoD.center = {
                .x = transform->position.x,
                .y = transform->position.y,
            };
            viewport2DNavigationInitialized_ = true;
        } else {
            threeD.target = {
                .x = transform->position.x,
                .y = transform->position.y,
                .z = transform->position.z,
            };
            viewport3DNavigationInitialized_ = true;
        }
        if (workspaceMode_ == WorkspaceMode::World2D) {
            if (auto status = viewportNavigation_->set2DView(twoD); !status) {
                return status;
            }
        } else if (auto status = viewportNavigation_->set3DView(threeD); !status) {
            return status;
        }
        return applyViewportNavigationToPreview();
    }

    [[nodiscard]] bool queueViewportNavigationInput(
        Tina::Editor::EditorViewportNavigationInput input) noexcept
    {
        using Kind = Tina::Editor::EditorViewportNavigationInputKind;
        if (pendingViewportNavigationCount_ != 0U) {
            auto& previous = pendingViewportNavigationInputs_[
                pendingViewportNavigationCount_ - 1U];
            if (previous.kind == input.kind && input.kind != Kind::Zoom2D) {
                if (input.kind == Kind::Dolly3D) {
                    previous.wheelSteps += input.wheelSteps;
                } else {
                    previous.pixelDelta.x += input.pixelDelta.x;
                    previous.pixelDelta.y += input.pixelDelta.y;
                }
                return std::isfinite(previous.pixelDelta.x) &&
                       std::isfinite(previous.pixelDelta.y) &&
                       std::isfinite(previous.wheelSteps);
            }
        }
        if (pendingViewportNavigationCount_ ==
            pendingViewportNavigationInputs_.size()) {
            viewportNavigationQueueOverflowed_ = true;
            return false;
        }
        pendingViewportNavigationInputs_[pendingViewportNavigationCount_++] = input;
        return true;
    }

    [[nodiscard]] Tina::Core::Status applyViewportZoomPercent(float percent)
    {
        if (auto status = ensureViewportNavigation(); !status) {
            return status;
        }
        percent = std::clamp(percent, 25.0F, 400.0F);
        const auto& config = viewportNavigation_->config();
        if (workspaceMode_ == WorkspaceMode::World2D) {
            const float targetZoom = percent / 100.0F;
            const float steps = std::log(
                                    targetZoom / viewportNavigation_->twoD().zoom) /
                                std::log(config.twoDZoomStepFactor);
            if (auto status = viewportNavigation_->zoom2D(
                    steps,
                    {.x = viewportLogicalRect_.width,
                     .y = viewportLogicalRect_.height},
                    {.x = viewportLogicalRect_.width * 0.5F,
                     .y = viewportLogicalRect_.height * 0.5F});
                !status) {
                return status;
            }
        } else {
            const float targetDistance = std::clamp(
                viewport3DFrameAllState_.distance * 100.0F / percent,
                config.minimumThreeDDistance, config.maximumThreeDDistance);
            const float steps = std::log(
                                    viewportNavigation_->threeD().distance /
                                    targetDistance) /
                                std::log(config.threeDDollyStepFactor);
            if (auto status = viewportNavigation_->dolly3D(steps); !status) {
                return status;
            }
        }
        return applyViewportNavigationToPreview();
    }

    [[nodiscard]] Tina::Core::Status frameViewportContents()
    {
        if (auto status = ensureViewportNavigation(); !status) {
            return status;
        }
        auto twoD = viewportNavigation_->twoD();
        auto threeD = viewportNavigation_->threeD();
        if (workspaceMode_ == WorkspaceMode::World2D) {
            twoD = viewport2DFrameAllState_;
        } else {
            threeD = viewport3DFrameAllState_;
            viewport3DViewPreset_.reset();
        }
        if (workspaceMode_ == WorkspaceMode::World2D) {
            if (auto status = viewportNavigation_->set2DView(twoD); !status) {
                return status;
            }
        } else if (auto status = viewportNavigation_->set3DView(threeD); !status) {
            return status;
        }
        return applyViewportNavigationToPreview();
    }

    [[nodiscard]] Tina::Core::Status processViewportNavigation()
    {
        if (viewportNavigationQueueOverflowed_) {
            viewportNavigationQueueOverflowed_ = false;
            pendingViewportNavigationCount_ = 0;
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                "editor viewport navigation input exceeded its fixed batch capacity");
        }
        if (auto status = ensureViewportNavigation(); !status) {
            return status;
        }
        if (pendingViewportNavigationCount_ != 0U) {
            std::array<Tina::Editor::EditorViewportNavigationInput,
                       Tina::Editor::EditorViewportNavigationLimits::MaximumInputCommandsPerBatch>
                inputs{};
            std::copy_n(pendingViewportNavigationInputs_.begin(),
                        pendingViewportNavigationCount_, inputs.begin());
            const Tina::Core::usize inputCount =
                std::exchange(pendingViewportNavigationCount_, Tina::Core::usize{0});
            const float viewportHeight = viewportLogicalRect_.height;
            if (!std::isfinite(viewportHeight) || viewportHeight <= 0.0F) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidConfiguration,
                    "editor viewport navigation requires a committed viewport height");
            }
            const auto& config = viewportNavigation_->config();
            bool orbitInputObserved = false;
            for (Tina::Core::usize index = 0; index < inputCount; ++index) {
                auto& input = inputs[index];
                if (input.kind ==
                        Tina::Editor::EditorViewportNavigationInputKind::Orbit3D &&
                    (input.pixelDelta.x != 0.0F || input.pixelDelta.y != 0.0F)) {
                    orbitInputObserved = true;
                }
                if (input.kind ==
                    Tina::Editor::EditorViewportNavigationInputKind::Pan2D) {
                    const float desiredWorldUnitsPerPixel =
                        viewportWorldHeight() / viewportHeight;
                    const float moduleWorldUnitsPerPixel =
                        config.twoDWorldUnitsPerPixelAtZoomOne /
                        viewportNavigation_->twoD().zoom;
                    const float scale = desiredWorldUnitsPerPixel /
                                        moduleWorldUnitsPerPixel;
                    input.pixelDelta.x *= scale;
                    input.pixelDelta.y *= scale;
                } else if (input.kind ==
                           Tina::Editor::EditorViewportNavigationInputKind::Pan3D) {
                    const float desiredWorldUnitsPerPixel =
                        2.0F * viewportNavigation_->threeD().distance *
                        std::tan(55.0F * DegreesToRadians * 0.5F) /
                        viewportHeight;
                    const float moduleWorldUnitsPerPixel =
                        viewportNavigation_->threeD().distance *
                        config.threeDPanWorldUnitsPerPixelAtUnitDistance;
                    const float scale = desiredWorldUnitsPerPixel /
                                        moduleWorldUnitsPerPixel;
                    input.pixelDelta.x *= scale;
                    input.pixelDelta.y *= scale;
                }
            }
            if (auto status = viewportNavigation_->apply(
                    std::span{inputs.data(), inputCount});
                !status) {
                return status;
            }
            ++counters_.viewportNavigationBatches;
            for (Tina::Core::usize index = 0; index < inputCount; ++index) {
                switch (inputs[index].kind) {
                case Tina::Editor::EditorViewportNavigationInputKind::Pan2D:
                    ++counters_.viewportPan2DInputs;
                    break;
                case Tina::Editor::EditorViewportNavigationInputKind::Zoom2D:
                    ++counters_.viewportZoom2DInputs;
                    break;
                case Tina::Editor::EditorViewportNavigationInputKind::Orbit3D:
                    ++counters_.viewportOrbit3DInputs;
                    break;
                case Tina::Editor::EditorViewportNavigationInputKind::Pan3D:
                    ++counters_.viewportPan3DInputs;
                    break;
                case Tina::Editor::EditorViewportNavigationInputKind::Dolly3D:
                    ++counters_.viewportDolly3DInputs;
                    break;
                }
            }
            if (orbitInputObserved) {
                viewport3DViewPreset_.reset();
                viewportViewModeRefreshPending_ = true;
            }
        }
        if (pendingViewportZoomPercent_.has_value()) {
            const float percent = *pendingViewportZoomPercent_;
            pendingViewportZoomPercent_.reset();
            if (auto status = applyViewportZoomPercent(percent); !status) {
                return status;
            }
        } else if (auto status = applyViewportNavigationToPreview(); !status) {
            return status;
        }
        pendingViewportSliderValue_ = viewportZoomPercent_;
        return Tina::Core::success();
    }

    [[nodiscard]] bool queueAutomaticViewportNavigation() noexcept
    {
        if (pendingViewportNavigationCount_ != 0U ||
            viewportNavigationQueueOverflowed_ || viewportGizmo_.captured ||
            viewportNavigationDrag_.captured || viewportMarquee_.captured ||
            !std::isfinite(viewportLogicalRect_.width) ||
            !std::isfinite(viewportLogicalRect_.height) ||
            viewportLogicalRect_.width <= 0.0F ||
            viewportLogicalRect_.height <= 0.0F) {
            return false;
        }
        using Kind = Tina::Editor::EditorViewportNavigationInputKind;
        if (workspaceMode_ == WorkspaceMode::World2D) {
            const bool panQueued = queueViewportNavigationInput({
                .kind = Kind::Pan2D,
                .pixelDelta = {.x = 24.0F, .y = -12.0F},
            });
            const bool zoomQueued = queueViewportNavigationInput({
                .kind = Kind::Zoom2D,
                .wheelSteps = 1.0F,
                .viewportSizePixels = {
                    .x = viewportLogicalRect_.width,
                    .y = viewportLogicalRect_.height,
                },
                .anchorPixels = {
                    .x = viewportLogicalRect_.width * 0.65F,
                    .y = viewportLogicalRect_.height * 0.4F,
                },
            });
            return panQueued && zoomQueued;
        }
        return queueViewportNavigationInput({
                   .kind = Kind::Orbit3D,
                   .pixelDelta = {.x = 18.0F, .y = -8.0F},
               }) &&
               queueViewportNavigationInput({
                   .kind = Kind::Pan3D,
                   .pixelDelta = {.x = -10.0F, .y = 6.0F},
               }) &&
               queueViewportNavigationInput({
                   .kind = Kind::Dolly3D,
                   .wheelSteps = 1.0F,
               });
    }

    [[nodiscard]] bool prepareAutomaticViewportGizmo(
        Tina::Editor::EditorTransformGizmoMode mode) noexcept
    {
        using Handle = Tina::Editor::EditorTransformGizmoHandle;
        using Mode = Tina::Editor::EditorTransformGizmoMode;
        using Orientation = Tina::Editor::EditorTransformGizmoOrientation;
        const auto& snapshot = viewportTransformGizmo_.snapshot();
        if (!snapshot.framePublished || snapshot.dragging()) {
            return false;
        }
        ViewportToolMode toolMode = ViewportToolMode::Translate;
        if (mode == Mode::Rotate) {
            toolMode = ViewportToolMode::Rotate;
        } else if (mode == Mode::Scale) {
            toolMode = ViewportToolMode::Scale;
        }
        if (snapshot.mode != mode || viewportToolMode_ != toolMode) {
            pendingViewportToolMode_ = toolMode;
            return false;
        }
        if (snapshot.orientation != Orientation::World) {
            pendingGizmoOrientationToggle_ = true;
            return false;
        }
        if (viewportTransformGizmo_.snap().enabled) {
            pendingGizmoSnapToggle_ = true;
            return false;
        }
        Handle desired = workspaceMode_ == WorkspaceMode::World2D
                             ? Handle::PlaneXY
                             : Handle::PlaneXZ;
        if (mode == Mode::Rotate) {
            desired = workspaceMode_ == WorkspaceMode::World2D
                          ? Handle::AxisZ
                          : Handle::AxisY;
        } else if (mode == Mode::Scale) {
            desired = Handle::Uniform;
        }
        const auto geometry = std::find_if(
            snapshot.handles().begin(), snapshot.handles().end(),
            [desired](const auto& candidate) {
                return candidate.handle == desired && candidate.pointCount != 0U;
            });
        if (geometry == snapshot.handles().end()) {
            return false;
        }
        autoGizmoCenter_ = {};
        if (geometry->shape ==
            Tina::Editor::EditorTransformGizmoHandleShape::Ring) {
            autoGizmoCenter_ = {
                .x = geometry->points[0].x,
                .y = geometry->points[0].y,
            };
            autoGizmoStart_ = {
                .x = autoGizmoCenter_.x + geometry->radiusPixels,
                .y = autoGizmoCenter_.y,
            };
        } else {
            for (u32 index = 0; index < geometry->pointCount; ++index) {
                autoGizmoCenter_.x += geometry->points[index].x;
                autoGizmoCenter_.y += geometry->points[index].y;
            }
            autoGizmoCenter_.x /= static_cast<float>(geometry->pointCount);
            autoGizmoCenter_.y /= static_cast<float>(geometry->pointCount);
            autoGizmoStart_ = autoGizmoCenter_;
        }
        return beginViewportGizmo(Tina::Platform::PrimaryPointerId,
                                  autoGizmoStart_);
    }

    [[nodiscard]] std::optional<UI::UILogicalPoint>
    automaticViewportGizmoPoint(float fraction) const noexcept
    {
        if (!std::isfinite(fraction) || !viewportGizmo_.captured) {
            return std::nullopt;
        }
        const auto mode = viewportTransformGizmo_.snapshot().mode;
        if (mode == Tina::Editor::EditorTransformGizmoMode::Rotate) {
            const float radius = std::hypot(
                autoGizmoStart_.x - autoGizmoCenter_.x,
                autoGizmoStart_.y - autoGizmoCenter_.y);
            const float radians =
                -std::numbers::pi_v<float> * 0.5F * fraction;
            return UI::UILogicalPoint{
                .x = autoGizmoCenter_.x + std::cos(radians) * radius,
                .y = autoGizmoCenter_.y + std::sin(radians) * radius,
            };
        }
        if (mode == Tina::Editor::EditorTransformGizmoMode::Scale) {
            constexpr float AutomaticScaleDragPixels = 32.0F;
            return UI::UILogicalPoint{
                .x = autoGizmoStart_.x + AutomaticScaleDragPixels * fraction,
                .y = autoGizmoStart_.y - AutomaticScaleDragPixels * fraction,
            };
        }
        if (!previewWorld_.has_value()) {
            return std::nullopt;
        }
        const Tina::Scene::EntityId entity =
            findPreviewEntity(viewportGizmo_.stableEntityId);
        const Tina::Scene::WorldTransform* transform =
            entity.hasValue() ? previewWorld_->worldTransform(entity) : nullptr;
        if (transform == nullptr) {
            return std::nullopt;
        }
        const Tina::Scene::Vec3 originWorld =
            viewportGizmo_.selectionCount > 1U ? viewportGizmo_.pivot
                                               : transform->position;
        const ViewportProjectedPoint origin =
            projectViewportWorldPoint(originWorld);
        const ViewportProjectedPoint xAxis = projectViewportWorldPoint(
            originWorld + Tina::Scene::Vec3{1.0F, 0.0F, 0.0F});
        const Tina::Scene::Vec3 secondDirection =
            workspaceMode_ == WorkspaceMode::World2D
                ? Tina::Scene::Vec3{0.0F, 1.0F, 0.0F}
                : Tina::Scene::Vec3{0.0F, 0.0F, 1.0F};
        const ViewportProjectedPoint secondAxis =
            projectViewportWorldPoint(originWorld + secondDirection);
        if (!origin.projectable || !xAxis.projectable ||
            !secondAxis.projectable) {
            return std::nullopt;
        }
        const float secondAmount =
            workspaceMode_ == WorkspaceMode::World2D ? -1.0F : 1.0F;
        return UI::UILogicalPoint{
            .x = autoGizmoStart_.x +
                 ((xAxis.screen.x - origin.screen.x) * 2.0F +
                  (secondAxis.screen.x - origin.screen.x) * secondAmount) *
                     fraction,
            .y = autoGizmoStart_.y +
                 ((xAxis.screen.y - origin.screen.y) * 2.0F +
                  (secondAxis.screen.y - origin.screen.y) * secondAmount) *
                     fraction,
        };
    }

    [[nodiscard]] bool prepareAutomaticViewportMarquee(
        Tina::Editor::EditorMarqueeSelectionMode mode) noexcept
    {
        if (viewportGizmo_.captured || viewportNavigationDrag_.captured ||
            viewportMarquee_.captured) {
            return false;
        }
        if (viewportToolMode_ != ViewportToolMode::Select) {
            pendingViewportToolMode_ = ViewportToolMode::Select;
            return false;
        }
        if (marqueeSelectionMode_ != mode) {
            pendingMarqueeSelectionMode_ = mode;
            return false;
        }

        std::array<Tina::Editor::EditorMarqueeCandidate,
                   ViewportMarqueeCandidateCapacity>
            candidates{};
        const Tina::Core::usize candidateCount =
            collectViewportMarqueeCandidates(candidates);
        if (candidateCount < 2U) {
            return false;
        }
        const auto currentSelection = std::span{
            viewportSelectedEntityIds_.data(), viewportSelectedEntityCount_};
        constexpr std::array ProbeFactors{
            UI::UILogicalPoint{.x = 0.5F, .y = 0.5F},
            UI::UILogicalPoint{.x = 0.2F, .y = 0.2F},
            UI::UILogicalPoint{.x = 0.8F, .y = 0.2F},
            UI::UILogicalPoint{.x = 0.2F, .y = 0.8F},
            UI::UILogicalPoint{.x = 0.8F, .y = 0.8F},
        };
        constexpr float ProbeHalfExtent = 0.5F;
        for (Tina::Core::usize candidateIndex = 0;
             candidateIndex < candidateCount; ++candidateIndex) {
            const auto& bounds = candidates[candidateIndex].screenBounds;
            const float left = (std::min)(bounds.x0, bounds.x1);
            const float right = (std::max)(bounds.x0, bounds.x1);
            const float top = (std::min)(bounds.y0, bounds.y1);
            const float bottom = (std::max)(bounds.y0, bounds.y1);
            for (const UI::UILogicalPoint factor : ProbeFactors) {
                const UI::UILogicalPoint point{
                    .x = left + (right - left) * factor.x,
                    .y = top + (bottom - top) * factor.y,
                };
                const Tina::Editor::EditorMarqueeScreenRect rect{
                    .x0 = point.x - ProbeHalfExtent,
                    .y0 = point.y - ProbeHalfExtent,
                    .x1 = point.x + ProbeHalfExtent,
                    .y1 = point.y + ProbeHalfExtent,
                };
                auto evaluated = Tina::Editor::EditorMarqueeSelection::Evaluate(
                    rect, std::span{candidates.data(), candidateCount},
                    currentSelection, mode);
                if (!evaluated) {
                    continue;
                }
                bool suitable = false;
                const Tina::Core::usize transformTargetCount =
                    viewportTransformTargetCount(evaluated->selection());
                switch (mode) {
                case Tina::Editor::EditorMarqueeSelectionMode::Replace:
                    suitable = evaluated->changed() &&
                               evaluated->selection().size() == 1U &&
                               transformTargetCount == 1U;
                    break;
                case Tina::Editor::EditorMarqueeSelectionMode::Add:
                    suitable = !evaluated->added().empty() &&
                               evaluated->selection().size() >
                                   currentSelection.size() &&
                               transformTargetCount >= 2U;
                    break;
                case Tina::Editor::EditorMarqueeSelectionMode::Toggle:
                    suitable = !evaluated->removed().empty() &&
                               transformTargetCount == 1U;
                    break;
                }
                if (!suitable ||
                    !beginViewportMarquee(
                        Tina::Platform::PrimaryPointerId,
                        {.x = rect.x0, .y = rect.y0})) {
                    continue;
                }
                if (!updateViewportMarquee(
                        Tina::Platform::PrimaryPointerId,
                        {.x = rect.x1, .y = rect.y1})) {
                    viewportMarquee_ = {};
                    return false;
                }
                viewportMarquee_.commitRequested = true;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool beginViewportGizmo(Tina::Platform::PointerId pointer,
                                          UI::UILogicalPoint position) noexcept
    {
        if (!authoringEnabled() || pointer != Tina::Platform::PrimaryPointerId ||
            (viewportToolMode_ != ViewportToolMode::Translate &&
             viewportToolMode_ != ViewportToolMode::Rotate &&
             viewportToolMode_ != ViewportToolMode::Scale) || viewportGizmo_.captured ||
            viewportNavigationDrag_.captured || viewportMarquee_.captured ||
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
        ViewportTransformTransaction transaction{
            .workspace = workspaceMode_,
            .pointer = pointer,
            .stableEntityId = stableEntityId,
            .baselineRevision = activeDocumentRevision(),
            .selectionRevision = viewportSelectionRevision_,
            .captured = true,
        };
        if (auto status = captureViewportTransformTargets(transaction); !status) {
            return false;
        }
        const auto operation = viewportTransformGizmo_.beginDrag(
            ViewportPrimaryPointerToken, {.x = position.x, .y = position.y});
        if (operation != Tina::Editor::EditorTransformGizmoOperation::Success) {
            return false;
        }
        transaction.processedGizmoRevision = viewportTransformGizmo_.snapshot().revision;
        viewportGizmo_ = std::move(transaction);
        return true;
    }

    [[nodiscard]] bool updateViewportGizmo(Tina::Platform::PointerId pointer,
                                           UI::UILogicalPoint position) noexcept
    {
        if (!viewportGizmo_.captured || pointer != viewportGizmo_.pointer ||
            viewportGizmo_.commitRequested || viewportGizmo_.cancelRequested) {
            return false;
        }
        return viewportTransformGizmo_.updateDrag(
                   ViewportPrimaryPointerToken,
                   {.x = position.x, .y = position.y}) ==
               Tina::Editor::EditorTransformGizmoOperation::Success;
    }

    [[nodiscard]] bool requestViewportGizmoCommit(Tina::Platform::PointerId pointer,
                                                  UI::UILogicalPoint position) noexcept
    {
        if (!viewportGizmo_.captured || pointer != viewportGizmo_.pointer ||
            viewportGizmo_.commitRequested || viewportGizmo_.cancelRequested ||
            viewportTransformGizmo_.endDrag(
                ViewportPrimaryPointerToken,
                {.x = position.x, .y = position.y}) !=
                Tina::Editor::EditorTransformGizmoOperation::Success) {
            return false;
        }
        viewportGizmo_.commitRequested = true;
        return true;
    }

    [[nodiscard]] bool beginViewportNavigation(
        Tina::Platform::PointerId pointer,
        Tina::Platform::PointerButton button) noexcept
    {
        const bool supported = button == Tina::Platform::PointerButton::Middle ||
                               (workspaceMode_ == WorkspaceMode::World3D &&
                                button == Tina::Platform::PointerButton::Secondary);
        if (!supported || viewportNavigationDrag_.captured || viewportGizmo_.captured ||
            viewportMarquee_.captured) {
            return false;
        }
        viewportNavigationDrag_ = {
            .pointer = pointer,
            .button = button,
            .captured = true,
        };
        return true;
    }

    [[nodiscard]] bool updateViewportNavigation(
        const UI::UIPointerInputEvent& input) noexcept
    {
        if (!viewportNavigationDrag_.captured ||
            input.pointer != viewportNavigationDrag_.pointer) {
            return false;
        }
        using Kind = Tina::Editor::EditorViewportNavigationInputKind;
        Kind kind = Kind::Pan2D;
        if (workspaceMode_ == WorkspaceMode::World3D) {
            kind = viewportNavigationDrag_.button ==
                           Tina::Platform::PointerButton::Secondary
                       ? Kind::Orbit3D
                       : Kind::Pan3D;
        }
        return queueViewportNavigationInput({
            .kind = kind,
            .pixelDelta = {.x = input.delta.x, .y = input.delta.y},
        });
    }

    [[nodiscard]] bool beginViewportMarquee(
        Tina::Platform::PointerId pointer,
        UI::UILogicalPoint position) noexcept
    {
        if (viewportToolMode_ != ViewportToolMode::Select ||
            viewportMarquee_.captured || viewportGizmo_.captured ||
            viewportNavigationDrag_.captured) {
            return false;
        }
        viewportMarquee_ = {
            .pointer = pointer,
            .start = position,
            .current = position,
            .mode = marqueeSelectionMode_,
            .captured = true,
        };
        return true;
    }

    [[nodiscard]] bool updateViewportMarquee(
        Tina::Platform::PointerId pointer,
        UI::UILogicalPoint position) noexcept
    {
        if (!viewportMarquee_.captured || pointer != viewportMarquee_.pointer ||
            viewportMarquee_.commitRequested || viewportMarquee_.cancelRequested) {
            return false;
        }
        viewportMarquee_.current = position;
        return true;
    }

    [[nodiscard]] bool queueViewportTileBrush(UI::UILogicalPoint position) noexcept
    {
        if (!authoringEnabled() || !tileMapEditingContext() ||
            (viewportToolMode_ != ViewportToolMode::TilePaint &&
             viewportToolMode_ != ViewportToolMode::TileErase) ||
            pendingTileCellEdit_.has_value() || !previewWorld_.has_value() ||
            tileMapWidthCells_ == 0U || tileMapHeightCells_ == 0U ||
            viewportLogicalRect_.width <= 0.0F || viewportLogicalRect_.height <= 0.0F) {
            return false;
        }
        const Tina::Scene::WorldTransform* cameraTransform =
            previewWorld_->worldTransform(previewCamera2D_);
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
        const float worldHeight = viewportWorldHeight();
        const float worldWidth = viewportWorldWidth();
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
            if (!beginViewportNavigation(input.pointer, input.button)) {
                return;
            }
            event.capturePointer();
            (void)event.claimPointerButton(input.button);
            event.consumeInputTransition();
            event.preventDefaultAction();
            return;
        }
        if (queueViewportTileBrush(input.position)) {
            (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
            event.consumeInputTransition();
            event.preventDefaultAction();
            return;
        }
        const bool began = beginViewportGizmo(input.pointer, input.position);
        const bool marqueeBegan = !began &&
                                  beginViewportMarquee(input.pointer,
                                                       input.position);
        if (!began && !marqueeBegan && !viewportGizmo_.captured &&
            !viewportMarquee_.captured) {
            return;
        }
        if (began || marqueeBegan) {
            event.capturePointer();
        }
        (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void handleViewportPointerMove(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        if (viewportNavigationDrag_.captured) {
            if (!updateViewportNavigation(input)) {
                return;
            }
            (void)event.claimPointerButton(viewportNavigationDrag_.button);
            event.consumeInputTransition();
            event.preventDefaultAction();
            return;
        }
        if (updateViewportGizmo(input.pointer, input.position)) {
            (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
            event.consumeInputTransition();
            event.preventDefaultAction();
            return;
        }
        if (updateViewportMarquee(input.pointer, input.position)) {
            (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
            event.consumeInputTransition();
            event.preventDefaultAction();
            return;
        }
        if (viewportToolMode_ == ViewportToolMode::Translate ||
            viewportToolMode_ == ViewportToolMode::Rotate ||
            viewportToolMode_ == ViewportToolMode::Scale) {
            (void)viewportTransformGizmo_.updateHover(
                {.x = input.position.x, .y = input.position.y});
        }
    }

    void handleViewportPointerUp(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        if (viewportNavigationDrag_.captured &&
            input.pointer == viewportNavigationDrag_.pointer &&
            input.button == viewportNavigationDrag_.button) {
            viewportNavigationDrag_ = {};
            event.releasePointerCapture();
            event.consumeInputTransition();
            event.preventDefaultAction();
            return;
        }
        if (input.button != Tina::Platform::PointerButton::Primary) {
            return;
        }
        bool handled = requestViewportGizmoCommit(input.pointer, input.position);
        if (!handled && updateViewportMarquee(input.pointer, input.position)) {
            viewportMarquee_.commitRequested = true;
            handled = true;
        }
        if (!handled) {
            return;
        }
        event.releasePointerCapture();
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void handleViewportPointerCancel(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        bool handled = false;
        if (viewportNavigationDrag_.captured &&
            input.pointer == viewportNavigationDrag_.pointer) {
            viewportNavigationDrag_ = {};
            handled = true;
        }
        if (viewportGizmo_.captured && input.pointer == viewportGizmo_.pointer) {
            (void)viewportTransformGizmo_.cancelDrag(ViewportPrimaryPointerToken);
            viewportGizmo_.cancelRequested = true;
            handled = true;
        }
        if (viewportMarquee_.captured && input.pointer == viewportMarquee_.pointer) {
            viewportMarquee_.cancelRequested = true;
            handled = true;
        }
        if (!handled) {
            return;
        }
        event.releasePointerCapture();
        event.consumeInputTransition();
        event.preventDefaultAction();
    }

    void handleViewportPointerWheel(UI::UIRoutedPointerEvent& event) noexcept
    {
        const UI::UIPointerInputEvent& input = event.input();
        if (viewportNavigationDrag_.captured || viewportGizmo_.captured ||
            viewportMarquee_.captured || viewportLogicalRect_.width <= 0.0F ||
            viewportLogicalRect_.height <= 0.0F) {
            return;
        }
        const float wheelSteps = input.delta.y != 0.0F
                                     ? input.delta.y
                                     : input.delta.x;
        if (!std::isfinite(wheelSteps) || wheelSteps == 0.0F) {
            return;
        }
        using Kind = Tina::Editor::EditorViewportNavigationInputKind;
        Tina::Editor::EditorViewportNavigationInput navigation{
            .kind = workspaceMode_ == WorkspaceMode::World2D
                        ? Kind::Zoom2D
                        : Kind::Dolly3D,
            .wheelSteps = wheelSteps,
        };
        if (workspaceMode_ == WorkspaceMode::World2D) {
            navigation.viewportSizePixels = {
                .x = viewportLogicalRect_.width,
                .y = viewportLogicalRect_.height,
            };
            navigation.anchorPixels = {
                .x = input.position.x - viewportLogicalRect_.x,
                .y = input.position.y - viewportLogicalRect_.y,
            };
        }
        if (!queueViewportNavigationInput(navigation)) {
            return;
        }
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

    [[nodiscard]] u32 stableIdForPreviewEntity(
        Tina::Scene::EntityId entity) const noexcept
    {
        if (!entity.hasValue()) {
            return 0U;
        }
        if (workspaceMode_ == WorkspaceMode::World3D) {
            const auto binding = std::find_if(
                preview3DBindings_.begin(), preview3DBindings_.end(),
                [entity](const World3DPreviewBinding& candidate) {
                    return candidate.entity == entity;
                });
            return binding == preview3DBindings_.end() ? 0U
                                                       : binding->stableNodeId;
        }
        const auto binding = std::find_if(
            previewBindings_.begin(), previewBindings_.end(),
            [entity](const Tina::Scene::World2DEntityBinding& candidate) {
                return candidate.entity == entity;
            });
        return binding == previewBindings_.end() ? 0U
                                                 : binding->stableEntityId;
    }

    [[nodiscard]] bool viewportSelectionContains(u64 stableId) const noexcept
    {
        if (stableId == 0U) {
            return false;
        }
        return std::find(viewportSelectedEntityIds_.begin(),
                         viewportSelectedEntityIds_.begin() +
                             static_cast<std::ptrdiff_t>(
                                 viewportSelectedEntityCount_),
                         stableId) !=
               viewportSelectedEntityIds_.begin() +
                   static_cast<std::ptrdiff_t>(viewportSelectedEntityCount_);
    }

    [[nodiscard]] bool previewEntityHasAncestorInSelection(
        Tina::Scene::EntityId entity,
        std::span<const u64> selection) const noexcept
    {
        if (!previewWorld_.has_value()) {
            return false;
        }
        Tina::Scene::EntityId parent = previewWorld_->parent(entity);
        for (Tina::Core::usize depth = 0;
             depth < ViewportTransformTargetCapacity && parent.hasValue();
             ++depth) {
            const u64 parentStableId = stableIdForPreviewEntity(parent);
            if (parentStableId != 0U &&
                std::find(selection.begin(), selection.end(), parentStableId) !=
                    selection.end()) {
                return true;
            }
            const Tina::Scene::EntityId next = previewWorld_->parent(parent);
            if (next == parent) {
                break;
            }
            parent = next;
        }
        return false;
    }

    [[nodiscard]] bool previewEntityHasSelectedAncestor(
        Tina::Scene::EntityId entity) const noexcept
    {
        return previewEntityHasAncestorInSelection(
            entity,
            std::span<const u64>{viewportSelectedEntityIds_.data(),
                                 viewportSelectedEntityCount_});
    }

    [[nodiscard]] Tina::Core::usize viewportTransformTargetCount(
        std::span<const u64> selection) const noexcept
    {
        if (!previewWorld_.has_value()) {
            return 0U;
        }
        Tina::Core::usize count = 0;
        for (const u64 stableId : selection) {
            if (stableId == 0U ||
                stableId > (std::numeric_limits<u32>::max)()) {
                continue;
            }
            const Tina::Scene::EntityId entity =
                findPreviewEntity(static_cast<u32>(stableId));
            if (!entity.hasValue() ||
                previewEntityHasAncestorInSelection(entity, selection)) {
                continue;
            }
            const Tina::Scene::LocalTransform* local =
                previewWorld_->localTransform(entity);
            const Tina::Scene::WorldTransform* world =
                previewWorld_->worldTransform(entity);
            if (local != nullptr && world != nullptr &&
                Tina::Scene::isValid(*local) && Tina::Scene::isValid(*world)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] Tina::Scene::Vec3 viewportSelectionPivot() const noexcept
    {
        if (!previewWorld_.has_value()) {
            return {};
        }
        Tina::Scene::Vec3 sum{};
        Tina::Core::usize count = 0;
        const auto append = [&](u64 stableId) {
            const Tina::Scene::EntityId entity = findPreviewEntity(
                static_cast<u32>(stableId));
            if (!entity.hasValue() || previewEntityHasSelectedAncestor(entity)) {
                return;
            }
            const Tina::Scene::WorldTransform* transform =
                previewWorld_->worldTransform(entity);
            if (transform == nullptr || !Tina::Scene::isFinite(transform->position)) {
                return;
            }
            sum = sum + transform->position;
            ++count;
        };
        for (Tina::Core::usize index = 0;
             index < viewportSelectedEntityCount_; ++index) {
            append(viewportSelectedEntityIds_[index]);
        }
        if (count == 0U) {
            const u32 primary = stableEntityIdForHierarchyItem(selectionKey_);
            append(primary);
        }
        if (count == 0U) {
            return {};
        }
        return sum * (1.0F / static_cast<float>(count));
    }

    [[nodiscard]] Tina::Core::Status captureViewportTransformTargets(
        ViewportTransformTransaction& transaction)
    {
        if (!previewWorld_.has_value()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport transform requires a live preview world");
        }
        const u32 primaryStableId = stableEntityIdForHierarchyItem(selectionKey_);
        if (primaryStableId == 0U) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Viewport transform requires a scene selection");
        }

        std::array<u64, ViewportTransformTargetCapacity> orderedIds{};
        Tina::Core::usize orderedCount = 0;
        const auto appendId = [&](u64 stableId) {
            if (stableId == 0U || orderedCount == orderedIds.size() ||
                std::find(orderedIds.begin(), orderedIds.begin() +
                              static_cast<std::ptrdiff_t>(orderedCount),
                          stableId) !=
                    orderedIds.begin() +
                        static_cast<std::ptrdiff_t>(orderedCount)) {
                return;
            }
            orderedIds[orderedCount++] = stableId;
        };
        appendId(primaryStableId);
        for (Tina::Core::usize index = 0;
             index < viewportSelectedEntityCount_; ++index) {
            appendId(viewportSelectedEntityIds_[index]);
        }

        transaction.targetCount = 0;
        transaction.selectionCount = viewportSelectedEntityCount_ != 0U
                                         ? viewportSelectedEntityCount_
                                         : Tina::Core::usize{1};
        Tina::Scene::Vec3 pivotSum{};
        for (Tina::Core::usize index = 0; index < orderedCount; ++index) {
            const u32 stableId = static_cast<u32>(orderedIds[index]);
            const Tina::Scene::EntityId entity = findPreviewEntity(stableId);
            if (!entity.hasValue() || previewEntityHasSelectedAncestor(entity)) {
                continue;
            }
            const Tina::Scene::LocalTransform* local =
                previewWorld_->localTransform(entity);
            const Tina::Scene::WorldTransform* world =
                previewWorld_->worldTransform(entity);
            if (local == nullptr || world == nullptr ||
                !Tina::Scene::isValid(*local) ||
                !Tina::Scene::isValid(*world)) {
                continue;
            }
            if (transaction.targetCount == transaction.targets.size()) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                    "Viewport transform selection exceeds its fixed capacity");
            }
            auto& target = transaction.targets[transaction.targetCount++];
            target = {
                .stableEntityId = stableId,
                .entity = entity,
                .baselineLocal = *local,
                .baselineWorld = *world,
                .previewLocal = *local,
            };
            pivotSum = pivotSum + world->position;
        }
        if (transaction.targetCount == 0U) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Viewport transform selection has no transformable scene item");
        }
        transaction.pivot = pivotSum *
                            (1.0F / static_cast<float>(transaction.targetCount));
        transaction.stableEntityId = primaryStableId;
        transaction.selectionRevision = viewportSelectionRevision_;
        transaction.baselineTransform = transaction.targets[0].baselineLocal;
        transaction.previewTransform = transaction.targets[0].previewLocal;
        return Tina::Core::success();
    }

    [[nodiscard]] static Tina::Core::Result<Tina::Scene::WorldTransform>
    applyViewportWorldTransformDelta(
        const Tina::Scene::WorldTransform& baseline,
        Tina::Scene::Vec3 pivot,
        const Tina::Editor::EditorTransformGizmoDelta& delta,
        Tina::Scene::Quaternion localBasis) noexcept
    {
        const Tina::Scene::Vec3 translation{
            delta.translation.x, delta.translation.y, delta.translation.z};
        const Tina::Scene::Vec3 rotationAxis{
            delta.rotationAxis.x, delta.rotationAxis.y, delta.rotationAxis.z};
        const Tina::Scene::Vec3 scaleFactors{
            delta.scaleFactors.x, delta.scaleFactors.y, delta.scaleFactors.z};
        if (!Tina::Scene::isValid(baseline) ||
            !Tina::Scene::isFinite(pivot) ||
            !Tina::Scene::isFinite(translation) ||
            !Tina::Scene::isFinite(rotationAxis) ||
            !std::isfinite(delta.rotationDegrees) ||
            !Tina::Scene::isFinite(scaleFactors) ||
            scaleFactors.x <= 0.0F || scaleFactors.y <= 0.0F ||
            scaleFactors.z <= 0.0F) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport group transform contains a non-finite value");
        }
        Tina::Scene::WorldTransform result = baseline;
        Tina::Scene::Vec3 relative = result.position - pivot;
        if (std::abs(delta.rotationDegrees) > 1.0e-6F) {
            const float axisLengthSquared =
                Tina::Scene::dot(rotationAxis, rotationAxis);
            if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= 1.0e-12F) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Viewport group rotation has an invalid axis");
            }
            const Tina::Scene::Vec3 axis =
                rotationAxis * (1.0F / std::sqrt(axisLengthSquared));
            const float halfRadians =
                delta.rotationDegrees * DegreesToRadians * 0.5F;
            const float sine = std::sin(halfRadians);
            const Tina::Scene::Quaternion rotationDelta{
                .x = axis.x * sine,
                .y = axis.y * sine,
                .z = axis.z * sine,
                .w = std::cos(halfRadians),
            };
            relative = Tina::Scene::rotate(rotationDelta, relative);
            result.rotation = Tina::Scene::normalized(
                Tina::Scene::quaternionMultiply(rotationDelta,
                                                 result.rotation));
        }
        if (scaleFactors != Tina::Scene::Vec3{1.0F, 1.0F, 1.0F}) {
            if (delta.orientation ==
                Tina::Editor::EditorTransformGizmoOrientation::Local) {
                const Tina::Scene::Quaternion basis =
                    Tina::Scene::normalized(localBasis);
                relative = Tina::Scene::rotate(
                    basis,
                    Tina::Scene::rotate(Tina::Scene::quaternionConjugate(basis),
                                        relative) * scaleFactors);
            } else {
                relative = relative * scaleFactors;
            }
            result.scale = result.scale * scaleFactors;
        }
        result.position = pivot + relative + translation;
        if (!Tina::Scene::isValid(result)) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport group transform produced an invalid world transform");
        }
        return result;
    }

    [[nodiscard]] static bool viewportTransformNearlyEqual(float left,
                                                           float right) noexcept
    {
        const float scale = (std::max)({1.0F, std::abs(left), std::abs(right)});
        return std::abs(left - right) <= 2.0e-4F * scale;
    }

    [[nodiscard]] static bool viewportWorldTransformsEquivalent(
        Tina::Scene::WorldTransform left,
        Tina::Scene::WorldTransform right) noexcept
    {
        const bool sameRotation =
            viewportTransformNearlyEqual(left.rotation.x, right.rotation.x) &&
            viewportTransformNearlyEqual(left.rotation.y, right.rotation.y) &&
            viewportTransformNearlyEqual(left.rotation.z, right.rotation.z) &&
            viewportTransformNearlyEqual(left.rotation.w, right.rotation.w);
        const bool oppositeRotation =
            viewportTransformNearlyEqual(left.rotation.x, -right.rotation.x) &&
            viewportTransformNearlyEqual(left.rotation.y, -right.rotation.y) &&
            viewportTransformNearlyEqual(left.rotation.z, -right.rotation.z) &&
            viewportTransformNearlyEqual(left.rotation.w, -right.rotation.w);
        return viewportTransformNearlyEqual(left.position.x, right.position.x) &&
               viewportTransformNearlyEqual(left.position.y, right.position.y) &&
               viewportTransformNearlyEqual(left.position.z, right.position.z) &&
               (sameRotation || oppositeRotation) &&
               viewportTransformNearlyEqual(left.scale.x, right.scale.x) &&
               viewportTransformNearlyEqual(left.scale.y, right.scale.y) &&
               viewportTransformNearlyEqual(left.scale.z, right.scale.z);
    }

    [[nodiscard]] Tina::Core::Result<Tina::Scene::LocalTransform>
    localTransformFromWorld(Tina::Scene::EntityId entity,
                            Tina::Scene::WorldTransform world) const
    {
        if (!previewWorld_.has_value() || !Tina::Scene::isValid(world)) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport transform cannot publish an invalid world transform");
        }
        Tina::Scene::LocalTransform local{
            .position = world.position,
            .rotation = Tina::Scene::normalized(world.rotation),
            .scale = world.scale,
        };
        const Tina::Scene::EntityId parent = previewWorld_->parent(entity);
        if (parent.hasValue()) {
            const Tina::Scene::WorldTransform* parentWorld =
                previewWorld_->worldTransform(parent);
            if (parentWorld == nullptr || !Tina::Scene::isValid(*parentWorld)) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Viewport transform cannot resolve the target parent");
            }
            constexpr float MinimumParentScale = 1.0e-6F;
            if (std::abs(parentWorld->scale.x) <= MinimumParentScale ||
                std::abs(parentWorld->scale.y) <= MinimumParentScale ||
                std::abs(parentWorld->scale.z) <= MinimumParentScale) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Viewport transform cannot edit below a zero-scale parent");
            }
            const Tina::Scene::Quaternion inverseRotation =
                Tina::Scene::quaternionConjugate(
                    Tina::Scene::normalized(parentWorld->rotation));
            const Tina::Scene::Vec3 relative = world.position - parentWorld->position;
            const Tina::Scene::Vec3 parentLocal =
                Tina::Scene::rotate(inverseRotation, relative);
            local.position = {
                .x = parentLocal.x / parentWorld->scale.x,
                .y = parentLocal.y / parentWorld->scale.y,
                .z = parentLocal.z / parentWorld->scale.z,
            };
            local.rotation = Tina::Scene::normalized(
                Tina::Scene::quaternionMultiply(inverseRotation, world.rotation));
            local.scale = {
                .x = world.scale.x / parentWorld->scale.x,
                .y = world.scale.y / parentWorld->scale.y,
                .z = world.scale.z / parentWorld->scale.z,
            };
        }
        if (!Tina::Scene::isValid(local)) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport transform produced an invalid local transform");
        }
        if (parent.hasValue()) {
            const Tina::Scene::WorldTransform* parentWorld =
                previewWorld_->worldTransform(parent);
            if (parentWorld == nullptr ||
                !Tina::Scene::supportsTrsComposition(*parentWorld, local)) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Viewport group transform would require shear below a non-uniformly scaled parent");
            }
            Tina::Scene::WorldTransform recomposed{};
            if (!Tina::Scene::tryCompose(*parentWorld, local, recomposed) ||
                !viewportWorldTransformsEquivalent(recomposed, world)) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Viewport group transform cannot preserve the requested world TRS below its parent");
            }
        }
        return local;
    }

    [[nodiscard]] bool viewportGizmoContextMatches() const noexcept
    {
        return viewportGizmo_.workspace == workspaceMode_ &&
               viewportGizmo_.stableEntityId == stableEntityIdForHierarchyItem(selectionKey_) &&
               viewportGizmo_.selectionRevision == viewportSelectionRevision_ &&
               viewportGizmo_.baselineRevision == activeDocumentRevision() &&
               viewportGizmo_.baselineRevision == previewRevision_ &&
               previewWorld_.has_value();
    }

    [[nodiscard]] static bool viewportTransformDeltaIsIdentity(
        const Tina::Editor::EditorTransformGizmoDelta& delta) noexcept
    {
        constexpr float Epsilon = 1.0e-6F;
        return std::abs(delta.translation.x) <= Epsilon &&
               std::abs(delta.translation.y) <= Epsilon &&
               std::abs(delta.translation.z) <= Epsilon &&
               std::abs(delta.rotationDegrees) <= Epsilon &&
               std::abs(delta.scaleFactors.x - 1.0F) <= Epsilon &&
               std::abs(delta.scaleFactors.y - 1.0F) <= Epsilon &&
               std::abs(delta.scaleFactors.z - 1.0F) <= Epsilon;
    }

    [[nodiscard]] Tina::Core::Status
    finishViewportGizmoWithoutCommit(Tina::PrimaryWindowUITreeUpdater& tree, bool rejected,
                                     std::string_view feedback)
    {
        const bool group = viewportGizmo_.selectionCount > 1U;
        if (viewportTransformGizmo_.snapshot().dragging()) {
            (void)viewportTransformGizmo_.cancelDrag(ViewportPrimaryPointerToken);
        }
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
        try {
            authoringFeedback_ = group ? "Group transform | "
                                      : "Viewport transform | ";
            authoringFeedback_.append(feedback);
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Viewport transform feedback allocation failed");
        }
        return refreshAuthoringUi(tree);
    }

    [[nodiscard]] Tina::Core::Status
    commitViewportGizmoTransform(const ViewportTransformTransaction& transaction)
    {
        const std::span<const ViewportTransformTransaction::Target> targets{
            transaction.targets.data(), transaction.targetCount};
        if (transaction.workspace == WorkspaceMode::World3D) {
            std::vector<Tina::AssetFormat::PrefabNodeView> views;
            auto prefab = document3D_.parseCurrentPrefab(views);
            if (!prefab) {
                return Tina::Core::failure(std::move(prefab.error()));
            }
            std::vector<Tina::AssetFormat::PrefabNodeDesc> edited;
            try {
                edited.reserve(views.size());
                for (const auto& node : views) {
                    Tina::AssetFormat::PrefabNodeDesc candidate{
                        .stableNodeId = node.stableNodeId,
                        .parentIndex = node.parentIndex,
                        .positionX = node.positionX,
                        .positionY = node.positionY,
                        .positionZ = node.positionZ,
                        .rotationX = node.rotationX,
                        .rotationY = node.rotationY,
                        .rotationZ = node.rotationZ,
                        .rotationW = node.rotationW,
                        .scaleX = node.scaleX,
                        .scaleY = node.scaleY,
                        .scaleZ = node.scaleZ,
                        .meshId = node.meshId,
                        .materialId = node.materialId,
                        .visible = node.visible,
                    };
                    const auto target = std::find_if(
                        targets.begin(), targets.end(),
                        [&node](const auto& selected) {
                            return selected.stableEntityId == node.stableNodeId;
                        });
                    if (target != targets.end()) {
                        candidate.positionX = target->previewLocal.position.x;
                        candidate.positionY = target->previewLocal.position.y;
                        candidate.positionZ = target->previewLocal.position.z;
                        candidate.rotationX = target->previewLocal.rotation.x;
                        candidate.rotationY = target->previewLocal.rotation.y;
                        candidate.rotationZ = target->previewLocal.rotation.z;
                        candidate.rotationW = target->previewLocal.rotation.w;
                        candidate.scaleX = target->previewLocal.scale.x;
                        candidate.scaleY = target->previewLocal.scale.y;
                        candidate.scaleZ = target->previewLocal.scale.z;
                    }
                    edited.push_back(candidate);
                }
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Viewport group transform document staging allocation failed");
            }
            return document3D_.replace({.nodes = std::span<const Tina::AssetFormat::PrefabNodeDesc>{edited}});
        }

        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        for (auto& entity : storage) {
            const auto target = std::find_if(
                targets.begin(), targets.end(),
                [&entity](const auto& selected) {
                    return selected.stableEntityId == entity.stableEntityId;
                });
            if (target == targets.end()) {
                continue;
            }
            entity.positionX = target->previewLocal.position.x;
            entity.positionY = target->previewLocal.position.y;
            entity.positionZ = target->previewLocal.position.z;
            entity.rotationX = target->previewLocal.rotation.x;
            entity.rotationY = target->previewLocal.rotation.y;
            entity.rotationZ = target->previewLocal.rotation.z;
            entity.rotationW = target->previewLocal.rotation.w;
            entity.scaleX = target->previewLocal.scale.x;
            entity.scaleY = target->previewLocal.scale.y;
            entity.scaleZ = target->previewLocal.scale.z;
        }
        return document_.replace({
            .entities = std::span<const Tina::AssetFormat::World2DEntityDesc>{storage},
            .gameplaySchema = snapshot->gameplaySchema,
            .gameplayVersion = snapshot->gameplayVersion,
            .gameplayBytes = snapshot->gameplayBytes,
        });
    }

    [[nodiscard]] Tina::Core::Status
    processViewportGizmo(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (!viewportGizmo_.captured) {
            return Tina::Core::success();
        }
        if (viewportGizmo_.cancelRequested) {
            return finishViewportGizmoWithoutCommit(
                tree, false, "cancelled; document unchanged");
        }
        if (!viewportGizmoContextMatches() || pendingEditorCommand_.has_value()) {
            return finishViewportGizmoWithoutCommit(
                tree, true,
                "rejected after selection, workspace, or document revision changed");
        }

        if (viewportGizmo_.targetCount == 0U) {
            return finishViewportGizmoWithoutCommit(
                tree, true, "rejected because preview targets are unavailable");
        }
        if (!viewportGizmo_.baselineReady) {
            viewportGizmo_.baselineReady = true;
            ++counters_.viewportGizmoBegins;
            if (viewportGizmo_.selectionCount > 1U) {
                try {
                    authoringFeedback_ = "Group transform started | ";
                    authoringFeedback_ +=
                        std::to_string(viewportGizmo_.selectionCount);
                    authoringFeedback_ += " selected | Group pivot";
                } catch (const std::bad_alloc&) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::OutOfMemory,
                        "Group transform start feedback allocation failed");
                }
            } else {
                authoringFeedback_ = "Viewport transform started";
            }
            if (auto status = tree.setText(authoringHint_, authoringFeedback_);
                !status) {
                return status;
            }
        }

        const auto& snapshot = viewportTransformGizmo_.snapshot();
        if (snapshot.revision != viewportGizmo_.processedGizmoRevision) {
            viewportGizmo_.processedGizmoRevision = snapshot.revision;
            viewportGizmo_.delta = snapshot.delta;
            bool changed = false;
            Tina::Scene::Quaternion localBasis = viewportGizmo_.baselineTransform.rotation;
            if (viewportGizmo_.targetCount != 0U) {
                localBasis = viewportGizmo_.targets[0].baselineWorld.rotation;
            }
            std::array<Tina::Scene::LocalTransform,
                       ViewportTransformTargetCapacity>
                stagedLocals{};
            for (Tina::Core::usize targetIndex = 0;
                 targetIndex < viewportGizmo_.targetCount; ++targetIndex) {
                auto& target = viewportGizmo_.targets[targetIndex];
                auto world = applyViewportWorldTransformDelta(
                    target.baselineWorld, viewportGizmo_.pivot,
                    viewportGizmo_.delta, localBasis);
                if (!world) {
                    return finishViewportGizmoWithoutCommit(
                        tree, true, world.error().message);
                }
                auto local = localTransformFromWorld(target.entity, *world);
                if (!local) {
                    return finishViewportGizmoWithoutCommit(
                        tree, true, local.error().message);
                }
                stagedLocals[targetIndex] = *local;
                changed = changed || *local != target.previewLocal;
            }
            if (changed) {
                for (Tina::Core::usize targetIndex = 0;
                     targetIndex < viewportGizmo_.targetCount; ++targetIndex) {
                    auto& target = viewportGizmo_.targets[targetIndex];
                    target.previewLocal = stagedLocals[targetIndex];
                    if (auto status = previewWorld_->setLocalTransform(
                            target.entity, target.previewLocal);
                        !status) {
                        return status;
                    }
                }
                viewportGizmo_.previewTransform =
                    viewportGizmo_.targets[0].previewLocal;
                if (auto status = previewWorld_->updateWorldTransforms(); !status) {
                    return status;
                }
                viewportGizmo_.previewPublished = true;
                ++counters_.viewportGizmoPreviews;
                switch (snapshot.mode) {
                case Tina::Editor::EditorTransformGizmoMode::Rotate:
                    counters_.viewportGizmoRotationDegrees =
                        viewportGizmo_.delta.rotationDegrees;
                    authoringFeedback_ = viewportGizmo_.selectionCount > 1U
                                             ? "Group rotation preview"
                                             : "Viewport rotation preview";
                    break;
                case Tina::Editor::EditorTransformGizmoMode::Scale:
                    counters_.viewportGizmoScaleFactorX =
                        viewportGizmo_.delta.scaleFactors.x;
                    counters_.viewportGizmoScaleFactorY =
                        viewportGizmo_.delta.scaleFactors.y;
                    counters_.viewportGizmoScaleFactorZ =
                        viewportGizmo_.delta.scaleFactors.z;
                    authoringFeedback_ = viewportGizmo_.selectionCount > 1U
                                             ? "Group scale preview"
                                             : "Viewport scale preview";
                    break;
                case Tina::Editor::EditorTransformGizmoMode::Translate:
                default:
                    counters_.viewportGizmoWorldDeltaX =
                        viewportGizmo_.delta.translation.x;
                    counters_.viewportGizmoWorldDeltaY =
                        viewportGizmo_.delta.translation.y;
                    counters_.viewportGizmoWorldDeltaZ =
                        viewportGizmo_.delta.translation.z;
                    authoringFeedback_ = viewportGizmo_.selectionCount > 1U
                                             ? "Group translation preview"
                                             : (workspaceMode_ == WorkspaceMode::World2D
                                                    ? "Move preview on the World2D XY plane"
                                                    : "Move preview in the World3D viewport");
                    break;
                }
                if (auto status = tree.setText(authoringHint_, authoringFeedback_);
                    !status) {
                    return status;
                }
            }
        }

        if (!viewportGizmo_.commitRequested) {
            return Tina::Core::success();
        }
        if (viewportTransformDeltaIsIdentity(viewportGizmo_.delta)) {
            return finishViewportGizmoWithoutCommit(tree, false,
                                                    "ended without a document change");
        }
        if (!viewportGizmoContextMatches()) {
            return finishViewportGizmoWithoutCommit(
                tree, true, "rejected before commit because its baseline changed");
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
                tree, false, "ended without a document change");
        }
        if (committedRevision < viewportGizmo_.baselineRevision ||
            committedRevision - viewportGizmo_.baselineRevision != 1U) {
            viewportGizmo_ = {};
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "viewport gizmo commit did not publish exactly one document revision");
        }
        const Tina::Core::usize selectionCount = viewportGizmo_.selectionCount;
        const Tina::Core::usize targetCount = viewportGizmo_.targetCount;
        const bool multiSelection = selectionCount > 1U;
        const bool multiTarget = targetCount > 1U;
        const Tina::Editor::EditorTransformGizmoMode committedMode = snapshot.mode;
        viewportGizmo_ = {};
        ++counters_.viewportGizmoCommits;
        counters_.viewportMaximumGizmoTargets = (std::max)(
            counters_.viewportMaximumGizmoTargets,
            static_cast<u64>(targetCount));
        switch (committedMode) {
        case Tina::Editor::EditorTransformGizmoMode::Translate:
            ++counters_.viewportTranslateGizmoCommits;
            break;
        case Tina::Editor::EditorTransformGizmoMode::Rotate:
            ++counters_.viewportRotateGizmoCommits;
            if (multiTarget) {
                ++counters_.viewportGroupRotateGizmoCommits;
            }
            break;
        case Tina::Editor::EditorTransformGizmoMode::Scale:
            ++counters_.viewportScaleGizmoCommits;
            if (multiTarget) {
                ++counters_.viewportGroupScaleGizmoCommits;
            }
            break;
        }
        if (multiTarget) {
            ++counters_.viewportGroupGizmoCommits;
        }
        ++counters_.authoringEdits;
        if (auto status = validateRuntimePreview(); !status) {
            return status;
        }
        if (multiSelection) {
            try {
                authoringFeedback_ = "Group transform committed as one document revision | ";
                authoringFeedback_ += std::to_string(selectionCount);
                authoringFeedback_ += " selected | Group pivot";
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Group transform commit feedback allocation failed");
            }
        } else {
            authoringFeedback_ =
                "Viewport transform committed as one document revision";
        }
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
        const bool authoring = authoringEnabled();
        const bool tileToolsAvailable = authoring && tileMapEditingContext();
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
        for (const UI::UINodeId button : translateToolButtons_) {
            if (auto status = tree.setEnabled(
                    button, authoring && viewportToolMode_ != ViewportToolMode::Translate);
                !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : rotateToolButtons_) {
            if (auto status = tree.setEnabled(
                    button, authoring && viewportToolMode_ != ViewportToolMode::Rotate);
                !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : scaleToolButtons_) {
            if (auto status = tree.setEnabled(
                    button, authoring && viewportToolMode_ != ViewportToolMode::Scale);
                !status) {
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
        const bool transformTool = viewportToolMode_ == ViewportToolMode::Translate ||
                                   viewportToolMode_ == ViewportToolMode::Rotate ||
                                   viewportToolMode_ == ViewportToolMode::Scale;
        const bool interactionActive = viewportGizmo_.captured ||
                                       viewportNavigationDrag_.captured ||
                                       viewportMarquee_.captured;
        if (auto status = tree.setEnabled(
                orientationButton_, authoring && transformTool && !interactionActive);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                snapButton_, authoring && transformTool && !interactionActive);
            !status) {
            return status;
        }

        const auto orientation = viewportTransformGizmo_.snapshot().orientation;
        const bool snapEnabled = viewportTransformGizmo_.snap().enabled;
        if (auto status = tree.setText(
                orientationButton_,
                orientation == Tina::Editor::EditorTransformGizmoOrientation::World
                    ? "World"
                    : "Local");
            !status) {
            return status;
        }
        if (auto status = tree.setText(snapButton_,
                                       snapEnabled ? "Snap On" : "Snap Off");
            !status) {
            return status;
        }

        constexpr std::array marqueeModes{
            Tina::Editor::EditorMarqueeSelectionMode::Replace,
            Tina::Editor::EditorMarqueeSelectionMode::Add,
            Tina::Editor::EditorMarqueeSelectionMode::Toggle,
        };
        for (Tina::Core::usize index = 0; index < marqueeModeButtons_.size();
             ++index) {
            const bool active = marqueeSelectionMode_ == marqueeModes[index];
            if (auto status = tree.setEnabled(
                    marqueeModeButtons_[index], selectActive && !active &&
                                                    !interactionActive);
                !status) {
                return status;
            }
        }

        std::string toolStatus;
        switch (viewportToolMode_) {
        case ViewportToolMode::Translate:
            toolStatus = "Translate";
            break;
        case ViewportToolMode::Rotate:
            toolStatus = "Rotate";
            break;
        case ViewportToolMode::Scale:
            toolStatus = "Scale";
            break;
        case ViewportToolMode::TilePaint:
            toolStatus = "Tile Paint | Grid Snap";
            break;
        case ViewportToolMode::TileErase:
            toolStatus = "Tile Erase | Grid Snap";
            break;
        case ViewportToolMode::Select:
        default:
            toolStatus = "Select | ";
            switch (marqueeSelectionMode_) {
            case Tina::Editor::EditorMarqueeSelectionMode::Add:
                toolStatus += "Add";
                break;
            case Tina::Editor::EditorMarqueeSelectionMode::Toggle:
                toolStatus += "Toggle";
                break;
            case Tina::Editor::EditorMarqueeSelectionMode::Replace:
            default:
                toolStatus += "Replace";
                break;
            }
            return tree.setText(viewportToolStatus_, toolStatus);
        }
        if (transformTool) {
            toolStatus += orientation ==
                                  Tina::Editor::EditorTransformGizmoOrientation::World
                              ? " | World"
                              : " | Local";
            toolStatus += snapEnabled ? " | Snap" : " | Free";
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
        const Tina::Scene::PerspectiveCamera3D* currentCamera =
            previewWorld_->perspectiveCamera3D(previewCamera3D_);
        if (currentCamera == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "editor World3D viewport camera component is unavailable");
        }
        Tina::Scene::PerspectiveCamera3D camera = *currentCamera;
        camera.normalizedViewport = *viewportNormalized_;
        camera.active = true;
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

    [[nodiscard]] ViewportProjectedPoint projectViewportWorldPoint(
        Tina::Scene::Vec3 worldPoint) const noexcept
    {
        if (!previewWorld_.has_value() || viewportLogicalRect_.width <= 0.0F ||
            viewportLogicalRect_.height <= 0.0F) {
            return {};
        }
        if (workspaceMode_ == WorkspaceMode::World2D) {
            const Tina::Scene::WorldTransform* camera =
                previewWorld_->worldTransform(previewCamera2D_);
            if (camera == nullptr) {
                return {};
            }
            const float normalizedX =
                (worldPoint.x - camera->position.x) / viewportWorldWidth() + 0.5F;
            const float normalizedY =
                0.5F - (worldPoint.y - camera->position.y) / viewportWorldHeight();
            if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY)) {
                return {};
            }
            return {
                .screen = {
                    .x = viewportLogicalRect_.x + normalizedX * viewportLogicalRect_.width,
                    .y = viewportLogicalRect_.y + normalizedY * viewportLogicalRect_.height,
                },
                .cameraDepth = 1.0F,
                .projectable = normalizedX >= -0.25F && normalizedX <= 1.25F &&
                               normalizedY >= -0.25F && normalizedY <= 1.25F,
            };
        }

        const Tina::Scene::WorldTransform* camera =
            previewWorld_->worldTransform(previewCamera3D_);
        if (camera == nullptr) {
            return {};
        }
        const Tina::Scene::Vec3 relative = worldPoint - camera->position;
        const Tina::Scene::Vec3 cameraSpace = Tina::Scene::rotate(
            Tina::Scene::quaternionConjugate(camera->rotation), relative);
        const float depth = -cameraSpace.z;
        if (!std::isfinite(depth) || depth <= 0.01F) {
            return {};
        }
        const float tangent = std::tan(55.0F * DegreesToRadians * 0.5F);
        const float aspect = viewportLogicalRect_.width / viewportLogicalRect_.height;
        const float normalizedDeviceX = cameraSpace.x / (depth * tangent * aspect);
        const float normalizedDeviceY = cameraSpace.y / (depth * tangent);
        if (!std::isfinite(normalizedDeviceX) ||
            !std::isfinite(normalizedDeviceY)) {
            return {};
        }
        return {
            .screen = {
                .x = viewportLogicalRect_.x +
                     (normalizedDeviceX * 0.5F + 0.5F) * viewportLogicalRect_.width,
                .y = viewportLogicalRect_.y +
                     (0.5F - normalizedDeviceY * 0.5F) * viewportLogicalRect_.height,
            },
            .cameraDepth = depth,
            .projectable = normalizedDeviceX >= -1.5F &&
                           normalizedDeviceX <= 1.5F &&
                           normalizedDeviceY >= -1.5F &&
                           normalizedDeviceY <= 1.5F,
        };
    }

    [[nodiscard]] static UI::UIStraightSrgba8Color viewportGizmoColor(
        Tina::Editor::EditorTransformGizmoHandle handle,
        bool highlighted) noexcept
    {
        using Handle = Tina::Editor::EditorTransformGizmoHandle;
        if (highlighted) {
            return UI::rgb(0xF4F8FC, 250);
        }
        switch (handle) {
        case Handle::AxisX:
            return UI::rgb(0xE16060, 235);
        case Handle::AxisY:
            return UI::rgb(0x52BE7A, 235);
        case Handle::AxisZ:
            return UI::rgb(0x5894EA, 235);
        case Handle::PlaneXY:
            return UI::rgb(0xE8D45C, 210);
        case Handle::PlaneXZ:
            return UI::rgb(0xD75EC8, 210);
        case Handle::PlaneYZ:
            return UI::rgb(0x59C9C6, 210);
        case Handle::Uniform:
            return UI::rgb(0xE8EDF3, 235);
        case Handle::None:
        default:
            return UI::rgb(0x9AA8B8, 180);
        }
    }

    [[nodiscard]] Tina::Core::Status collapseViewportGizmoVisuals(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        for (Tina::Core::usize index = 0;
             index < viewportGizmoVisibleNodeCount_; ++index) {
            UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
            collapsed.placement = UI::UILayoutPlacement::Overlay;
            collapsed.visibility = UI::UIVisibility::Collapsed;
            if (auto status = tree.setLayoutStyle(
                    viewportGizmoVisualNodes_[index], collapsed);
                !status) {
                return status;
            }
        }
        viewportGizmoVisibleNodeCount_ = 0;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status materializeViewportGizmoVisuals(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        Tina::Core::usize nodeCount = 0;
        const auto& snapshot = viewportTransformGizmo_.snapshot();
        const auto appendRect = [&](UI::UILogicalRect screenRect,
                                    UI::UIStraightSrgba8Color color)
            -> Tina::Core::Status {
            if (!std::isfinite(screenRect.x) || !std::isfinite(screenRect.y) ||
                !std::isfinite(screenRect.width) ||
                !std::isfinite(screenRect.height) || screenRect.width <= 0.0F ||
                screenRect.height <= 0.0F) {
                return Tina::Core::success();
            }
            const float localLeft = screenRect.x - viewportLogicalRect_.x;
            const float localTop = screenRect.y - viewportLogicalRect_.y;
            const float localRight = localLeft + screenRect.width;
            const float localBottom = localTop + screenRect.height;
            const float clippedLeft = std::clamp(
                localLeft, 0.0F, viewportLogicalRect_.width);
            const float clippedTop = std::clamp(
                localTop, 0.0F, viewportLogicalRect_.height);
            const float clippedRight = std::clamp(
                localRight, 0.0F, viewportLogicalRect_.width);
            const float clippedBottom = std::clamp(
                localBottom, 0.0F, viewportLogicalRect_.height);
            if (!(clippedRight > clippedLeft) ||
                !(clippedBottom > clippedTop)) {
                return Tina::Core::success();
            }
            if (nodeCount == viewportGizmoVisualNodes_.size()) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                    "editor transform gizmo visual exceeded fixed node capacity");
            }
            UI::UILayoutStyle style = fixedSize(clippedRight - clippedLeft,
                                                clippedBottom - clippedTop);
            style.placement = UI::UILayoutPlacement::Overlay;
            style.overlay.horizontal = UI::UIAxisAlignment::Start;
            style.overlay.vertical = UI::UIAxisAlignment::Start;
            style.overlay.offset.x = UI::UILayoutLength::Px(clippedLeft);
            style.overlay.offset.y = UI::UILayoutLength::Px(clippedTop);
            const UI::UINodeId node = viewportGizmoVisualNodes_[nodeCount++];
            if (auto status = tree.setLayoutStyle(node, style); !status) {
                return status;
            }
            return tree.setBoxPaint(node, UI::makeSolidBox(color));
        };
        const auto appendLine = [&](Tina::Editor::EditorTransformGizmoPoint start,
                                    Tina::Editor::EditorTransformGizmoPoint end,
                                    u32 steps,
                                    Tina::Editor::EditorTransformGizmoHandle handle,
                                    bool highlighted) -> Tina::Core::Status {
            const UI::UIStraightSrgba8Color color =
                viewportGizmoColor(handle, highlighted);
            const float thickness = highlighted ? 3.0F : 2.0F;
            for (u32 step = 0; step < steps; ++step) {
                const float firstT = static_cast<float>(step) /
                                     static_cast<float>(steps);
                const float secondT = static_cast<float>(step + 1U) /
                                      static_cast<float>(steps);
                const Tina::Editor::EditorTransformGizmoPoint first{
                    .x = start.x + (end.x - start.x) * firstT,
                    .y = start.y + (end.y - start.y) * firstT,
                };
                const Tina::Editor::EditorTransformGizmoPoint second{
                    .x = start.x + (end.x - start.x) * secondT,
                    .y = start.y + (end.y - start.y) * secondT,
                };
                const float horizontalLength = std::abs(second.x - first.x);
                if (horizontalLength > 1.0e-4F) {
                    if (auto status = appendRect(
                            {
                                .x = (std::min)(first.x, second.x) -
                                     thickness * 0.5F,
                                .y = first.y - thickness * 0.5F,
                                .width = horizontalLength + thickness,
                                .height = thickness,
                            },
                            color);
                        !status) {
                        return status;
                    }
                }
                const float verticalLength = std::abs(second.y - first.y);
                if (verticalLength > 1.0e-4F) {
                    if (auto status = appendRect(
                            {
                                .x = second.x - thickness * 0.5F,
                                .y = (std::min)(first.y, second.y) -
                                     thickness * 0.5F,
                                .width = thickness,
                                .height = verticalLength + thickness,
                            },
                            color);
                        !status) {
                        return status;
                    }
                }
            }
            return Tina::Core::success();
        };

        for (const auto& geometry : snapshot.handles()) {
            const bool highlighted = geometry.handle == snapshot.activeHandle ||
                                     geometry.handle == snapshot.hoveredHandle;
            if (geometry.shape ==
                Tina::Editor::EditorTransformGizmoHandleShape::Segment) {
                constexpr u32 SegmentSteps = 4;
                if (auto status = appendLine(geometry.points[0], geometry.points[1],
                                             SegmentSteps, geometry.handle,
                                             highlighted);
                    !status) {
                    return status;
                }
            } else if (geometry.shape ==
                       Tina::Editor::EditorTransformGizmoHandleShape::Ring) {
                constexpr u32 RingSteps = 8;
                for (u32 step = 0; step < RingSteps; ++step) {
                    const float firstRadians =
                        static_cast<float>(step) / static_cast<float>(RingSteps) *
                        std::numbers::pi_v<float> * 2.0F;
                    const float secondRadians =
                        static_cast<float>(step + 1U) /
                        static_cast<float>(RingSteps) *
                        std::numbers::pi_v<float> * 2.0F;
                    const Tina::Editor::EditorTransformGizmoPoint first{
                        .x = geometry.points[0].x +
                             std::cos(firstRadians) * geometry.radiusPixels,
                        .y = geometry.points[0].y +
                             std::sin(firstRadians) * geometry.radiusPixels,
                    };
                    const Tina::Editor::EditorTransformGizmoPoint second{
                        .x = geometry.points[0].x +
                             std::cos(secondRadians) * geometry.radiusPixels,
                        .y = geometry.points[0].y +
                             std::sin(secondRadians) * geometry.radiusPixels,
                    };
                    if (auto status = appendLine(first, second, 1U,
                                                 geometry.handle, highlighted);
                        !status) {
                        return status;
                    }
                }
            } else {
                if (geometry.pointCount < 3U) {
                    continue;
                }
                float minimumX = geometry.points[0].x;
                float minimumY = geometry.points[0].y;
                float maximumX = geometry.points[0].x;
                float maximumY = geometry.points[0].y;
                for (u32 pointIndex = 1; pointIndex < geometry.pointCount;
                     ++pointIndex) {
                    minimumX = (std::min)(minimumX, geometry.points[pointIndex].x);
                    minimumY = (std::min)(minimumY, geometry.points[pointIndex].y);
                    maximumX = (std::max)(maximumX, geometry.points[pointIndex].x);
                    maximumY = (std::max)(maximumY, geometry.points[pointIndex].y);
                }
                UI::UIStraightSrgba8Color fillColor =
                    viewportGizmoColor(geometry.handle, highlighted);
                fillColor.alpha = highlighted ? 82U : 44U;
                if (auto status = appendRect(
                        {
                            .x = minimumX,
                            .y = minimumY,
                            .width = maximumX - minimumX,
                            .height = maximumY - minimumY,
                        },
                        fillColor);
                    !status) {
                    return status;
                }
                for (u32 pointIndex = 0; pointIndex < geometry.pointCount;
                     ++pointIndex) {
                    const u32 nextPoint = (pointIndex + 1U) % geometry.pointCount;
                    if (auto status = appendLine(
                            geometry.points[pointIndex], geometry.points[nextPoint],
                            1U, geometry.handle, highlighted);
                        !status) {
                        return status;
                    }
                }
            }
        }
        for (Tina::Core::usize index = nodeCount;
             index < viewportGizmoVisibleNodeCount_; ++index) {
            UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
            collapsed.placement = UI::UILayoutPlacement::Overlay;
            collapsed.visibility = UI::UIVisibility::Collapsed;
            if (auto status = tree.setLayoutStyle(
                    viewportGizmoVisualNodes_[index], collapsed);
                !status) {
                return status;
            }
        }
        viewportGizmoVisibleNodeCount_ = nodeCount;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status updateViewportTransformGizmo(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const bool transformTool =
            viewportToolMode_ == ViewportToolMode::Translate ||
            viewportToolMode_ == ViewportToolMode::Rotate ||
            viewportToolMode_ == ViewportToolMode::Scale;
        const Tina::Scene::EntityId entity =
            findPreviewEntity(stableEntityIdForHierarchyItem(selectionKey_));
        const Tina::Scene::WorldTransform* transform =
            previewWorld_.has_value() && entity.hasValue()
                ? previewWorld_->worldTransform(entity)
                : nullptr;
        if (!transformTool || transform == nullptr) {
            return collapseViewportGizmoVisuals(tree);
        }
        if (!viewportGizmo_.captured) {
            const Tina::Scene::Vec3 gizmoOrigin = viewportSelectedEntityCount_ > 1U
                                                      ? viewportSelectionPivot()
                                                      : transform->position;
            const ViewportProjectedPoint origin =
                projectViewportWorldPoint(gizmoOrigin);
            if (!origin.projectable) {
                return collapseViewportGizmoVisuals(tree);
            }
            constexpr std::array worldDirections{
                Tina::Scene::Vec3{1.0F, 0.0F, 0.0F},
                Tina::Scene::Vec3{0.0F, 1.0F, 0.0F},
                Tina::Scene::Vec3{0.0F, 0.0F, 1.0F},
            };
            Tina::Editor::EditorTransformGizmoFrame frame{
                .dimension = workspaceMode_ == WorkspaceMode::World2D
                                 ? Tina::Editor::EditorTransformGizmoDimension::TwoD
                                 : Tina::Editor::EditorTransformGizmoDimension::ThreeD,
                .screenOrigin = {.x = origin.screen.x, .y = origin.screen.y},
            };
            for (Tina::Core::usize axis = 0; axis < worldDirections.size();
                 ++axis) {
                const Tina::Scene::Vec3 worldDirection = worldDirections[axis];
                const Tina::Scene::Vec3 localDirection =
                    Tina::Scene::rotate(transform->rotation, worldDirection);
                const ViewportProjectedPoint worldEndpoint =
                    projectViewportWorldPoint(gizmoOrigin + worldDirection);
                const ViewportProjectedPoint localEndpoint =
                    projectViewportWorldPoint(gizmoOrigin + localDirection);
                frame.worldAxes[axis] = {
                    .screenPerWorldUnit = {
                        .x = worldEndpoint.projectable
                                 ? worldEndpoint.screen.x - origin.screen.x
                                 : 0.0F,
                        .y = worldEndpoint.projectable
                                 ? worldEndpoint.screen.y - origin.screen.y
                                 : 0.0F,
                    },
                    .worldDirection = {
                        .x = worldDirection.x,
                        .y = worldDirection.y,
                        .z = worldDirection.z,
                    },
                };
                frame.localAxes[axis] = {
                    .screenPerWorldUnit = {
                        .x = localEndpoint.projectable
                                 ? localEndpoint.screen.x - origin.screen.x
                                 : 0.0F,
                        .y = localEndpoint.projectable
                                 ? localEndpoint.screen.y - origin.screen.y
                                 : 0.0F,
                    },
                    .worldDirection = {
                        .x = localDirection.x,
                        .y = localDirection.y,
                        .z = localDirection.z,
                    },
                };
            }
            if (viewportTransformGizmo_.publishFrame(frame) !=
                Tina::Editor::EditorTransformGizmoOperation::Success) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidConfiguration,
                    "editor transform gizmo could not publish its projected frame");
            }
        }
        return materializeViewportGizmoVisuals(tree);
    }

    [[nodiscard]] Tina::Core::Status updateViewportMarqueeVisual(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        UI::UILayoutStyle style = fixedSize(1.0F, 1.0F);
        style.placement = UI::UILayoutPlacement::Overlay;
        if (!viewportMarquee_.captured || viewportMarquee_.cancelRequested) {
            style.visibility = UI::UIVisibility::Collapsed;
            return tree.setLayoutStyle(viewportMarqueeNode_, style);
        }
        const float left = (std::min)(viewportMarquee_.start.x,
                                      viewportMarquee_.current.x) -
                           viewportLogicalRect_.x;
        const float top = (std::min)(viewportMarquee_.start.y,
                                     viewportMarquee_.current.y) -
                          viewportLogicalRect_.y;
        const float width = std::abs(viewportMarquee_.current.x -
                                     viewportMarquee_.start.x);
        const float height = std::abs(viewportMarquee_.current.y -
                                      viewportMarquee_.start.y);
        style.overlay.horizontal = UI::UIAxisAlignment::Start;
        style.overlay.vertical = UI::UIAxisAlignment::Start;
        style.overlay.offset.x = UI::UILayoutLength::Px(
            std::clamp(left, 0.0F, viewportLogicalRect_.width));
        style.overlay.offset.y = UI::UILayoutLength::Px(
            std::clamp(top, 0.0F, viewportLogicalRect_.height));
        style.size.width = UI::UILayoutLength::Px(
            std::clamp(width, 1.0F, viewportLogicalRect_.width));
        style.size.height = UI::UILayoutLength::Px(
            std::clamp(height, 1.0F, viewportLogicalRect_.height));
        return tree.setLayoutStyle(viewportMarqueeNode_, style);
    }

    [[nodiscard]] std::optional<u64> hierarchyIndexForStableId(
        u64 stableId) const noexcept
    {
        for (u64 index = 0; index < hierarchyItemCount(this); ++index) {
            UI::UITreeViewItemDescriptor descriptor{};
            if (resolveHierarchyItem(this, index, descriptor) &&
                stableEntityIdForHierarchyItem(descriptor.key) == stableId) {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<u32> automaticHierarchyStableId(
        bool preferLast) const noexcept
    {
        if (!previewWorld_.has_value()) {
            return std::nullopt;
        }
        const auto transformable = [this](const EditorHierarchyRow& row) noexcept {
            if (row.stableId == 0U || !hierarchyRowVisible(row)) {
                return false;
            }
            const Tina::Scene::EntityId entity = findPreviewEntity(row.stableId);
            return entity.hasValue() && previewWorld_->worldTransform(entity) != nullptr;
        };
        if (preferLast) {
            const auto row = std::find_if(hierarchyRows_.rbegin(),
                                          hierarchyRows_.rend(), transformable);
            return row == hierarchyRows_.rend()
                       ? std::nullopt
                       : std::optional<u32>{row->stableId};
        }
        const auto row = std::find_if(hierarchyRows_.begin(), hierarchyRows_.end(),
                                      transformable);
        return row == hierarchyRows_.end()
                   ? std::nullopt
                   : std::optional<u32>{row->stableId};
    }

    void synchronizeViewportSelectionFromHierarchy() noexcept
    {
        const Tina::Core::usize previousCount = viewportSelectedEntityCount_;
        const auto previousSelection = std::span<const u64>(
            viewportSelectedEntityIds_.data(), previousCount);
        std::array<u64, Tina::Editor::EditorMarqueeSelectionCapacity> next{};
        Tina::Core::usize nextCount = 0;
        viewportSelectedEntityCount_ = 0;
        if (assetInspectorActive_) {
            if (!previousSelection.empty()) {
                ++viewportSelectionRevision_;
            }
            return;
        }
        const u64 stableId = stableEntityIdForHierarchyItem(selectionKey_);
        if (stableId != 0U) {
            next[0] = stableId;
            nextCount = 1;
        }
        const bool changed = previousSelection.size() != nextCount ||
                             !std::equal(previousSelection.begin(),
                                         previousSelection.end(), next.begin());
        if (changed) {
            std::copy_n(next.begin(), nextCount,
                        viewportSelectedEntityIds_.begin());
            ++viewportSelectionRevision_;
        }
        viewportSelectedEntityCount_ = nextCount;
    }

    [[nodiscard]] Tina::Core::usize collectViewportMarqueeCandidates(
        std::span<Tina::Editor::EditorMarqueeCandidate> output) const noexcept
    {
        if (!previewWorld_.has_value()) {
            return 0;
        }
        Tina::Core::usize count = 0;
        const auto append = [&](u64 stableId, Tina::Scene::EntityId entity) {
            if (stableId == 0U || count == output.size()) {
                return;
            }
            const Tina::Scene::WorldTransform* transform =
                previewWorld_->worldTransform(entity);
            if (transform == nullptr) {
                return;
            }
            const ViewportProjectedPoint projected =
                projectViewportWorldPoint(transform->position);
            if (!projected.projectable) {
                return;
            }
            float halfWidth = 9.0F;
            float halfHeight = 9.0F;
            if (workspaceMode_ == WorkspaceMode::World2D) {
                if (const Tina::Scene::SpriteRenderer2D* sprite =
                        previewWorld_->spriteRenderer2D(entity);
                    sprite != nullptr) {
                    halfWidth = sprite->sizeOverrideMeters.x *
                                std::abs(transform->scale.x) /
                                viewportWorldWidth() *
                                viewportLogicalRect_.width * 0.5F;
                    halfHeight = sprite->sizeOverrideMeters.y *
                                 std::abs(transform->scale.y) /
                                 viewportWorldHeight() *
                                 viewportLogicalRect_.height * 0.5F;
                }
            } else if (const Tina::Scene::MeshRenderer3D* mesh =
                           previewWorld_->meshRenderer3D(entity);
                       mesh != nullptr) {
                const float maximumScale = (std::max)(
                    {std::abs(transform->scale.x), std::abs(transform->scale.y),
                     std::abs(transform->scale.z)});
                const float pixelsPerWorldUnit =
                    viewportLogicalRect_.height /
                    (2.0F * projected.cameraDepth *
                     std::tan(55.0F * DegreesToRadians * 0.5F));
                halfWidth = halfHeight = (std::max)(
                    6.0F, mesh->localBounds.radius * maximumScale *
                              pixelsPerWorldUnit);
            }
            output[count++] = {
                .stableId = stableId,
                .screenBounds = {
                    .x0 = projected.screen.x - halfWidth,
                    .y0 = projected.screen.y - halfHeight,
                    .x1 = projected.screen.x + halfWidth,
                    .y1 = projected.screen.y + halfHeight,
                },
            };
        };
        if (workspaceMode_ == WorkspaceMode::World2D) {
            for (const auto& binding : previewBindings_) {
                append(binding.stableEntityId, binding.entity);
            }
        } else {
            for (const auto& binding : preview3DBindings_) {
                append(binding.stableNodeId, binding.entity);
            }
        }
        return count;
    }

    [[nodiscard]] Tina::Core::Status processViewportMarquee(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (!viewportMarquee_.captured) {
            return Tina::Core::success();
        }
        if (viewportMarquee_.cancelRequested) {
            viewportMarquee_ = {};
            ++counters_.viewportMarqueeCancels;
            return Tina::Core::success();
        }
        if (!viewportMarquee_.commitRequested) {
            return Tina::Core::success();
        }
        std::array<Tina::Editor::EditorMarqueeCandidate,
                   ViewportMarqueeCandidateCapacity>
            candidates{};
        const Tina::Core::usize candidateCount =
            collectViewportMarqueeCandidates(candidates);
        const auto evaluated = Tina::Editor::EditorMarqueeSelection::Evaluate(
            {
                .x0 = viewportMarquee_.start.x,
                .y0 = viewportMarquee_.start.y,
                .x1 = viewportMarquee_.current.x,
                .y1 = viewportMarquee_.current.y,
            },
            std::span{candidates.data(), candidateCount},
            std::span{viewportSelectedEntityIds_.data(),
                      viewportSelectedEntityCount_},
            viewportMarquee_.mode);
        const Tina::Editor::EditorMarqueeSelectionMode committedMode =
            viewportMarquee_.mode;
        viewportMarquee_ = {};
        if (!evaluated) {
            ++counters_.viewportMarqueeRejects;
            return Tina::Core::failure(std::move(evaluated.error()));
        }
        ++counters_.viewportMarqueeCommits;
        switch (committedMode) {
        case Tina::Editor::EditorMarqueeSelectionMode::Replace:
            ++counters_.viewportMarqueeReplaceCommits;
            break;
        case Tina::Editor::EditorMarqueeSelectionMode::Add:
            ++counters_.viewportMarqueeAddCommits;
            break;
        case Tina::Editor::EditorMarqueeSelectionMode::Toggle:
            ++counters_.viewportMarqueeToggleCommits;
            break;
        }
        if (evaluated->changed()) {
            ++counters_.viewportMarqueeSelectionChanges;
        }
        counters_.viewportMarqueeAddedItems += evaluated->added().size();
        counters_.viewportMarqueeRemovedItems += evaluated->removed().size();
        counters_.viewportMarqueeMaximumSelection = (std::max)(
            counters_.viewportMarqueeMaximumSelection,
            static_cast<u64>(evaluated->selection().size()));
        std::array<u64, Tina::Editor::EditorMarqueeSelectionCapacity>
            previousSelectionStorage{};
        const Tina::Core::usize previousSelectionCount =
            viewportSelectedEntityCount_;
        std::copy_n(viewportSelectedEntityIds_.begin(),
                    previousSelectionCount, previousSelectionStorage.begin());
        viewportSelectedEntityCount_ = evaluated->selection().size();
        std::copy(evaluated->selection().begin(), evaluated->selection().end(),
                  viewportSelectedEntityIds_.begin());
        if (previousSelectionCount != viewportSelectedEntityCount_ ||
            !std::equal(previousSelectionStorage.begin(),
                        previousSelectionStorage.begin() +
                            static_cast<std::ptrdiff_t>(previousSelectionCount),
                        viewportSelectedEntityIds_.begin())) {
            ++viewportSelectionRevision_;
        }
        const std::optional<u64> hierarchyIndex =
            viewportSelectedEntityCount_ == 0U
                ? std::optional<u64>{0U}
                : hierarchyIndexForStableId(viewportSelectedEntityIds_[0]);
        if (hierarchyIndex.has_value()) {
            if (auto status = tree.setTreeViewSelectedIndex(
                    hierarchyTree_, *hierarchyIndex);
                !status) {
                return status;
            }
            preserveViewportSelectionOnHierarchyPublish_ = true;
        }
        authoringFeedback_ = viewportSelectedEntityCount_ == 0U
                                 ? "Viewport selection cleared"
                                 : "Viewport marquee selection published";
        return refreshAuthoringUi(tree);
    }

    [[nodiscard]] float viewportWorldHeight() const noexcept
    {
        return PreviewWorldHeight * 100.0F / viewportZoomPercent_;
    }

    [[nodiscard]] float viewportWorldWidth() const noexcept
    {
        if (viewportLogicalRect_.width > 0.0F && viewportLogicalRect_.height > 0.0F) {
            return viewportWorldHeight() * viewportLogicalRect_.width /
                   viewportLogicalRect_.height;
        }
        return PreviewWorldWidth * 100.0F / viewportZoomPercent_;
    }

    [[nodiscard]] static UI::UIStraightSrgba8Color viewportGridColor(
        Tina::Editor::EditorViewportGridSegmentKind kind) noexcept
    {
        using Kind = Tina::Editor::EditorViewportGridSegmentKind;
        switch (kind) {
        case Kind::Major:
            return UI::rgb(0x9AA8B8, 116);
        case Kind::AxisX:
            return UI::rgb(0xE16060, 225);
        case Kind::AxisY:
            return UI::rgb(0x52BE7A, 225);
        case Kind::AxisZ:
            return UI::rgb(0x5894EA, 225);
        case Kind::Minor:
        default:
            return UI::rgb(0x748397, 66);
        }
    }

    [[nodiscard]] static UI::UILayoutStyle viewportGridLayout(
        const Tina::Editor::EditorViewportGridSegment& segment,
        Tina::Core::usize partIndex,
        bool diagonal) noexcept
    {
        using Kind = Tina::Editor::EditorViewportGridSegmentKind;
        const bool axis = segment.kind == Kind::AxisX ||
                          segment.kind == Kind::AxisY ||
                          segment.kind == Kind::AxisZ;
        const float thickness = axis ? 2.0F
                                     : (segment.kind == Kind::Major ? 1.25F : 1.0F);
        float startX = segment.startX;
        float startY = segment.startY;
        float endX = segment.endX;
        float endY = segment.endY;
        if (diagonal) {
            const Tina::Core::usize stepIndex =
                partIndex / ViewportGridDiagonalPartsPerStep;
            const bool verticalPart =
                partIndex % ViewportGridDiagonalPartsPerStep != 0U;
            const float startT = static_cast<float>(stepIndex) /
                                 static_cast<float>(ViewportGridDiagonalSteps);
            const float endT = static_cast<float>(stepIndex + 1U) /
                               static_cast<float>(ViewportGridDiagonalSteps);
            startX = segment.startX +
                     (segment.endX - segment.startX) * startT;
            startY = segment.startY +
                     (segment.endY - segment.startY) * startT;
            endX = segment.startX +
                   (segment.endX - segment.startX) * endT;
            endY = segment.startY +
                   (segment.endY - segment.startY) * endT;
            if (verticalPart) {
                startX = endX;
            } else {
                endY = startY;
            }
        }
        const float width = std::abs(endX - startX);
        const float height = std::abs(endY - startY);
        UI::UILayoutStyle style{};
        style.placement = UI::UILayoutPlacement::Overlay;
        style.overlay.horizontal = UI::UIAxisAlignment::Start;
        style.overlay.vertical = UI::UIAxisAlignment::Start;
        if (height > width) {
            style.overlay.offset.x = UI::UILayoutLength::Percent(
                (startX + endX) * 50.0F);
            style.overlay.offset.y = UI::UILayoutLength::Percent(
                (std::min)(startY, endY) * 100.0F);
            style.size.width = UI::UILayoutLength::Px(thickness);
            style.size.height = UI::UILayoutLength::Percent(height * 100.0F);
        } else {
            style.overlay.offset.x = UI::UILayoutLength::Percent(
                (std::min)(startX, endX) * 100.0F);
            style.overlay.offset.y = UI::UILayoutLength::Percent(
                (startY + endY) * 50.0F);
            style.size.width = UI::UILayoutLength::Percent(width * 100.0F);
            style.size.height = UI::UILayoutLength::Px(thickness);
        }
        return style;
    }

    [[nodiscard]] Tina::Core::Status
    updateViewportGrid(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (!viewportNavigation_.has_value()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "editor viewport grid has no navigation state");
        }
        const auto& twoD = viewportNavigation_->twoD();
        const auto& threeD = viewportNavigation_->threeD();
        Tina::Editor::EditorViewportGridConfig gridConfig{
            .projection = workspaceMode_ == WorkspaceMode::World2D
                              ? Tina::Editor::EditorViewportGridProjection::Orthographic2D
                              : Tina::Editor::EditorViewportGridProjection::Perspective3D,
            .logicalWidth = viewportLogicalRect_.width,
            .logicalHeight = viewportLogicalRect_.height,
            .zoomPercent = viewportZoomPercent_,
            .cameraCenterX = twoD.center.x,
            .cameraCenterY = twoD.center.y,
            .cameraTargetX = threeD.target.x,
            .cameraTargetY = threeD.target.y,
            .cameraTargetZ = threeD.target.z,
            .cameraYawRadians = threeD.yawRadians,
            .cameraPitchRadians = threeD.pitchRadians,
            .cameraDistance = threeD.distance,
            .verticalFovDegrees = 55.0F,
        };
        auto updated = viewportGrid_.update(gridConfig);
        if (!updated) {
            return Tina::Core::failure(std::move(updated.error()));
        }
        if (!*updated) {
            return Tina::Core::success();
        }

        const std::span segments = viewportGrid_.segments();
        Tina::Core::usize visualNodeCount = 0;
        for (const auto& segment : segments) {
            const bool diagonal =
                std::abs(segment.endX - segment.startX) > 1.0e-5F &&
                std::abs(segment.endY - segment.startY) > 1.0e-5F;
            const Tina::Core::usize partCount =
                diagonal ? ViewportGridDiagonalSteps *
                               ViewportGridDiagonalPartsPerStep
                         : Tina::Core::usize{1};
            for (Tina::Core::usize part = 0; part < partCount; ++part) {
                if (visualNodeCount == viewportGridNodes_.size()) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                        "editor viewport grid exceeded its fixed visual node capacity");
                }
                const UI::UINodeId node = viewportGridNodes_[visualNodeCount++];
                if (auto status = tree.setLayoutStyle(
                        node, viewportGridLayout(segment, part, diagonal));
                    !status) {
                    return status;
                }
                if (auto status = tree.setBoxPaint(
                        node,
                        UI::makeSolidBox(viewportGridColor(segment.kind)));
                    !status) {
                    return status;
                }
            }
        }
        for (Tina::Core::usize index = visualNodeCount;
             index < viewportGridVisibleNodeCount_; ++index) {
            UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
            collapsed.placement = UI::UILayoutPlacement::Overlay;
            collapsed.visibility = UI::UIVisibility::Collapsed;
            if (auto status = tree.setLayoutStyle(viewportGridNodes_[index], collapsed);
                !status) {
                return status;
            }
        }
        viewportGridVisibleNodeCount_ = visualNodeCount;

        const auto& stats = viewportGrid_.stats();
        counters_.viewportGridRevision = stats.revision;
        counters_.viewportGridSegments = stats.segmentCount;
        counters_.viewportGridMinorLines = stats.minorSegmentCount;
        counters_.viewportGridMajorLines = stats.majorSegmentCount;
        counters_.viewportGridAxisLines = stats.axisSegmentCount;
        counters_.viewportZoomPercent = viewportZoomPercent_;
        counters_.viewportGridReady = !segments.empty();
        if (workspaceMode_ == WorkspaceMode::World2D) {
            counters_.viewportGrid2DObserved = true;
        } else {
            counters_.viewportGrid3DObserved = true;
        }

        std::string gridLabel = workspaceMode_ == WorkspaceMode::World2D
                                    ? "2D Grid 1 m | "
                                    : "3D Ground Grid | ";
        gridLabel += std::to_string(
            static_cast<int>(std::lround(viewportZoomPercent_)));
        gridLabel += '%';
        if (auto status = tree.setText(gridStatus_, gridLabel); !status) {
            return status;
        }
        std::string zoomLabel = std::to_string(
            static_cast<int>(std::lround(viewportZoomPercent_)));
        zoomLabel += '%';
        return tree.setText(zoomValue_, zoomLabel);
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

    [[nodiscard]] Tina::Core::Status refreshProjectAssetUi(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        std::string count = std::to_string(projectAssets_.visibleItemCount());
        count += " / ";
        count += std::to_string(projectAssets_.itemCount());
        if (auto status = tree.setText(projectAssetCount_, count); !status) {
            return status;
        }
        if (auto status = tree.setText(
                projectAssetSource_,
                assetResources_.projectCatalogConfigured ? "Project" : "Preview");
            !status) {
            return status;
        }
        const u32 activeFilter = static_cast<u32>(projectAssets_.filter());
        for (u32 index = 0; index < projectFilterButtons_.size(); ++index) {
            if (auto status = tree.setEnabled(projectFilterButtons_[index],
                                              index != activeFilter);
                !status) {
                return status;
            }
        }
        if (auto status = tree.setEnabled(openProjectAssetButton_,
                                          projectAssets_.selectedItem() != nullptr);
            !status) {
            return status;
        }
        const bool sourceImportIdle =
            sourceImportService_.state() ==
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle;
        if (auto status = tree.setEnabled(refreshProjectCatalogButton_,
                                          assetResources_.projectCatalogConfigured &&
                                              !catalogRefreshPending_ &&
                                              !pendingProjectSwitch_.has_value() &&
                                              sourceImportIdle);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(createProjectButton_,
                                          !pendingProjectSwitch_.has_value() &&
                                              sourceImportIdle);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(openProjectButton_,
                                          !pendingProjectSwitch_.has_value() &&
                                              sourceImportIdle);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(importSourceButton_,
                                          activeProjectWorkspace_.has_value() &&
                                              !pendingProjectSwitch_.has_value() &&
                                              !catalogRefreshPending_ && sourceImportIdle);
            !status) {
            return status;
        }
        counters_.projectAssetVisibleItems = projectAssets_.visibleItemCount();
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status reportAuthoringFailure(
        std::string_view prefix, const Tina::Core::Error& error)
    {
        try {
            authoringFeedback_.assign(prefix);
            authoringFeedback_ += error.message;
            return Tina::Core::success();
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor authoring failure feedback allocation failed");
        }
    }

    [[nodiscard]] Tina::Core::Result<Tina::Editor::ProjectAssetBrowserModel>
    prepareProjectBrowserForSnapshot(
        const Tina::Asset::CatalogSnapshot& catalog,
        Tina::Editor::ProjectAssetFilter filter,
        std::optional<Tina::Core::AssetId> selectedAsset)
    {
        auto browser = createProjectAssetBrowser(catalog);
        if (!browser) {
            return Tina::Core::failure(std::move(browser.error()));
        }
        if (auto status = browser->setFilter(filter); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        bool restoredSelection = false;
        if (selectedAsset.has_value()) {
            restoredSelection = static_cast<bool>(browser->selectAsset(*selectedAsset));
        }
        if (!restoredSelection && browser->visibleItemCount() != 0U) {
            if (auto status = browser->selectVisibleIndex(0U); !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
        }
        return std::move(*browser);
    }

    [[nodiscard]] Tina::Core::Result<Tina::Editor::EditorDocumentTabs>
    prepareProjectSwitchDocumentTabs()
    {
        if (auto status = synchronizeActiveTabDirty(); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }

        std::vector<Tina::Editor::EditorDocumentTabDesc> retainedTabs;
        try {
            retainedTabs.reserve(documentTabs_.tabCount());
            for (Tina::Core::usize index = 0; index < documentTabs_.tabCount(); ++index) {
                const auto* tab = documentTabs_.tab(index);
                if (tab == nullptr) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::Internal,
                        "Editor document tab disappeared during project switch staging");
                }
                if (tab->key.assetId && tab->dirty) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::DirtyDocumentRequiresConfirmation,
                        "Save or discard modified Catalog documents before switching projects");
                }
                if (tab->pinned || !tab->key.assetId) {
                    retainedTabs.push_back(*tab);
                }
            }
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor project switch tab staging allocation failed");
        }

        auto candidate = Tina::Editor::EditorDocumentTabs::Create(
            retainedTabs, documentTabs_.config());
        if (!candidate) {
            return Tina::Core::failure(std::move(candidate.error()));
        }
        const Tina::Editor::EditorDocumentKey fallbackKey{
            .kind = workspaceMode_ == WorkspaceMode::World2D
                        ? Tina::Editor::EditorDocumentKind::World2D
                        : Tina::Editor::EditorDocumentKind::World3D,
        };
        const auto fallback = candidate->find(fallbackKey);
        if (!fallback.has_value()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Editor project switch has no workspace document fallback");
        }
        if (auto status = candidate->activate(*fallback); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
        return std::move(*candidate);
    }

    [[nodiscard]] Tina::Core::Status switchCatalogAuthoringOwnersToPinnedTabs()
    {
        constexpr std::array catalogDocumentKinds{
            Tina::Editor::EditorDocumentKind::World3D,
            Tina::Editor::EditorDocumentKind::TileMap2D,
            Tina::Editor::EditorDocumentKind::SpriteAnimation2D,
        };
        for (const auto kind : catalogDocumentKinds) {
            auto* owner = activeAuthoringDocumentOwner(kind);
            if (owner == nullptr) {
                continue;
            }
            const Tina::Editor::EditorDocumentTabDesc* pinned = nullptr;
            for (Tina::Core::usize index = 0; index < documentTabs_.tabCount(); ++index) {
                const auto* candidate = documentTabs_.tab(index);
                if (candidate != nullptr && candidate->pinned &&
                    candidate->key.kind == kind) {
                    pinned = candidate;
                    break;
                }
            }
            if (pinned == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Editor project switch has no pinned authoring document owner");
            }
            if (*owner != pinned->key) {
                if (auto status = switchActiveAuthoringDocument(pinned->key); !status) {
                    return status;
                }
            }
        }
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status refreshPinnedCatalogAuthoringDocuments(
        const Tina::Editor::ProjectAssetBrowserModel& browser)
    {
        constexpr std::array reloadableKinds{
            Tina::Editor::EditorDocumentKind::TileMap2D,
            Tina::Editor::EditorDocumentKind::SpriteAnimation2D,
        };
        for (const auto kind : reloadableKinds) {
            const Tina::Editor::EditorDocumentTabDesc* pinned = nullptr;
            for (Tina::Core::usize index = 0; index < documentTabs_.tabCount(); ++index) {
                const auto* candidate = documentTabs_.tab(index);
                if (candidate != nullptr && candidate->pinned &&
                    candidate->key.kind == kind) {
                    pinned = candidate;
                    break;
                }
            }
            if (pinned == nullptr || !pinned->key.assetId) {
                continue;
            }
            const auto* asset = browser.inspectorSnapshot(pinned->key.assetId);
            if (asset == nullptr ||
                Tina::Editor::editorDocumentKindForAsset(asset->assetKind) != kind) {
                continue;
            }
            auto loaded = loadProjectAssetDocument(*asset);
            if (!loaded) {
                return Tina::Core::failure(std::move(loaded.error()));
            }
            if (!loaded->document.has_value()) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Pinned Catalog authoring asset did not produce a document");
            }
            if (kind == Tina::Editor::EditorDocumentKind::TileMap2D) {
                auto* document = std::get_if<Tina::Editor::TileMapAuthoringDocument>(
                    &*loaded->document);
                if (document == nullptr) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::Internal,
                        "Pinned TileMap Catalog asset produced the wrong document kind");
                }
                tileMapDocument_ = std::move(*document);
            } else {
                auto* document =
                    std::get_if<Tina::Editor::SpriteAnimationAuthoringDocument>(
                        &*loaded->document);
                if (document == nullptr) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::Internal,
                        "Pinned animation Catalog asset produced the wrong document kind");
                }
                spriteAnimationDocument_ = std::move(*document);
            }
        }
        return Tina::Core::success();
    }

    void commitProjectSwitchDocumentTabs(
        Tina::Editor::EditorDocumentTabs candidateTabs) noexcept
    {
        documentTabs_ = std::move(candidateTabs);
        for (auto& slot : suspendedAuthoringDocuments_) {
            slot.reset();
        }
        for (auto& slot : tabDocumentSessions_) {
            slot.reset();
        }
        pendingDirtyCloseKey_.reset();
        assetInspectorActive_ = false;
        synchronizeViewportSelectionFromHierarchy();
        counters_.documentTabCount = documentTabs_.tabCount();
    }

    [[nodiscard]] Tina::Core::Status rebuildLiveCatalogPreview(
        std::string successFeedback)
    {
        counters_.runtimePreviewValid = false;
        counters_.catalogReady = false;
        animationAnimator_.reset();
        releasePreviewAssetBindings();
        if (auto status = preparePreviewAssetBindings(); !status) {
            auto error = std::move(status.error());
            releasePreviewAssetBindings();
            if (auto feedback = reportAuthoringFailure(
                    "Catalog committed, but runtime preview binding rebuild failed: ",
                    error);
                !feedback) {
                return feedback;
            }
            return Tina::Core::failure(std::move(error));
        }
        if (auto status = rebuildAnimationAnimator(); !status) {
            auto error = std::move(status.error());
            releasePreviewAssetBindings();
            counters_.runtimePreviewValid = false;
            if (auto feedback = reportAuthoringFailure(
                    "Catalog committed, but animation preview rebuild failed: ",
                    error);
                !feedback) {
                return feedback;
            }
            return Tina::Core::failure(std::move(error));
        }
        if (auto status = validateRuntimePreview(); !status) {
            auto error = std::move(status.error());
            releasePreviewAssetBindings();
            counters_.runtimePreviewValid = false;
            if (auto feedback = reportAuthoringFailure(
                    "Catalog committed, but runtime preview validation failed: ",
                    error);
                !feedback) {
                return feedback;
            }
            return Tina::Core::failure(std::move(error));
        }
        ++counters_.previewAssetBindingRefreshes;
        authoringFeedback_.swap(successFeedback);
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status switchLiveProjectCatalog(
        Tina::Editor::EditorProjectWorkspace workspace)
    {
        if (!assetResources_.system.has_value()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Project switch requires the Editor AssetSystem");
        }

        auto candidateTabs = prepareProjectSwitchDocumentTabs();
        if (!candidateTabs) {
            if (candidateTabs.error().code ==
                Tina::Editor::EditorErrorCode::DirtyDocumentRequiresConfirmation) {
                return reportAuthoringFailure(
                    "Project switch blocked; previous Catalog preserved: ",
                    candidateTabs.error());
            }
            return Tina::Core::failure(std::move(candidateTabs.error()));
        }

        std::optional<Tina::Core::AssetId> previousSelection{};
        if (const auto* selected = projectAssets_.selectedItem(); selected != nullptr) {
            previousSelection = selected->assetId;
        }
        const Tina::Editor::ProjectAssetFilter previousFilter = projectAssets_.filter();

        auto resolvedCatalog = resolveProjectCatalog(workspace);
        if (!resolvedCatalog) {
            return reportAuthoringFailure(
                "Project switch could not resolve its active Catalog: ",
                resolvedCatalog.error());
        }
        std::string nextCatalogRoot;
        std::string nextSourceImportStatePath;
        std::string successFeedback;
        try {
            nextCatalogRoot = resolvedCatalog->catalogRootUtf8;
            nextSourceImportStatePath = resolvedCatalog->sourceImportStatePathUtf8;
            successFeedback = "Project switched: ";
            successFeedback += workspace.projectRootUtf8();
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Project switch path staging allocation failed");
        }

        Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
            spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
        Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
            mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
        Tina::Asset::CatalogReloadConfig reloadConfig{};
        reloadConfig.package.manifest.catalog.maxEntries = 1024;
        reloadConfig.package.manifest.catalog.maxDependencies = 4096;
        reloadConfig.package.manifest.catalog.maxDependenciesPerAsset = 64;
        reloadConfig.package.validation.verifyTypedPayload = true;
        if (spriteParticipant != nullptr) {
            reloadConfig.bindings.sprite2D =
                std::span<Tina::Asset::Sprite2DBindingRegistry*>{&spriteParticipant, 1U};
        }
        if (meshParticipant != nullptr) {
            reloadConfig.bindings.mesh3D =
                std::span<Tina::Asset::Mesh3DBindingRegistry*>{&meshParticipant, 1U};
        }
        auto reload = assetResources_.system->reloadCatalog(
            resolvedCatalog->catalogRootUtf8, reloadConfig);
        if (!reload) {
            return reportAuthoringFailure(
                "Project switch failed; previous Catalog preserved: ", reload.error());
        }

        const Tina::Asset::CatalogSnapshot* committedCatalog =
            assetResources_.system->catalog();
        if (committedCatalog == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Project switch committed without an AssetSystem Catalog snapshot");
        }
        auto candidateBrowser = prepareProjectBrowserForSnapshot(
            *committedCatalog, previousFilter, previousSelection);
        if (!candidateBrowser) {
            auto error = std::move(candidateBrowser.error());
            if (auto feedback = reportAuthoringFailure(
                    "Project Catalog committed, but Browser rebuild failed: ", error);
                !feedback) {
                return feedback;
            }
            return Tina::Core::failure(std::move(error));
        }
        if (auto status = switchCatalogAuthoringOwnersToPinnedTabs(); !status) {
            return status;
        }
        if (auto status = refreshPinnedCatalogAuthoringDocuments(*candidateBrowser);
            !status) {
            return status;
        }

        assetResources_.catalogRootUtf8.swap(nextCatalogRoot);
        assetResources_.sourceImportStatePathUtf8.swap(nextSourceImportStatePath);
        assetResources_.catalogEntryCount =
            static_cast<u32>(candidateBrowser->itemCount());
        assetResources_.projectCatalogConfigured = true;
        assetResources_.builtInPreviewCatalog = false;
        counters_.catalogEntryCount = assetResources_.catalogEntryCount;
        counters_.projectCatalogConfigured = true;
        counters_.builtInPreviewCatalog = false;
        if (auto status = rebuildLiveCatalogPreview(std::move(successFeedback)); !status) {
            return status;
        }

        projectAssets_ = std::move(*candidateBrowser);
        commitProjectSwitchDocumentTabs(std::move(*candidateTabs));
        activeProjectWorkspace_ = std::move(workspace);
        sourceImportUnits_ = std::move(resolvedCatalog->sourceImportUnits);
        counters_.sourceImportIntendedUnits = sourceImportUnits_.size();
        sourceImportPointerPathUtf8_.clear();
        sourceImportPendingStageRootUtf8_.clear();
        sourceImportSupersededCatalogRootUtf8_.clear();
        sourceImportCatalogCommitted_ = false;
        ++counters_.projectSwitches;
        observedProjectAssetSelectionIndex_.reset();
        previewAssetBindingsRefreshPending_ = false;
        projectBrowserUiRefreshPending_ = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status createNewProjectFromDialog()
    {
        auto location = makeSaveDialogLocation(
            assetResources_.catalogRootUtf8, {}, true);
        if (!location) {
            return Tina::Core::failure(std::move(location.error()));
        }
        auto selected = fileDialog_.pickFolder({
            .titleUtf8 = "Create Tina Project in Empty Folder",
            .initialDirectoryUtf8 = location->initialDirectoryUtf8,
        });
        if (!selected) {
            if (selected.error().code == Tina::Core::CoreErrorCode::Unsupported) {
                authoringFeedback_ =
                    "Native project folder selection is unavailable on this platform";
                return Tina::Core::success();
            }
            return Tina::Core::failure(std::move(selected.error()));
        }
        if (!selected->selected()) {
            authoringFeedback_ = "New project creation cancelled";
            return Tina::Core::success();
        }

        auto workspace = Tina::Editor::CreateNewEditorProject({
            .projectRootUtf8 = selected->selectedPathUtf8,
            .targetPlatform = editorTargetPlatform(),
        });
        if (!workspace) {
            try {
                authoringFeedback_ = "Project creation failed: ";
                authoringFeedback_ += workspace.error().message;
                return Tina::Core::success();
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Project creation failure message allocation failed");
            }
        }

        auto emptyManifest = Tina::AssetFormat::writeCookedManifestBytes({
            .targetPlatform = workspace->targetPlatform(),
        });
        if (!emptyManifest) {
            return Tina::Core::failure(std::move(emptyManifest.error()));
        }
        if (auto status = Tina::Asset::publishCatalogPackage(
                workspace->cookedCatalogRootUtf8(),
                Tina::Asset::DefaultCatalogManifestRelativePath,
                *emptyManifest, {}, {.writeObjects = false});
            !status) {
            try {
                authoringFeedback_ =
                    "Project directories created, but empty Catalog initialization failed: ";
                authoringFeedback_ += status.error().message;
                return Tina::Core::success();
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Project Catalog initialization failure message allocation failed");
            }
        }
        std::pmr::unsynchronized_pool_resource validationMemory;
        Tina::Asset::CatalogPackageOpenConfig openConfig{};
        openConfig.manifest.catalog = {
            .maxEntries = 1,
            .maxDependencies = 0,
            .maxDependenciesPerAsset = 0,
            .memoryResource = &validationMemory,
        };
        openConfig.validation.verifyTypedPayload = true;
        auto validatedCatalog = Tina::Asset::openCatalogPackage(
            workspace->cookedCatalogRootUtf8(), openConfig);
        if (!validatedCatalog) {
            try {
                authoringFeedback_ =
                    "Project directories created, but empty Catalog validation failed: ";
                authoringFeedback_ += validatedCatalog.error().message;
                return Tina::Core::success();
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Project Catalog validation failure message allocation failed");
            }
        }

        try {
            std::string feedback =
                "Project created; live Catalog switch scheduled: ";
            feedback += workspace->projectRootUtf8();
            pendingProjectSwitch_ = std::move(*workspace);
            authoringFeedback_.swap(feedback);
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Project creation success message allocation failed");
        }
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status openProjectFromDialog()
    {
        const std::string_view currentRoot = activeProjectWorkspace_.has_value()
                                                 ? activeProjectWorkspace_->projectRootUtf8()
                                                 : std::string_view{assetResources_.catalogRootUtf8};
        auto location = makeSaveDialogLocation(currentRoot, {}, true);
        if (!location) {
            return Tina::Core::failure(std::move(location.error()));
        }
        auto selected = fileDialog_.pickFolder({
            .titleUtf8 = "Open Tina Project",
            .initialDirectoryUtf8 = location->initialDirectoryUtf8,
        });
        if (!selected) {
            if (selected.error().code == Tina::Core::CoreErrorCode::Unsupported) {
                authoringFeedback_ =
                    "Native project folder selection is unavailable on this platform";
                return Tina::Core::success();
            }
            return Tina::Core::failure(std::move(selected.error()));
        }
        if (!selected->selected()) {
            authoringFeedback_ = "Open project cancelled";
            return Tina::Core::success();
        }

        auto workspace = openExistingEditorProjectWorkspace(selected->selectedPathUtf8);
        if (!workspace) {
            return reportAuthoringFailure("Project open failed: ", workspace.error());
        }
        try {
            std::string feedback = "Project open scheduled: ";
            feedback += workspace->projectRootUtf8();
            pendingProjectSwitch_ = std::move(*workspace);
            authoringFeedback_.swap(feedback);
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Project open success feedback allocation failed");
        }
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status startSourceImport()
    {
        if (!activeProjectWorkspace_.has_value()) {
            authoringFeedback_ = "Source import requires an open Tina project";
            return Tina::Core::success();
        }
        if (sourceImportUnits_.empty()) {
            authoringFeedback_ = "Source import requires at least one recipe or glTF unit";
            return Tina::Core::success();
        }
        if (sourceImportService_.state() !=
            Tina::EditorApp::Detail::EditorSourceImportServiceState::Idle) {
            authoringFeedback_ = "Source import is already running or awaiting Catalog commit";
            return Tina::Core::success();
        }

        auto stagePaths = createSourceImportStagePaths(*activeProjectWorkspace_);
        if (!stagePaths) {
            return Tina::Core::failure(std::move(stagePaths.error()));
        }
        auto stageReservation = Tina::Core::makeScopeExit([this, &stagePaths]() noexcept {
            cleanupOwnedSourceImportStage(stagePaths->catalogRootUtf8);
            sourceImportPendingStageRootUtf8_.clear();
            sourceImportPointerPathUtf8_.clear();
        });
        auto cache = ensureSourceImportCache(*activeProjectWorkspace_);
        if (!cache) {
            return Tina::Core::failure(std::move(cache.error()));
        }

        Tina::EditorApp::Detail::EditorSourceImportRequest request{};
        try {
            request.sourceRootUtf8.assign(activeProjectWorkspace_->sourceRootUtf8());
            request.baselineCatalogRootUtf8 = assetResources_.catalogRootUtf8;
            request.baselineStatePathUtf8 = assetResources_.sourceImportStatePathUtf8;
            request.freshStageRootUtf8 = stagePaths->catalogRootUtf8;
            request.freshStageStatePathUtf8 = stagePaths->statePathUtf8;
            request.targetPlatform = activeProjectWorkspace_->targetPlatform();
            request.units = sourceImportUnits_;
            sourceImportPointerPathUtf8_ = pathToUtf8(cache->activeCatalogPointer);
            sourceImportPendingStageRootUtf8_ = request.freshStageRootUtf8;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor could not retain the source-import request");
        }

        if (auto status = sourceImportService_.start(std::move(request)); !status) {
            return status;
        }
        stageReservation.release();
        sourceImportCatalogCommitted_ = false;
        counters_.sourceImportRunning = true;
        counters_.sourceImportReady = false;
        counters_.sourceImportStateCommitted = false;
        ++counters_.sourceImportStarts;
        authoringFeedback_ = "Source import is cooking a fully validated fresh stage";
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status importSourceFromDialog()
    {
        if (!activeProjectWorkspace_.has_value()) {
            authoringFeedback_ = "Open or create a project before importing source assets";
            return Tina::Core::success();
        }
        constexpr std::array filters{
            Tina::EditorApp::Detail::EditorFileDialogFilter{
                .labelUtf8 = "Tina recipe or glTF",
                .patternUtf8 = "*.recipe;*.gltf;*.glb",
            },
            Tina::EditorApp::Detail::EditorFileDialogFilter{
                .labelUtf8 = "All files",
                .patternUtf8 = "*.*",
            },
        };
        auto selected = fileDialog_.openExistingFile({
            .titleUtf8 = "Import Source into Tina Project",
            .initialDirectoryUtf8 = activeProjectWorkspace_->sourceRootUtf8(),
            .filters = filters,
        });
        if (!selected) {
            if (selected.error().code == Tina::Core::CoreErrorCode::Unsupported) {
                authoringFeedback_ =
                    "Native source import selection is unavailable on this platform";
                return Tina::Core::success();
            }
            return Tina::Core::failure(std::move(selected.error()));
        }
        if (!selected->selected()) {
            authoringFeedback_ = "Source import cancelled";
            return Tina::Core::success();
        }

        std::string extension;
        try {
            extension = std::filesystem::u8path(selected->selectedPathUtf8)
                            .extension()
                            .string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                           });
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Editor source import extension allocation failed");
        }
        Tina::EditorApp::Detail::EditorSourceImportUnitKind kind{};
        if (extension == ".recipe") {
            kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::CatalogRecipe;
        } else if (extension == ".gltf" || extension == ".glb") {
            kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::Gltf;
        } else {
            authoringFeedback_ = "Source import supports .recipe, .gltf, and .glb files";
            return Tina::Core::success();
        }

        const bool alreadyIntended = std::any_of(
            sourceImportUnits_.begin(), sourceImportUnits_.end(),
            [&](const auto& unit) {
                return unit.kind == kind &&
                       unit.sourcePathUtf8 == selected->selectedPathUtf8;
            });
        if (!alreadyIntended) {
            if (sourceImportUnits_.size() >=
                Tina::AssetFormat::SourceImportWire::MaxUnits) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::CapacityExceeded,
                    "Editor source import intended unit capacity exceeded");
            }
            try {
                sourceImportUnits_.push_back({
                    .kind = kind,
                    .sourcePathUtf8 = std::move(selected->selectedPathUtf8),
                });
                counters_.sourceImportIntendedUnits = sourceImportUnits_.size();
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Editor could not retain the selected source import unit");
            }
        }
        return startSourceImport();
    }

    void cleanupOwnedSourceImportStage(std::string_view catalogRootUtf8) noexcept
    {
        if (catalogRootUtf8.empty() || !activeProjectWorkspace_.has_value()) {
            return;
        }
        try {
            const auto cache = sourceImportCachePaths(*activeProjectWorkspace_);
            const auto catalogRoot = std::filesystem::u8path(
                catalogRootUtf8.begin(), catalogRootUtf8.end());
            const auto stageRoot = catalogRoot.parent_path().lexically_normal();
            if (stageRoot.empty()) {
                return;
            }
            if (!validatePhysicalProjectDirectory(cache.stages, "sourceImportStages") ||
                !validatePhysicalProjectDirectory(stageRoot, "sourceImportStage")) {
                return;
            }
            std::error_code canonicalError;
            const auto projectRoot = std::filesystem::u8path(
                activeProjectWorkspace_->projectRootUtf8().begin(),
                activeProjectWorkspace_->projectRootUtf8().end());
            const auto physicalProject =
                std::filesystem::weakly_canonical(projectRoot, canonicalError);
            if (canonicalError) {
                return;
            }
            const auto physicalStages =
                std::filesystem::weakly_canonical(cache.stages, canonicalError);
            if (canonicalError) {
                return;
            }
            const auto physicalStage =
                std::filesystem::weakly_canonical(stageRoot, canonicalError);
            if (canonicalError ||
                !pathIsSameOrDescendant(physicalStages, physicalProject) ||
                !pathsReferToSameLocation(physicalStage.parent_path(), physicalStages)) {
                return;
            }
            std::error_code cleanupError;
            (void)std::filesystem::remove_all(stageRoot, cleanupError);
        } catch (...) {
        }
    }

    void cleanupFailedSourceImportStage() noexcept
    {
        cleanupOwnedSourceImportStage(sourceImportPendingStageRootUtf8_);
        sourceImportPendingStageRootUtf8_.clear();
    }

    [[nodiscard]] Tina::Core::Status publishCommittedSourceImportState(
        const Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready)
    {
        if (!ready.stageCreated) {
            return Tina::Core::success();
        }
        try {
            std::error_code stateError;
            const auto stateStatus = std::filesystem::symlink_status(
                std::filesystem::u8path(ready.statePathUtf8.begin(), ready.statePathUtf8.end()),
                stateError);
            if (stateError || !std::filesystem::is_regular_file(stateStatus)) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::NotFound,
                    "Imported Catalog stage state disappeared before commit");
            }
        } catch (const std::filesystem::filesystem_error& exception) {
            Tina::Core::Error error{
                Tina::Core::CoreErrorCode::Io,
                "Editor could not inspect the imported Catalog stage state"};
            error.setNativeCode(exception.code().value());
            return Tina::Core::failure(std::move(error));
        }
        const auto pointerBytes = std::as_bytes(std::span{
            ready.stageRootUtf8.data(), ready.stageRootUtf8.size()});
        if (auto status = Tina::Core::writeFile(
                sourceImportPointerPathUtf8_, pointerBytes);
            !status) {
            return status;
        }
        cleanupOwnedSourceImportStage(sourceImportSupersededCatalogRootUtf8_);
        sourceImportSupersededCatalogRootUtf8_.clear();
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status commitSourceImportCatalog(
        const Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready)
    {
        if (!ready.stageCreated) {
            return Tina::Core::success();
        }
        auto candidateTabs = prepareProjectSwitchDocumentTabs();
        if (!candidateTabs) {
            if (candidateTabs.error().code ==
                Tina::Editor::EditorErrorCode::DirtyDocumentRequiresConfirmation) {
                return reportAuthoringFailure(
                    "Source import ready; save or discard modified Catalog documents: ",
                    candidateTabs.error());
            }
            return Tina::Core::failure(std::move(candidateTabs.error()));
        }

        const auto previousFilter = projectAssets_.filter();
        std::optional<Tina::Core::AssetId> previousSelection{};
        if (const auto* selected = projectAssets_.selectedItem(); selected != nullptr) {
            previousSelection = selected->assetId;
        }
        std::string nextCatalogRoot;
        std::string nextStatePath;
        std::string previousCatalogRoot;
        try {
            nextCatalogRoot = ready.stageRootUtf8;
            nextStatePath = ready.statePathUtf8;
            previousCatalogRoot = assetResources_.catalogRootUtf8;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor could not stage imported Catalog ownership");
        }

        Tina::Asset::CatalogPackageOpenConfig candidateConfig{};
        candidateConfig.manifest.catalog.maxEntries = 4096;
        candidateConfig.manifest.catalog.maxDependencies = 16384;
        candidateConfig.manifest.catalog.maxDependenciesPerAsset = 4096;
        candidateConfig.manifest.catalog.memoryResource = &sourceImportMemory_;
        candidateConfig.validation.file.memoryResource = &sourceImportMemory_;
        candidateConfig.validation.verifyTypedPayload = true;
        auto candidateCatalog = Tina::Asset::openCatalogPackage(
            ready.stageRootUtf8, candidateConfig);
        if (!candidateCatalog) {
            return Tina::Core::failure(std::move(candidateCatalog.error()));
        }
        auto browser = prepareProjectBrowserForSnapshot(
            *candidateCatalog, previousFilter, previousSelection);
        if (!browser) {
            return Tina::Core::failure(std::move(browser.error()));
        }

        Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
            spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
        Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
            mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
        Tina::Asset::CatalogReloadConfig reloadConfig{};
        reloadConfig.package.manifest.catalog.maxEntries = 4096;
        reloadConfig.package.manifest.catalog.maxDependencies = 16384;
        reloadConfig.package.manifest.catalog.maxDependenciesPerAsset = 4096;
        reloadConfig.package.validation.verifyTypedPayload = true;
        if (spriteParticipant != nullptr) {
            reloadConfig.bindings.sprite2D =
                std::span<Tina::Asset::Sprite2DBindingRegistry*>{&spriteParticipant, 1U};
        }
        if (meshParticipant != nullptr) {
            reloadConfig.bindings.mesh3D =
                std::span<Tina::Asset::Mesh3DBindingRegistry*>{&meshParticipant, 1U};
        }
        auto reload = assetResources_.system->reloadCatalog(
            ready.stageRootUtf8, reloadConfig);
        if (!reload) {
            if (reload.error().code == Tina::Asset::AssetErrorCode::CatalogReloadBusy) {
                ++counters_.sourceImportBusyRetries;
                authoringFeedback_ = "Source import stage is ready; Catalog reload will retry on "
                                     "the next safe frame";
                return Tina::Core::success();
            }
            auto status = reportAuthoringFailure(
                "Source import reload failed; previous Catalog preserved: ", reload.error());
            if (!status) {
                return status;
            }
            ++counters_.sourceImportFailures;
            counters_.sourceImportRunning = false;
            counters_.sourceImportReady = false;
            if (auto acknowledge = sourceImportService_.acknowledgeReady(); !acknowledge) {
                return acknowledge;
            }
            cleanupFailedSourceImportStage();
            return Tina::Core::success();
        }
        sourceImportCatalogCommitted_ = true;

        const auto* committedCatalog = assetResources_.system->catalog();
        if (committedCatalog == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Source import committed without an AssetSystem Catalog snapshot");
        }
        ++counters_.sourceImportCatalogReloads;
        if (auto status = switchCatalogAuthoringOwnersToPinnedTabs(); !status) {
            return status;
        }
        if (auto status = refreshPinnedCatalogAuthoringDocuments(*browser); !status) {
            return status;
        }
        assetResources_.catalogRootUtf8.swap(nextCatalogRoot);
        assetResources_.sourceImportStatePathUtf8.swap(nextStatePath);
        sourceImportSupersededCatalogRootUtf8_ = std::move(previousCatalogRoot);
        assetResources_.catalogEntryCount = static_cast<u32>(browser->itemCount());
        counters_.catalogEntryCount = assetResources_.catalogEntryCount;
        if (auto status = rebuildLiveCatalogPreview(
                "Source import Catalog, Browser, documents, and previews committed");
            !status) {
            return status;
        }
        projectAssets_ = std::move(*browser);
        commitProjectSwitchDocumentTabs(std::move(*candidateTabs));
        observedProjectAssetSelectionIndex_.reset();
        projectBrowserUiRefreshPending_ = true;
        previewAssetBindingsRefreshPending_ = false;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status updateSourceImport()
    {
        if (sourceImportStartPending_) {
            sourceImportStartPending_ = false;
            if (auto status = startSourceImport(); !status) {
                return status;
            }
        }
        if (auto status = sourceImportService_.poll(); !status) {
            return status;
        }
        using State = Tina::EditorApp::Detail::EditorSourceImportServiceState;
        counters_.sourceImportRunning = sourceImportService_.state() == State::Running;
        if (sourceImportService_.state() == State::Failed) {
            const auto* failure = sourceImportService_.failure();
            if (failure == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Source import service failed without an error");
            }
            if (auto status = reportAuthoringFailure("Source import failed: ", *failure);
                !status) {
                return status;
            }
            ++counters_.sourceImportFailures;
            counters_.sourceImportRunning = false;
            counters_.sourceImportReady = false;
            cleanupFailedSourceImportStage();
            return sourceImportService_.dismissFailure();
        }
        if (sourceImportService_.state() != State::Ready) {
            return Tina::Core::success();
        }

        const auto* ready = sourceImportService_.readyStage();
        if (ready == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Source import service is Ready without a stage");
        }
        counters_.sourceImportRunning = false;
        counters_.sourceImportReady = true;
        if (!sourceImportCatalogCommitted_) {
            if (auto status = commitSourceImportCatalog(*ready); !status) {
                return status;
            }
            if (sourceImportService_.state() != State::Ready ||
                (ready->stageCreated && !sourceImportCatalogCommitted_)) {
                return Tina::Core::success();
            }
        }
        if (auto status = publishCommittedSourceImportState(*ready); !status) {
            return reportAuthoringFailure(
                "Imported Catalog is live; import state commit will retry: ",
                status.error());
        }

        counters_.sourceImportUnitsTotal = ready->statistics.unitsTotal;
        counters_.sourceImportUnitsRecooked = ready->statistics.unitsRecooked;
        counters_.sourceImportUnitsRemoved = ready->statistics.unitsRemoved;
        counters_.sourceImportObjectsReused = ready->statistics.objectsReused;
        counters_.sourceImportObjectsCooked = ready->statistics.objectsCooked;
        counters_.sourceImportStateCommitted = true;
        counters_.sourceImportReady = false;
        ++counters_.sourceImportCompletions;
        try {
            authoringFeedback_ = "Source import complete: ";
            authoringFeedback_ += std::to_string(ready->statistics.unitsRecooked);
            authoringFeedback_ += " recooked unit(s), ";
            authoringFeedback_ += std::to_string(ready->statistics.objectsReused);
            authoringFeedback_ += " reused object(s)";
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor source import completion feedback allocation failed");
        }
        if (!ready->stageCreated) {
            cleanupOwnedSourceImportStage(sourceImportPendingStageRootUtf8_);
        }
        if (auto status = sourceImportService_.acknowledgeReady(); !status) {
            return status;
        }
        sourceImportCatalogCommitted_ = false;
        sourceImportPendingStageRootUtf8_.clear();
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status refreshProjectCatalog()
    {
        if (!assetResources_.projectCatalogConfigured ||
            !assetResources_.system.has_value()) {
            authoringFeedback_ = "Catalog refresh requires a configured project Catalog";
            return Tina::Core::success();
        }

        auto candidateTabs = prepareProjectSwitchDocumentTabs();
        if (!candidateTabs) {
            return reportAuthoringFailure(
                "Catalog refresh blocked; previous Catalog preserved: ",
                candidateTabs.error());
        }

        const Tina::Editor::ProjectAssetFilter previousFilter =
            projectAssets_.filter();
        std::optional<Tina::Core::AssetId> previousSelection{};
        if (const auto* selected = projectAssets_.selectedItem(); selected != nullptr) {
            previousSelection = selected->assetId;
        }

        Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
            spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
        Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
            mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
        Tina::Asset::CatalogReloadConfig reloadConfig{};
        reloadConfig.package.manifest.catalog.maxEntries = 1024;
        reloadConfig.package.manifest.catalog.maxDependencies = 4096;
        reloadConfig.package.manifest.catalog.maxDependenciesPerAsset = 64;
        reloadConfig.package.validation.verifyTypedPayload = true;
        if (spriteParticipant != nullptr) {
            reloadConfig.bindings.sprite2D =
                std::span<Tina::Asset::Sprite2DBindingRegistry*>{&spriteParticipant, 1U};
        }
        if (meshParticipant != nullptr) {
            reloadConfig.bindings.mesh3D =
                std::span<Tina::Asset::Mesh3DBindingRegistry*>{&meshParticipant, 1U};
        }
        auto reload = assetResources_.system->reloadCatalog(
            assetResources_.catalogRootUtf8, reloadConfig);
        if (!reload) {
            return reportAuthoringFailure(
                "Catalog refresh failed; previous Catalog preserved: ", reload.error());
        }

        const Tina::Asset::CatalogSnapshot* committedCatalog =
            assetResources_.system->catalog();
        if (committedCatalog == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Catalog refresh committed without an AssetSystem Catalog snapshot");
        }
        auto refreshedBrowser = prepareProjectBrowserForSnapshot(
            *committedCatalog, previousFilter, previousSelection);
        if (!refreshedBrowser) {
            auto error = std::move(refreshedBrowser.error());
            if (auto feedback = reportAuthoringFailure(
                    "Catalog refresh committed, but Browser rebuild failed: ", error);
                !feedback) {
                return feedback;
            }
            return Tina::Core::failure(std::move(error));
        }
        if (auto status = switchCatalogAuthoringOwnersToPinnedTabs(); !status) {
            return status;
        }
        if (auto status = refreshPinnedCatalogAuthoringDocuments(*refreshedBrowser);
            !status) {
            return status;
        }

        assetResources_.catalogEntryCount =
            static_cast<u32>(refreshedBrowser->itemCount());
        counters_.catalogEntryCount = assetResources_.catalogEntryCount;
        if (auto status = rebuildLiveCatalogPreview(
                "Catalog, Project Browser, documents, and preview bindings refreshed");
            !status) {
            return status;
        }

        projectAssets_ = std::move(*refreshedBrowser);
        commitProjectSwitchDocumentTabs(std::move(*candidateTabs));
        observedProjectAssetSelectionIndex_.reset();
        projectBrowserUiRefreshPending_ = true;
        previewAssetBindingsRefreshPending_ = false;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status refreshDocumentTabsUi(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (auto status = synchronizeActiveTabDirty(); !status) {
            return status;
        }
        for (u32 index = 0; index < documentTabButtons_.size(); ++index) {
            const auto* tab = documentTabs_.tab(index);
            std::string title = tab != nullptr ? tab->title : "Empty";
            if (tab != nullptr && tab->dirty) {
                title += " *";
            }
            if (auto status = tree.setText(documentTabButtons_[index], title); !status) {
                return status;
            }
            if (auto status = tree.setEnabled(
                    documentTabButtons_[index],
                    tab != nullptr && index != documentTabs_.activeIndex());
                !status) {
                return status;
            }
        }
        const auto* active = documentTabs_.activeTab();
        if (auto status = tree.setEnabled(closeDocumentButton_,
                                          active != nullptr && !active->pinned);
            !status) {
            return status;
        }
        if (active != nullptr) {
            if (auto status = tree.setText(toolbarDocument_, active->title); !status) {
                return status;
            }
        }
        counters_.documentTabCount = documentTabs_.tabCount();
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status applyProjectAssetFilter(
        Tina::PrimaryWindowUITreeUpdater& tree,
        Tina::Editor::ProjectAssetFilter filter)
    {
        if (auto status = projectAssets_.setFilter(filter); !status) {
            return status;
        }
        observedProjectAssetSelectionIndex_.reset();
        if (auto status = tree.setListViewDataSource(projectAssetList_,
                                                     projectAssetDataSource());
            !status) {
            return status;
        }
        if (projectAssets_.visibleItemCount() != 0U) {
            if (auto status = tree.setListViewSelectedIndex(projectAssetList_, 0U); !status) {
                return status;
            }
            assetInspectorActive_ = true;
        } else {
            assetInspectorActive_ = false;
        }
        synchronizeViewportSelectionFromHierarchy();
        authoringFeedback_ = "Project Asset Browser filter changed";
        if (auto status = refreshProjectAssetUi(tree); !status) {
            return status;
        }
        return refreshAuthoringUi(tree);
    }

    [[nodiscard]] WorkspaceSessionState*
    findDocumentSession(Tina::Editor::EditorDocumentKey key) noexcept
    {
        if (key == world2DSession_.key) {
            return &world2DSession_;
        }
        if (key == world3DSession_.key) {
            return &world3DSession_;
        }
        for (auto& slot : tabDocumentSessions_) {
            if (slot.has_value() && slot->key == key) {
                return &*slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const WorkspaceSessionState*
    findDocumentSession(Tina::Editor::EditorDocumentKey key) const noexcept
    {
        if (key == world2DSession_.key) {
            return &world2DSession_;
        }
        if (key == world3DSession_.key) {
            return &world3DSession_;
        }
        for (const auto& slot : tabDocumentSessions_) {
            if (slot.has_value() && slot->key == key) {
                return &*slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] Tina::Core::Status
    installDocumentSession(WorkspaceSessionState session)
    {
        if (findDocumentSession(session.key) != nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Editor document already owns a save session");
        }
        for (auto& slot : tabDocumentSessions_) {
            if (!slot.has_value()) {
                slot.emplace(std::move(session));
                return Tina::Core::success();
            }
        }
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabCapacityExceeded,
            "Editor document save session capacity is exhausted");
    }

    void discardDocumentSession(Tina::Editor::EditorDocumentKey key) noexcept
    {
        for (auto& slot : tabDocumentSessions_) {
            if (slot.has_value() && slot->key == key) {
                slot.reset();
                return;
            }
        }
    }

    [[nodiscard]] Tina::Core::Status initializePinnedDocumentSessions()
    {
        if (findDocumentSession(tileMapDocumentOwnerKey_) != nullptr &&
            findDocumentSession(spriteAnimationDocumentOwnerKey_) != nullptr) {
            return Tina::Core::success();
        }
        auto tileMapBaseline = captureSavedBaseline(tileMapDocument_);
        if (!tileMapBaseline) {
            return Tina::Core::failure(std::move(tileMapBaseline.error()));
        }
        auto animationBaseline = captureSavedBaseline(spriteAnimationDocument_);
        if (!animationBaseline) {
            return Tina::Core::failure(std::move(animationBaseline.error()));
        }
        WorkspaceSessionState tileMapSession{
            .key = tileMapDocumentOwnerKey_,
            .savedBaseline = std::move(*tileMapBaseline),
        };
        WorkspaceSessionState animationSession{
            .key = spriteAnimationDocumentOwnerKey_,
            .savedBaseline = std::move(*animationBaseline),
        };
        bool installedTileMapSession = false;
        if (findDocumentSession(tileMapDocumentOwnerKey_) == nullptr) {
            if (auto status = installDocumentSession(std::move(tileMapSession)); !status) {
                return status;
            }
            installedTileMapSession = true;
        }
        if (findDocumentSession(spriteAnimationDocumentOwnerKey_) == nullptr) {
            if (auto status = installDocumentSession(std::move(animationSession)); !status) {
                if (installedTileMapSession) {
                    discardDocumentSession(tileMapDocumentOwnerKey_);
                }
                return status;
            }
        }
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Result<WorkspaceSessionState>
    makeProjectAssetSession(Tina::Editor::EditorDocumentKey key,
                            const TabAuthoringDocument& document,
                            Tina::AssetFormat::TargetPlatform targetPlatform) const
    {
        auto baseline = std::visit(
            [](const auto& activeDocument) {
                return captureSavedBaseline(activeDocument);
            },
            document);
        if (!baseline) {
            return Tina::Core::failure(std::move(baseline.error()));
        }
        return WorkspaceSessionState{
            .key = key,
            .savedBaseline = std::move(*baseline),
            .targetPlatform = targetPlatform,
            .loadedFromPath = true,
        };
    }

    [[nodiscard]] WorkspaceSessionState* activeDocumentSession() noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        return tab != nullptr ? findDocumentSession(tab->key) : nullptr;
    }

    [[nodiscard]] const WorkspaceSessionState* activeDocumentSession() const noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        return tab != nullptr ? findDocumentSession(tab->key) : nullptr;
    }

    [[nodiscard]] bool documentPathOwnedByOtherSession(
        std::string_view path,
        Tina::Editor::EditorDocumentKey activeKey) const noexcept
    {
        const auto ownsPath = [path, activeKey](const WorkspaceSessionState& session) {
            return session.key != activeKey && session.documentPathUtf8 == path;
        };
        if (ownsPath(world2DSession_) || ownsPath(world3DSession_)) {
            return true;
        }
        return std::any_of(tabDocumentSessions_.begin(), tabDocumentSessions_.end(),
                           [&](const auto& slot) {
                               return slot.has_value() && ownsPath(*slot);
                           });
    }

    [[nodiscard]] Tina::Core::Status refreshToolbarPathForActiveTab(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const WorkspaceSessionState* session = activeDocumentSession();
        if (auto status = tree.setText(
                toolbarPath_, session != nullptr ? session->documentPathUtf8
                                                 : std::string_view{});
            !status) {
            return status;
        }
        return tree.setEnabled(toolbarPath_, session != nullptr);
    }

    [[nodiscard]] Tina::Editor::EditorDocumentKey*
    activeAuthoringDocumentOwner(Tina::Editor::EditorDocumentKind kind) noexcept
    {
        switch (kind) {
        case Tina::Editor::EditorDocumentKind::World3D:
            return &world3DDocumentOwnerKey_;
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            return &tileMapDocumentOwnerKey_;
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            return &spriteAnimationDocumentOwnerKey_;
        case Tina::Editor::EditorDocumentKind::World2D:
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return nullptr;
        }
    }

    [[nodiscard]] const SuspendedTabAuthoringDocument*
    findSuspendedAuthoringDocument(Tina::Editor::EditorDocumentKey key) const noexcept
    {
        for (const auto& slot : suspendedAuthoringDocuments_) {
            if (slot.has_value() && slot->key == key) {
                return &*slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] SuspendedTabAuthoringDocument*
    findSuspendedAuthoringDocument(Tina::Editor::EditorDocumentKey key) noexcept
    {
        for (auto& slot : suspendedAuthoringDocuments_) {
            if (slot.has_value() && slot->key == key) {
                return &*slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] Tina::Core::Status
    switchActiveAuthoringDocument(Tina::Editor::EditorDocumentKey key) noexcept
    {
        auto* owner = activeAuthoringDocumentOwner(key.kind);
        if (owner == nullptr || *owner == key) {
            return Tina::Core::success();
        }
        auto* suspended = findSuspendedAuthoringDocument(key);
        if (suspended == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                "Catalog authoring tab has no suspended document state");
        }
        switch (key.kind) {
        case Tina::Editor::EditorDocumentKind::World3D: {
            auto* target = std::get_if<Tina::Editor::World3DAuthoringDocument>(
                &suspended->document);
            if (target == nullptr) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "World3D tab state has the wrong document kind");
            }
            std::swap(document3D_, *target);
            break;
        }
        case Tina::Editor::EditorDocumentKind::TileMap2D: {
            auto* target = std::get_if<Tina::Editor::TileMapAuthoringDocument>(
                &suspended->document);
            if (target == nullptr) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "TileMap tab state has the wrong document kind");
            }
            std::swap(tileMapDocument_, *target);
            break;
        }
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D: {
            auto* target = std::get_if<Tina::Editor::SpriteAnimationAuthoringDocument>(
                &suspended->document);
            if (target == nullptr) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "SpriteAnimation tab state has the wrong document kind");
            }
            std::swap(spriteAnimationDocument_, *target);
            break;
        }
        case Tina::Editor::EditorDocumentKind::World2D:
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return Tina::Core::success();
        }
        std::swap(*owner, suspended->key);
        ++counters_.tabOwnedDocumentSwaps;
        previewAssetBindingsRefreshPending_ = true;
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status
    installNewAuthoringDocument(Tina::Editor::EditorDocumentKey key,
                                TabAuthoringDocument document)
    {
        auto* owner = activeAuthoringDocumentOwner(key.kind);
        if (owner == nullptr || *owner == key) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "Catalog document kind cannot own an authoring tab");
        }
        std::optional<SuspendedTabAuthoringDocument>* emptySlot = nullptr;
        for (auto& slot : suspendedAuthoringDocuments_) {
            if (!slot.has_value()) {
                emptySlot = &slot;
                break;
            }
        }
        if (emptySlot == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabCapacityExceeded,
                "Editor authoring document state capacity is exhausted");
        }
        try {
            emptySlot->emplace(SuspendedTabAuthoringDocument{
                .key = key,
                .document = std::move(document),
            });
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Editor authoring tab state allocation failed");
        }
        if (auto status = switchActiveAuthoringDocument(key); !status) {
            emptySlot->reset();
            return status;
        }
        return Tina::Core::success();
    }

    void discardSuspendedAuthoringDocument(
        Tina::Editor::EditorDocumentKey key) noexcept
    {
        if (auto* slot = findSuspendedAuthoringDocument(key); slot != nullptr) {
            for (auto& candidate : suspendedAuthoringDocuments_) {
                if (candidate.has_value() && &*candidate == slot) {
                    candidate.reset();
                    return;
                }
            }
        }
    }

    [[nodiscard]] Tina::Core::Result<LoadedProjectAssetDocument>
    loadProjectAssetDocument(
        const Tina::Editor::ProjectAssetDescriptor& asset)
    {
        if (!assetResources_.system.has_value()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Tina Editor AssetSystem is unavailable");
        }
        auto loaded = assetResources_.system->loadOne(asset.assetId);
        if (!loaded) {
            return Tina::Core::failure(std::move(loaded.error()));
        }
        const Tina::Asset::CookedAssetFile* file =
            assetResources_.system->tryGet(*loaded);
        if (file == nullptr) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "Opened Catalog asset has no CPU payload");
        }
        switch (Tina::Editor::projectAssetOpenKind(asset.assetKind)) {
        case Tina::Editor::ProjectAssetOpenKind::World3D:
        {
            auto candidate = Tina::Editor::World3DAuthoringDocument::Create(
                document3D_.config());
            if (!candidate) {
                return Tina::Core::failure(std::move(candidate.error()));
            }
            if (auto status = candidate->loadPayload(file->payload()); !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
            TabAuthoringDocument state{
                std::in_place_type<Tina::Editor::World3DAuthoringDocument>,
                std::move(*candidate)};
            return LoadedProjectAssetDocument{
                .document = std::optional<TabAuthoringDocument>{std::move(state)},
                .targetPlatform = file->header().targetPlatform,
            };
        }
        case Tina::Editor::ProjectAssetOpenKind::SpriteAnimation2D:
        {
            auto seed = spriteAnimationDocument_.snapshot();
            if (!seed) {
                return Tina::Core::failure(std::move(seed.error()));
            }
            seed->clipId = asset.assetId;
            auto candidate = Tina::Editor::SpriteAnimationAuthoringDocument::Create(
                *seed, spriteAnimationDocument_.config());
            if (!candidate) {
                return Tina::Core::failure(std::move(candidate.error()));
            }
            if (auto status = candidate->loadCookedAsset(file->bytes()); !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
            TabAuthoringDocument state{
                std::in_place_type<Tina::Editor::SpriteAnimationAuthoringDocument>,
                std::move(*candidate)};
            return LoadedProjectAssetDocument{
                .document = std::optional<TabAuthoringDocument>{std::move(state)},
                .targetPlatform = file->header().targetPlatform,
            };
        }
        case Tina::Editor::ProjectAssetOpenKind::TileMap2D: {
            Tina::Core::AssetId tilesetId{};
            std::vector<Tina::Core::AssetId> chunkIds;
            std::vector<Tina::Core::AssetId> dependencies;
            try {
                chunkIds.reserve(file->header().dependencyCount);
                dependencies.reserve(file->header().dependencyCount);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "TileMap document dependency allocation failed");
            }
            for (u32 index = 0; index < file->header().dependencyCount; ++index) {
                const auto dependency = file->dependency(index);
                if (!dependency) {
                    return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                               "TileMap document dependency disappeared");
                }
                if (dependency->expectedKind == Tina::AssetFormat::AssetKind::Tileset) {
                    tilesetId = dependency->assetId;
                    dependencies.push_back(dependency->assetId);
                } else if (dependency->expectedKind ==
                           Tina::AssetFormat::AssetKind::TileMapChunk) {
                    chunkIds.push_back(dependency->assetId);
                    dependencies.push_back(dependency->assetId);
                }
            }
            if (!dependencies.empty()) {
                auto dependencyHandles = assetResources_.system->load(dependencies);
                if (!dependencyHandles) {
                    return Tina::Core::failure(std::move(dependencyHandles.error()));
                }
            }
            std::vector<Tina::Editor::TileMapAuthoringChunkSource> chunks;
            try {
                chunks.reserve(chunkIds.size());
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "TileMap document chunk allocation failed");
            }
            for (const Tina::Core::AssetId chunkId : chunkIds) {
                const auto handle = assetResources_.system->find(chunkId);
                const auto* chunkFile = handle ? assetResources_.system->tryGet(*handle)
                                               : nullptr;
                if (chunkFile == nullptr) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                        "TileMap Catalog chunk is not resident");
                }
                chunks.push_back({.assetId = chunkId,
                                  .payloadBytes = chunkFile->payload()});
            }
            if (!tilesetId) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "TileMap Catalog asset has no Tileset dependency");
            }
            auto seed = tileMapDocument_.snapshot();
            if (!seed) {
                return Tina::Core::failure(std::move(seed.error()));
            }
            seed->tileMapId = asset.assetId;
            seed->tilesetId = tilesetId;
            auto candidate = Tina::Editor::TileMapAuthoringDocument::Create(
                *seed, tileMapDocument_.config());
            if (!candidate) {
                return Tina::Core::failure(std::move(candidate.error()));
            }
            if (auto status = candidate->loadPayloadFamily(
                    asset.assetId, tilesetId, file->payload(), chunks);
                !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
            TabAuthoringDocument state{
                std::in_place_type<Tina::Editor::TileMapAuthoringDocument>,
                std::move(*candidate)};
            return LoadedProjectAssetDocument{
                .document = std::optional<TabAuthoringDocument>{std::move(state)},
                .targetPlatform = file->header().targetPlatform,
            };
        }
        case Tina::Editor::ProjectAssetOpenKind::AssetInspector:
        default:
            return LoadedProjectAssetDocument{
                .targetPlatform = file->header().targetPlatform,
            };
        }
    }

    [[nodiscard]] Tina::Core::Status openSelectedProjectAsset(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (auto status = synchronizeActiveTabDirty(); !status) {
            return status;
        }
        const auto* asset = projectAssets_.selectedItem();
        if (asset == nullptr) {
            return Tina::Core::failure(Tina::Editor::EditorErrorCode::ProjectAssetNotFound,
                                       "Project Asset Browser has no selected asset");
        }
        const Tina::Editor::EditorDocumentKind documentKind =
            Tina::Editor::editorDocumentKindForAsset(asset->assetKind);
        const Tina::Editor::EditorDocumentKey documentKey{
            .kind = documentKind,
            .assetId = asset->assetId,
        };
        if (const auto existing = documentTabs_.find(documentKey); existing.has_value()) {
            ++counters_.projectAssetOpenCount;
            authoringFeedback_ = "Catalog asset tab activated";
            return activateDocumentTab(tree, static_cast<u32>(*existing));
        }
        if (!documentTabs_.find(documentKey) &&
            documentTabs_.tabCount() >= documentTabs_.config().tabCapacity) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabCapacityExceeded,
                "Close a document before opening another Catalog asset");
        }
        auto loadedDocument = loadProjectAssetDocument(*asset);
        if (!loadedDocument) {
            return Tina::Core::failure(std::move(loadedDocument.error()));
        }
        std::optional<WorkspaceSessionState> preparedSession{};
        if (loadedDocument->document.has_value()) {
            auto session = makeProjectAssetSession(
                documentKey, *loadedDocument->document,
                loadedDocument->targetPlatform);
            if (!session) {
                return Tina::Core::failure(std::move(session.error()));
            }
            preparedSession.emplace(std::move(*session));
        }
        const Tina::Core::usize previousActiveIndex = documentTabs_.activeIndex();
        auto opened = documentTabs_.open(Tina::Editor::EditorDocumentTabDesc{
            .key = documentKey,
            .title = asset->displayName,
        });
        if (!opened) {
            return Tina::Core::failure(std::move(opened.error()));
        }
        if (preparedSession.has_value()) {
            if (auto status = installDocumentSession(std::move(*preparedSession)); !status) {
                (void)documentTabs_.close(*opened, true);
                (void)documentTabs_.activate(previousActiveIndex);
                return status;
            }
        }
        if (loadedDocument->document.has_value()) {
            if (auto status = installNewAuthoringDocument(
                    documentKey, std::move(*loadedDocument->document));
                !status) {
                discardDocumentSession(documentKey);
                (void)documentTabs_.close(*opened, true);
                (void)documentTabs_.activate(previousActiveIndex);
                return status;
            }
            ++counters_.tabOwnedDocumentLoads;
        }
        ++counters_.projectAssetOpenCount;
        authoringFeedback_ = "Catalog asset opened in a document tab";
        return activateDocumentTab(tree, static_cast<u32>(*opened));
    }

    [[nodiscard]] Tina::Core::Status showDirtyCloseModal(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr || tab->pinned || !tab->dirty) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Dirty-close confirmation requires a modified unpinned document");
        }
        const WorkspaceSessionState* session = findDocumentSession(tab->key);
        try {
            std::string title = "Save changes to ";
            title += tab->title;
            title += '?';
            if (auto status = tree.setText(dirtyCloseTitle_, title); !status) {
                return status;
            }
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Dirty-close title allocation failed");
        }
        if (auto status = tree.setText(
                dirtyCloseMessage_,
                session != nullptr && session->hasDocumentPath()
                    ? "Save the canonical document, discard the edits, or cancel closing."
                    : "Choose a Save As destination, discard the edits, or cancel closing.");
            !status) {
            return status;
        }
        if (auto status = tree.setText(
                dirtyClosePath_,
                session != nullptr ? session->documentPathUtf8 : std::string_view{});
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(dirtyClosePath_, session != nullptr); !status) {
            return status;
        }
        if (auto status = tree.setText(
                dirtyCloseSaveButton_,
                session != nullptr && session->hasDocumentPath() ? "Save" : "Save As");
            !status) {
            return status;
        }
        pendingDirtyCloseKey_ = tab->key;
        if (auto status = tree.setLayoutStyle(
                dirtyCloseModal_, dirtyCloseModalLayout(UI::UIVisibility::Visible));
            !status) {
            pendingDirtyCloseKey_.reset();
            return status;
        }
        return tree.requestFocus(
            session != nullptr && session->hasDocumentPath()
                ? dirtyCloseSaveButton_
                : dirtyClosePath_);
    }

    [[nodiscard]] Tina::Core::Status hideDirtyCloseModal(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (auto status = tree.setLayoutStyle(
                dirtyCloseModal_, dirtyCloseModalLayout(UI::UIVisibility::Collapsed));
            !status) {
            return status;
        }
        pendingDirtyCloseKey_.reset();
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status closeActiveDocument(
        Tina::PrimaryWindowUITreeUpdater& tree,
        bool discardDirty = false)
    {
        if (auto status = synchronizeActiveTabDirty(); !status) {
            return status;
        }
        const Tina::Core::usize closingIndex = documentTabs_.activeIndex();
        const auto* closingTab = documentTabs_.activeTab();
        if (closingTab == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                "Editor has no active document tab to close");
        }
        if (closingTab->pinned) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::PinnedDocumentCannotClose,
                "Pinned Editor document cannot be closed");
        }
        if (closingTab->dirty && !discardDirty) {
            return showDirtyCloseModal(tree);
        }
        const Tina::Editor::EditorDocumentKey closingKey = closingTab->key;
        if (auto* owner = activeAuthoringDocumentOwner(closingKey.kind);
            owner != nullptr && *owner == closingKey) {
            const Tina::Editor::EditorDocumentTabDesc* replacement = nullptr;
            for (Tina::Core::usize index = 0; index < documentTabs_.tabCount(); ++index) {
                const auto* candidate = documentTabs_.tab(index);
                if (index != closingIndex && candidate != nullptr &&
                    candidate->key.kind == closingKey.kind) {
                    replacement = candidate;
                    break;
                }
            }
            if (replacement == nullptr) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Closing authoring tab has no remaining document owner");
            }
            if (auto status = switchActiveAuthoringDocument(replacement->key); !status) {
                return status;
            }
            discardSuspendedAuthoringDocument(closingKey);
        }
        if (auto status = documentTabs_.close(closingIndex, discardDirty); !status) {
            return status;
        }
        discardDocumentSession(closingKey);
        assetInspectorActive_ = false;
        synchronizeViewportSelectionFromHierarchy();
        authoringFeedback_ = "Document tab closed";
        if (documentTabs_.activeTab() == nullptr) {
            return refreshDocumentTabsUi(tree);
        }
        return activateDocumentTab(tree,
                                   static_cast<u32>(documentTabs_.activeIndex()));
    }

    [[nodiscard]] bool dirtyCloseTargetsActiveDocument() const noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        return tab != nullptr && pendingDirtyCloseKey_.has_value() &&
               tab->key == *pendingDirtyCloseKey_;
    }

    [[nodiscard]] Tina::Core::Status confirmDirtyCloseSave(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (!dirtyCloseTargetsActiveDocument()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                "Dirty-close target is no longer the active document");
        }
        const WorkspaceSessionState* session = activeDocumentSession();
        if (session == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Active dirty document does not support persistence");
        }

        Tina::Core::Status saveStatus = Tina::Core::success();
        if (session->hasDocumentPath()) {
            saveStatus = saveActiveDocument();
        } else {
            auto path = tree.text(dirtyClosePath_);
            if (!path) {
                return Tina::Core::failure(std::move(path.error()));
            }
            auto selectedPath = requestNativeSaveAsPath(*path);
            if (!selectedPath) {
                if (selectedPath.error().code == Tina::Core::CoreErrorCode::Unsupported) {
                    saveStatus = saveActiveDocument(*path);
                } else {
                    saveStatus = Tina::Core::failure(std::move(selectedPath.error()));
                }
            } else if (!selectedPath->has_value()) {
                authoringFeedback_ = "Save As cancelled; dirty document remains open";
                return tree.setText(dirtyCloseMessage_, authoringFeedback_);
            } else {
                saveStatus = saveActiveDocument(**selectedPath);
            }
        }
        if (!saveStatus) {
            try {
                std::string message = "Save failed: ";
                message += saveStatus.error().message;
                authoringFeedback_ = message;
                return tree.setText(dirtyCloseMessage_, message);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "Save failure message allocation failed");
            }
        }
        if (auto status = closeActiveDocument(tree); !status) {
            return status;
        }
        return hideDirtyCloseModal(tree);
    }

    [[nodiscard]] Tina::Core::Status confirmDirtyCloseDiscard(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (!dirtyCloseTargetsActiveDocument()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                "Dirty-close target is no longer the active document");
        }
        if (auto status = closeActiveDocument(tree, true); !status) {
            return status;
        }
        return hideDirtyCloseModal(tree);
    }

    [[nodiscard]] Tina::Core::Status cancelDirtyClose(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        if (!dirtyCloseTargetsActiveDocument()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                "Dirty-close target is no longer the active document");
        }
        authoringFeedback_ = "Close cancelled; document and selection preserved";
        return hideDirtyCloseModal(tree);
    }

    [[nodiscard]] Tina::Core::Status activateDocumentTab(
        Tina::PrimaryWindowUITreeUpdater& tree, u32 index)
    {
        if (index != documentTabs_.activeIndex()) {
            if (auto status = synchronizeActiveTabDirty(); !status) {
                return status;
            }
        }
        const auto* tab = documentTabs_.tab(index);
        if (tab == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                "Editor document tab does not exist");
        }
        if (index != documentTabs_.activeIndex()) {
            resetViewportInteractionState();
        }
        if (auto status = switchActiveAuthoringDocument(tab->key); !status) {
            return status;
        }
        if (auto status = documentTabs_.activate(index); !status) {
            return status;
        }
        if (auto status = refreshToolbarPathForActiveTab(tree); !status) {
            return status;
        }
        const bool restoreSceneSelection = assetInspectorActive_;
        const u32 preferredHierarchyStableId = restoreSceneSelection
                                                   ? stableEntityIdForHierarchyItem(
                                                         selectionKey_)
                                                   : 0U;
        assetInspectorActive_ = tab->key.kind ==
                                Tina::Editor::EditorDocumentKind::AssetInspector;
        if (assetInspectorActive_) {
            synchronizeViewportSelectionFromHierarchy();
        }
        switch (Tina::Editor::editorDocumentWorkspace(tab->key.kind)) {
        case Tina::Editor::EditorDocumentWorkspace::TwoD:
            if (auto status = activateWorkspace(tree, WorkspaceMode::World2D); !status) {
                return status;
            }
            break;
        case Tina::Editor::EditorDocumentWorkspace::ThreeD:
            if (auto status = activateWorkspace(tree, WorkspaceMode::World3D); !status) {
                return status;
            }
            break;
        case Tina::Editor::EditorDocumentWorkspace::None:
            break;
        }
        if (!assetInspectorActive_) {
            if (auto status = refreshHierarchyTree(
                    tree, preferredHierarchyStableId); !status) {
                return status;
            }
        }
        ++counters_.documentTabSwitches;
        if (auto status = refreshAuthoringUi(tree); !status) {
            return status;
        }
        return refreshDocumentTabsUi(tree);
    }

    [[nodiscard]] Tina::Core::Status executeEditorCommand(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const EditorCommand command = *pendingEditorCommand_;
        pendingEditorCommand_.reset();

        Tina::Core::Status status = Tina::Core::success();
        bool requiresPreviewValidation = false;
        bool animationDocumentChanged = false;
        std::optional<u32> hierarchyRefreshStableId{};
        const bool playCommand =
            command >= EditorCommand::PlayStartOrResume &&
            command <= EditorCommand::PlayStop;
        const bool previewNavigationCommand =
            command == EditorCommand::SceneFocus ||
            command == EditorCommand::ViewportCyclePreset ||
            command == EditorCommand::ViewportResetView;
        if (playSessionActive() && !playCommand && !previewNavigationCommand) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Editor authoring commands are locked while the isolated play session is active");
        }
        if (command >= EditorCommand::AnimationTogglePlayback &&
            command <= EditorCommand::AnimationRedo &&
            workspaceMode_ != WorkspaceMode::World2D) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "SpriteAnimationClip authoring is available in the 2D workspace");
        }
        switch (command) {
        case EditorCommand::SwitchToWorld2D:
            status = activateDocumentTab(tree, 0U);
            break;
        case EditorCommand::SwitchToWorld3D:
            status = activateDocumentTab(tree, 1U);
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
            InspectorTransformInput input{};
            const auto parseField = [&](UI::UINodeId field,
                                        std::string_view fieldName)
                -> Tina::Core::Result<std::optional<float>> {
                auto text = tree.text(field);
                if (!text) {
                    return Tina::Core::failure(std::move(text.error()));
                }
                return parseInspectorTransformValue(*text, fieldName);
            };
            const auto parseInto = [&](UI::UINodeId field,
                                       std::string_view fieldName,
                                       std::optional<float>& output)
                -> Tina::Core::Status {
                auto parsed = parseField(field, fieldName);
                if (!parsed) {
                    return Tina::Core::failure(std::move(parsed.error()));
                }
                output = *parsed;
                return Tina::Core::success();
            };
            const auto rejectInput = [&](const Tina::Core::Error& error)
                -> Tina::Core::Status {
                try {
                    authoringFeedback_ = "Transform rejected: ";
                    authoringFeedback_ += error.message;
                } catch (const std::bad_alloc&) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::OutOfMemory,
                        "Inspector rejection feedback allocation failed");
                }
                ++counters_.inspectorRejectedTransactions;
                return refreshAuthoringUi(tree);
            };

            status = parseInto(inspectorPositionX_, "Position X",
                               input.positionX);
            if (status) {
                status = parseInto(inspectorPositionY_, "Position Y",
                                   input.positionY);
            }
            if (status && workspaceMode_ == WorkspaceMode::World3D) {
                status = parseInto(inspectorPositionZ_, "Position Z",
                                   input.positionZ);
            }
            if (status && workspaceMode_ == WorkspaceMode::World3D) {
                status = parseInto(inspectorRotationX_, "Rotation X",
                                   input.rotationX);
            }
            if (status && workspaceMode_ == WorkspaceMode::World3D) {
                status = parseInto(inspectorRotationY_, "Rotation Y",
                                   input.rotationY);
            }
            if (status) {
                status = parseInto(inspectorRotationZ_, "Rotation Z",
                                   input.rotationZ);
            }
            if (status) {
                status = parseInto(inspectorScaleX_, "Scale X", input.scaleX);
            }
            if (status) {
                status = parseInto(inspectorScaleY_, "Scale Y", input.scaleY);
            }
            if (status && workspaceMode_ == WorkspaceMode::World3D) {
                status = parseInto(inspectorScaleZ_, "Scale Z", input.scaleZ);
            }
            if (!status) {
                if (status.error().code ==
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation) {
                    return rejectInput(status.error());
                }
                return status;
            }

            const u64 revisionBefore = activeDocumentRevision();
            status = applySelectedTransform(input);
            if (!status) {
                if (status.error().code ==
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation ||
                    status.error().code ==
                        Tina::Editor::EditorErrorCode::EntityNotFound) {
                    return rejectInput(status.error());
                }
                break;
            }
            const u64 revisionAfter = activeDocumentRevision();
            if (revisionAfter == revisionBefore) {
                authoringFeedback_ =
                    "Transform unchanged; no document revision published";
                break;
            }
            if (revisionAfter < revisionBefore ||
                revisionAfter - revisionBefore != 1U) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Inspector batch transform did not publish exactly one document revision");
            }
            ++counters_.authoringEdits;
            ++counters_.inspectorTransactions;
            requiresPreviewValidation = true;
            if (viewportSelectedEntityCount_ > 1U) {
                try {
                    authoringFeedback_ = "Transform applied to ";
                    authoringFeedback_ +=
                        std::to_string(viewportSelectedEntityCount_);
                    authoringFeedback_ +=
                        " selected entities as one document revision";
                } catch (const std::bad_alloc&) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::OutOfMemory,
                        "Inspector transaction feedback allocation failed");
                }
            } else {
                authoringFeedback_ =
                    "Transform applied as one document revision";
            }
            break;
        }
        case EditorCommand::Undo: {
            const auto* activeTab = documentTabs_.activeTab();
            if (activeTab == nullptr) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                    "Editor has no active document to undo");
            }
            const Tina::Editor::EditorDocumentKind activeKind = activeTab->key.kind;
            switch (activeKind) {
            case Tina::Editor::EditorDocumentKind::TileMap2D:
                status = tileMapDocument_.undo();
                break;
            case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
                status = spriteAnimationDocument_.undo();
                break;
            case Tina::Editor::EditorDocumentKind::World3D:
                status = document3D_.undo();
                break;
            case Tina::Editor::EditorDocumentKind::World2D:
            default:
                status = document_.undo();
                break;
            }
            if (status) {
                ++counters_.authoringUndos;
                if (activeKind == Tina::Editor::EditorDocumentKind::TileMap2D) {
                    ++counters_.tileMapUndos;
                } else if (activeKind ==
                           Tina::Editor::EditorDocumentKind::SpriteAnimation2D) {
                    animationSelectedFrameIndex_ = (std::min)(
                        animationSelectedFrameIndex_,
                        static_cast<u32>(spriteAnimationDocument_.frameCount() - 1U));
                    ++counters_.animationUndos;
                    animationDocumentChanged = true;
                }
                requiresPreviewValidation = true;
                if (activeKind == Tina::Editor::EditorDocumentKind::World2D ||
                    activeKind == Tina::Editor::EditorDocumentKind::World3D) {
                    hierarchyRefreshStableId =
                        stableEntityIdForHierarchyItem(selectionKey_);
                }
                authoringFeedback_ = "Undo restored the previous canonical snapshot";
            }
            break;
        }
        case EditorCommand::Redo: {
            const auto* activeTab = documentTabs_.activeTab();
            if (activeTab == nullptr) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                    "Editor has no active document to redo");
            }
            const Tina::Editor::EditorDocumentKind activeKind = activeTab->key.kind;
            switch (activeKind) {
            case Tina::Editor::EditorDocumentKind::TileMap2D:
                status = tileMapDocument_.redo();
                break;
            case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
                status = spriteAnimationDocument_.redo();
                break;
            case Tina::Editor::EditorDocumentKind::World3D:
                status = document3D_.redo();
                break;
            case Tina::Editor::EditorDocumentKind::World2D:
            default:
                status = document_.redo();
                break;
            }
            if (status) {
                ++counters_.authoringRedos;
                if (activeKind == Tina::Editor::EditorDocumentKind::TileMap2D) {
                    ++counters_.tileMapRedos;
                } else if (activeKind ==
                           Tina::Editor::EditorDocumentKind::SpriteAnimation2D) {
                    animationSelectedFrameIndex_ = (std::min)(
                        animationSelectedFrameIndex_,
                        static_cast<u32>(spriteAnimationDocument_.frameCount() - 1U));
                    ++counters_.animationRedos;
                    animationDocumentChanged = true;
                }
                requiresPreviewValidation = true;
                if (activeKind == Tina::Editor::EditorDocumentKind::World2D ||
                    activeKind == Tina::Editor::EditorDocumentKind::World3D) {
                    hierarchyRefreshStableId =
                        stableEntityIdForHierarchyItem(selectionKey_);
                }
                authoringFeedback_ = "Redo restored the next canonical snapshot";
            }
            break;
        }
        case EditorCommand::Save:
        case EditorCommand::SaveAs: {
            if (command == EditorCommand::SaveAs) {
                auto path = tree.text(toolbarPath_);
                if (!path) {
                    return Tina::Core::failure(std::move(path.error()));
                }
                auto selectedPath = requestNativeSaveAsPath(*path);
                if (!selectedPath) {
                    if (selectedPath.error().code == Tina::Core::CoreErrorCode::Unsupported) {
                        status = saveActiveDocument(*path);
                    } else {
                        status = Tina::Core::failure(std::move(selectedPath.error()));
                    }
                } else if (!selectedPath->has_value()) {
                    authoringFeedback_ = "Save As cancelled; document preserved";
                    status = Tina::Core::success();
                    break;
                } else {
                    status = saveActiveDocument(**selectedPath);
                }
            } else {
                status = saveActiveDocument();
            }
            if (!status) {
                try {
                    authoringFeedback_ = "Save failed: ";
                    authoringFeedback_ += status.error().message;
                } catch (const std::bad_alloc&) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::OutOfMemory,
                        "Save failure message allocation failed");
                }
                status = Tina::Core::success();
                break;
            }
            if (auto pathStatus = refreshToolbarPathForActiveTab(tree); !pathStatus) {
                return pathStatus;
            }
            switch (documentTabs_.activeTab()->key.kind) {
            case Tina::Editor::EditorDocumentKind::World2D:
                authoringFeedback_ = "Canonical World2D document saved atomically";
                break;
            case Tina::Editor::EditorDocumentKind::World3D:
                authoringFeedback_ = "Canonical Prefab v2 document saved atomically";
                break;
            case Tina::Editor::EditorDocumentKind::TileMap2D:
                authoringFeedback_ = "Canonical TileMap root and chunks saved root-last";
                break;
            case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
                authoringFeedback_ = "Canonical SpriteAnimationClip saved atomically";
                break;
            case Tina::Editor::EditorDocumentKind::AssetInspector:
            default:
                break;
            }
            break;
        }
        case EditorCommand::SceneAdd: {
            if (!sceneDocumentActive()) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Scene hierarchy commands require an active World2D or World3D document");
                break;
            }
            const u32 parentStableId =
                assetInspectorActive_ ? 0U : stableEntityIdForHierarchyItem(selectionKey_);
            auto added = workspaceMode_ == WorkspaceMode::World2D
                             ? Tina::Editor::addWorld2DEntity(document_, parentStableId)
                             : Tina::Editor::addWorld3DNode(document3D_, parentStableId);
            if (!added) {
                status = Tina::Core::failure(std::move(added.error()));
                break;
            }
            hierarchyRefreshStableId = added->primaryStableId;
            requiresPreviewValidation = true;
            ++counters_.authoringEdits;
            ++counters_.sceneAddCommands;
            if (options_.autoDemo) {
                counters_.automaticAddedStableId = added->primaryStableId;
            }
            authoringFeedback_ = workspaceMode_ == WorkspaceMode::World2D
                                     ? "Entity2D added as one scene revision"
                                     : "Node3D added as one scene revision";
            break;
        }
        case EditorCommand::SceneDuplicate: {
            if (!sceneDocumentActive()) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Scene hierarchy commands require an active World2D or World3D document");
                break;
            }
            const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
            if (stableId == 0U) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Select a scene item before duplicating it");
                break;
            }
            auto duplicated = workspaceMode_ == WorkspaceMode::World2D
                                  ? Tina::Editor::duplicateWorld2DEntitySubtree(
                                        document_, stableId)
                                  : Tina::Editor::duplicateWorld3DNodeSubtree(
                                        document3D_, stableId);
            if (!duplicated) {
                status = Tina::Core::failure(std::move(duplicated.error()));
                break;
            }
            hierarchyRefreshStableId = duplicated->primaryStableId;
            requiresPreviewValidation = true;
            ++counters_.authoringEdits;
            ++counters_.sceneDuplicateCommands;
            if (options_.autoDemo) {
                counters_.automaticDuplicatedStableId =
                    duplicated->primaryStableId;
            }
            authoringFeedback_ = "Scene subtree duplicated as one canonical revision";
            break;
        }
        case EditorCommand::SceneDelete: {
            if (!sceneDocumentActive()) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Scene hierarchy commands require an active World2D or World3D document");
                break;
            }
            const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
            if (stableId == 0U) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "The document root cannot be deleted");
                break;
            }
            auto deleted = workspaceMode_ == WorkspaceMode::World2D
                               ? Tina::Editor::deleteWorld2DEntitySubtree(
                                     document_, stableId)
                               : Tina::Editor::deleteWorld3DNodeSubtree(
                                     document3D_, stableId);
            if (!deleted) {
                status = Tina::Core::failure(std::move(deleted.error()));
                break;
            }
            hierarchyRefreshStableId = deleted->primaryStableId;
            requiresPreviewValidation = true;
            ++counters_.authoringEdits;
            ++counters_.sceneDeleteCommands;
            authoringFeedback_ = "Scene subtree deleted as one canonical revision";
            break;
        }
        case EditorCommand::SceneReparentRoot: {
            if (!sceneDocumentActive()) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Scene hierarchy commands require an active World2D or World3D document");
                break;
            }
            const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
            if (stableId == 0U) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Select a scene item before reparenting it");
                break;
            }
            const EditorHierarchyRow* selectedRow = hierarchyRow(stableId);
            if (selectedRow != nullptr && selectedRow->parentStableId == 0U) {
                authoringFeedback_ = "Scene item is already at the document root";
                break;
            }
            status = workspaceMode_ == WorkspaceMode::World2D
                         ? Tina::Editor::reparentWorld2DEntity(
                               document_, stableId, 0U)
                         : Tina::Editor::reparentWorld3DNode(
                               document3D_, stableId, 0U);
            if (status) {
                hierarchyRefreshStableId = stableId;
                requiresPreviewValidation = true;
                ++counters_.authoringEdits;
                ++counters_.sceneReparentRootCommands;
                authoringFeedback_ = "Scene item reparented to the document root";
            }
            break;
        }
        case EditorCommand::SceneReparent: {
            if (!sceneDocumentActive()) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Scene hierarchy commands require an active World2D or World3D document");
                break;
            }
            const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
            if (stableId == 0U) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Select a scene item before reparenting it");
                break;
            }
            auto parentText = tree.text(inspectorParentStableId_);
            if (!parentText) {
                return Tina::Core::failure(std::move(parentText.error()));
            }
            u32 parentStableId = 0U;
            if (!parseUnsigned(*parentText, parentStableId)) {
                ++counters_.inspectorRejectedTransactions;
                authoringFeedback_ = "Parent rejected: enter an unsigned stable ID (0 means scene root)";
                return refreshAuthoringUi(tree);
            }
            const EditorHierarchyRow* selectedRow = hierarchyRow(stableId);
            if (selectedRow != nullptr && selectedRow->parentStableId == parentStableId) {
                authoringFeedback_ = "Parent unchanged; no document revision was published";
                break;
            }
            status = workspaceMode_ == WorkspaceMode::World2D
                         ? Tina::Editor::reparentWorld2DEntity(
                               document_, stableId, parentStableId)
                         : Tina::Editor::reparentWorld3DNode(
                               document3D_, stableId, parentStableId);
            if (status) {
                hierarchyRefreshStableId = stableId;
                requiresPreviewValidation = true;
                ++counters_.authoringEdits;
                ++counters_.sceneReparentCommands;
                authoringFeedback_ = "Scene item reparented as one canonical revision";
            }
            break;
        }
        case EditorCommand::SceneFocus:
            status = focusViewportOnSelection();
            if (status) {
                authoringFeedback_ = "Viewport focused on the selected scene item";
            }
            break;
        case EditorCommand::PlayStartOrResume:
            if (!playSession_.has_value()) {
                status = Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Editor play session owner is unavailable");
                break;
            }
            if (playSession_->snapshot().state == Tina::Editor::EditorPlayState::Paused) {
                status = playSession_->resume();
                if (status) {
                    ++counters_.playResumes;
                    authoringFeedback_ = "Isolated scene simulation resumed";
                }
                break;
            }
            if (playSession_->snapshot().state == Tina::Editor::EditorPlayState::Playing) {
                authoringFeedback_ = "Isolated scene simulation is already running";
                break;
            }
            if (!sceneDocumentActive()) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Play requires an active World2D or World3D scene document");
                break;
            }
            if (!playStartReady()) {
                authoringFeedback_ =
                    "Play is waiting for project, import, Catalog, and preview updates to settle";
                break;
            }
            resetViewportInteractionState();
            viewportToolMode_ = ViewportToolMode::Select;
            animationPlaying_ = false;
            if (animationAnimator_.has_value()) {
                animationAnimator_->pause();
            }
            if (workspaceMode_ == WorkspaceMode::World2D) {
                status = playSession_->start(
                    Tina::Editor::EditorPlayWorkspace::TwoD,
                    document_.revision(), document_.snapshotBytes());
            } else {
                status = playSession_->start(
                    Tina::Editor::EditorPlayWorkspace::ThreeD,
                    document3D_.revision(), document3D_.payloadBytes());
            }
            if (status) {
                requiresPreviewValidation = true;
                ++counters_.playStarts;
                authoringFeedback_ = "Isolated scene simulation started from a canonical snapshot";
            }
            break;
        case EditorCommand::PlayPause:
            if (!playSession_.has_value()) {
                status = Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Editor play session owner is unavailable");
                break;
            }
            status = playSession_->pause();
            if (status) {
                ++counters_.playPauses;
                authoringFeedback_ = "Isolated scene simulation paused";
            }
            break;
        case EditorCommand::PlayStep: {
            if (!playSession_.has_value()) {
                status = Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Editor play session owner is unavailable");
                break;
            }
            const bool stepWasPending = playSession_->snapshot().stepPending;
            status = playSession_->requestStep();
            if (status) {
                if (!stepWasPending) {
                    ++counters_.playStepRequests;
                }
                authoringFeedback_ = "One fixed simulation step queued";
            }
            break;
        }
        case EditorCommand::PlayStop: {
            if (!playSession_.has_value()) {
                status = Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Editor play session owner is unavailable");
                break;
            }
            const bool playWasActive = playSession_->active();
            counters_.playMaximumSimulationTick = (std::max)(
                counters_.playMaximumSimulationTick,
                playSession_->snapshot().simulationTickCount);
            status = playSession_->stop();
            if (status) {
                if (playWasActive) {
                    ++counters_.playStops;
                }
                requiresPreviewValidation = true;
                authoringFeedback_ = "Play session stopped; canonical authoring scene restored";
            }
            break;
        }
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
        case EditorCommand::GenerateTileMapGameplay: {
            if (!tileMapEditingContext()) {
                status = Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Gameplay generation requires the 2D TileMap selection");
                break;
            }
            constexpr std::array Archetypes{
                Tina::Editor::TileMapGameplayArchetypeBinding{
                    .archetype = "player", .gameArchetypeId = EditorPlayerArchetypeId},
                Tina::Editor::TileMapGameplayArchetypeBinding{
                    .archetype = "crate", .gameArchetypeId = EditorCrateArchetypeId},
            };
            auto generated = Tina::Editor::generateTileMapGameplay(
                tileMapDocument_, document_, Archetypes,
                {
                    .objectLayerId = InitialGameplayObjectLayerId,
                    .archetypePropertyKey = "archetype",
                    .recordCapacity = tileMapDocument_.config().objectCapacity,
                },
                {
                    .gameplaySchema = EditorGameplaySpawnSchema,
                    .gameplayVersion = EditorGameplaySpawnVersion,
                },
                encodeEditorGameplaySpawns);
            if (!generated) {
                status = Tina::Core::failure(std::move(generated.error()));
                break;
            }
            ++counters_.tileMapGameplayGenerations;
            counters_.tileMapGameplaySpawnRecords = generated->records().size();
            counters_.tileMapGameplayBytes = document_.gameplayByteCount();
            counters_.tileMapGameplaySourceRevision = generated->sourceDocumentRevision();
            ++counters_.authoringEdits;
            requiresPreviewValidation = true;
            authoringFeedback_ = "Gameplay spawn plan generated as one World2D revision";
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
                animationAnimator_->play();
                animationPlaying_ = true;
                status = applyAnimationPreviewFrame(
                    static_cast<u32>(animationAnimator_->frameIndex()));
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
        case EditorCommand::ViewportCyclePreset: {
            if (workspaceMode_ != WorkspaceMode::World3D) {
                authoringFeedback_ = "2D viewport uses a fixed Orthographic view";
                break;
            }
            if (auto navigationStatus = ensureViewportNavigation(); !navigationStatus) {
                return navigationStatus;
            }
            const auto preset = nextViewportViewPreset(viewport3DViewPreset_);
            status = viewportNavigation_->set3DViewPreset(preset);
            if (status) {
                viewport3DViewPreset_ = preset;
                viewportViewModeRefreshPending_ = true;
                status = applyViewportNavigationToPreview();
                authoringFeedback_ = "3D view: ";
                authoringFeedback_ += viewportViewPresetName(preset);
            }
            break;
        }
        case EditorCommand::ViewportResetView:
            status = frameViewportContents();
            viewport3DViewPreset_.reset();
            if (status) {
                viewportViewModeRefreshPending_ = true;
                authoringFeedback_ = "Viewport framed to the authored content";
            }
            break;
        case EditorCommand::ProjectFilterAll:
            status = applyProjectAssetFilter(tree, Tina::Editor::ProjectAssetFilter::All);
            break;
        case EditorCommand::ProjectFilter2D:
            status = applyProjectAssetFilter(tree, Tina::Editor::ProjectAssetFilter::TwoD);
            break;
        case EditorCommand::ProjectFilter3D:
            status = applyProjectAssetFilter(tree, Tina::Editor::ProjectAssetFilter::ThreeD);
            break;
        case EditorCommand::ProjectFilterMedia:
            status = applyProjectAssetFilter(tree, Tina::Editor::ProjectAssetFilter::Media);
            break;
        case EditorCommand::RefreshProjectCatalog:
            catalogRefreshPending_ = true;
            authoringFeedback_ = "Catalog refresh scheduled before the next render packet";
            break;
        case EditorCommand::CreateProject:
            status = createNewProjectFromDialog();
            break;
        case EditorCommand::OpenProject:
            status = openProjectFromDialog();
            break;
        case EditorCommand::ImportSource:
            status = importSourceFromDialog();
            break;
        case EditorCommand::OpenSelectedProjectAsset:
            status = openSelectedProjectAsset(tree);
            break;
        case EditorCommand::CloseActiveDocument:
            status = closeActiveDocument(tree);
            break;
        case EditorCommand::DirtyCloseSave:
            status = confirmDirtyCloseSave(tree);
            break;
        case EditorCommand::DirtyCloseDiscard:
            status = confirmDirtyCloseDiscard(tree);
            break;
        case EditorCommand::DirtyCloseCancel:
            status = cancelDirtyClose(tree);
            break;
        }
        if (!status) {
            return status;
        }
        if (animationDocumentChanged) {
            if (auto animationStatus = rebuildAnimationAnimator(); !animationStatus) {
                animationAnimator_.reset();
                animationPlaying_ = false;
                animationPreviewAvailable_ = false;
                authoringFeedback_ += " | Runtime preview unavailable: ";
                authoringFeedback_ += animationStatus.error().message;
            }
            auto preview = spriteAnimationDocument_.cookPreview();
            counters_.animationDocumentRevision = spriteAnimationDocument_.revision();
            counters_.animationFrameCount = spriteAnimationDocument_.frameCount();
            if (preview) {
                counters_.animationCookPreviewBytes = preview->cookedBytes.size();
            } else {
                counters_.animationCookPreviewBytes = 0;
                authoringFeedback_ += " | Cook preview unavailable: ";
                authoringFeedback_ += preview.error().message;
            }
        }
        if (requiresPreviewValidation) {
            if (auto previewStatus = validateRuntimePreview(); !previewStatus) {
                return previewStatus;
            }
        }
        if (hierarchyRefreshStableId.has_value()) {
            if (auto hierarchyStatus = refreshHierarchyTree(
                    tree, *hierarchyRefreshStableId);
                !hierarchyStatus) {
                return hierarchyStatus;
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
        resetViewportInteractionState();
        workspaceMode_ = mode;
        if (mode == WorkspaceMode::World3D) {
            animationPlaying_ = false;
            if (animationAnimator_.has_value()) {
                animationAnimator_->pause();
            }
        }
        if (auto status = refreshHierarchyTree(tree, 0); !status) {
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
    refreshViewportViewModeUi(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
        std::string label = "Orthographic";
        if (!world2D) {
            label = "View: ";
            label += viewport3DViewPreset_.has_value()
                         ? viewportViewPresetName(*viewport3DViewPreset_)
                         : "Custom";
        }
        if (auto status = tree.setText(viewportMode_, label); !status) {
            return status;
        }
        return tree.setEnabled(viewportMode_, !world2D);
    }

    [[nodiscard]] Tina::Core::Status
    refreshWorkspaceChrome(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
        if (auto status = tree.setText(toolbarDocument_, world2D ? "World2D Scene" : "World3D Scene");
            !status) {
            return status;
        }
        std::string breadcrumb = world2D ? "Scene / World2D" : "Scene / World3D";
        if (stableEntityIdForHierarchyItem(selectionKey_) != 0U) {
            breadcrumb += " / ";
            breadcrumb += hierarchyDisplayLabel(selectionKey_);
        }
        if (auto status = tree.setText(breadcrumb_, breadcrumb);
            !status) {
            return status;
        }
        if (auto status = tree.setText(viewportTitle_, world2D ? "World2D Viewport"
                                                               : "World3D Viewport");
            !status) {
            return status;
        }
        if (auto status = refreshViewportViewModeUi(tree); !status) {
            return status;
        }
        if (auto status = tree.setText(gridStatus_, world2D ? "Tile Grid 1 m" : "Grid 1 m"); !status) {
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
        if (auto status = tree.setText(
                cameraStatus_,
                world2D ? "Camera2D | MMB Pan | Wheel Zoom"
                        : "Camera3D | MMB Pan | RMB Orbit | Wheel Dolly");
            !status) {
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

    [[nodiscard]] Tina::Core::Result<SavedDocumentBaseline>
    captureActiveDocumentSavedBaseline() const
    {
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentTabNotFound,
                "Editor has no active document to save");
        }
        switch (tab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            return captureSavedBaseline(document_);
        case Tina::Editor::EditorDocumentKind::World3D:
            return captureSavedBaseline(document3D_);
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            return captureSavedBaseline(tileMapDocument_);
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            return captureSavedBaseline(spriteAnimationDocument_);
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Read-only Asset Inspector documents cannot be saved");
        }
    }

    struct SaveDialogLocation final {
        std::string initialDirectoryUtf8{};
        std::string suggestedFileNameUtf8{};
    };

    [[nodiscard]] Tina::Core::Result<SaveDialogLocation>
    makeSaveDialogLocation(std::string_view currentPathUtf8,
                           std::string_view fallbackFileNameUtf8,
                           bool directoryTarget) const
    {
        SaveDialogLocation location{};
        try {
            location.suggestedFileNameUtf8.assign(fallbackFileNameUtf8);
            if (currentPathUtf8.empty() ||
                !Tina::Core::isStrictUtf8WithoutNul(currentPathUtf8)) {
                return location;
            }

            const std::filesystem::path currentPath =
                std::filesystem::u8path(currentPathUtf8.begin(), currentPathUtf8.end());
            std::filesystem::path directory = directoryTarget
                                                  ? currentPath
                                                  : currentPath.parent_path();
            std::error_code directoryError;
            if (!directory.empty() &&
                std::filesystem::is_directory(directory, directoryError) &&
                !directoryError) {
                location.initialDirectoryUtf8 = pathToUtf8(directory);
            } else if (directoryTarget) {
                directoryError.clear();
                directory = currentPath.parent_path();
                if (!directory.empty() &&
                    std::filesystem::is_directory(directory, directoryError) &&
                    !directoryError) {
                    location.initialDirectoryUtf8 = pathToUtf8(directory);
                }
            }

            if (!directoryTarget && currentPath.has_filename()) {
                const std::string candidate = pathToUtf8(currentPath.filename());
                if (!candidate.empty()) {
                    location.suggestedFileNameUtf8 = candidate;
                }
            }
            return location;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Editor Save As dialog path allocation failed");
        } catch (const std::filesystem::filesystem_error&) {
            return location;
        }
    }

    [[nodiscard]] Tina::Core::Result<std::optional<std::string>>
    requestNativeSaveAsPath(std::string_view currentPathUtf8)
    {
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr || activeDocumentSession() == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Active Editor document does not support Save As");
        }

        if (tab->key.kind == Tina::Editor::EditorDocumentKind::TileMap2D) {
            auto location = makeSaveDialogLocation(currentPathUtf8, {}, true);
            if (!location) {
                return Tina::Core::failure(std::move(location.error()));
            }
            auto selected = fileDialog_.pickFolder({
                .titleUtf8 = "Save Tina TileMap",
                .initialDirectoryUtf8 = location->initialDirectoryUtf8,
            });
            if (!selected) {
                return Tina::Core::failure(std::move(selected.error()));
            }
            if (!selected->selected()) {
                return std::optional<std::string>{};
            }
            return std::optional<std::string>{std::move(selected->selectedPathUtf8)};
        }

        std::string_view title = "Save Tina document";
        std::string_view fallbackFileName = "document.tasset";
        std::string_view extension = "tasset";
        Tina::EditorApp::Detail::EditorFileDialogFilter filter{
            .labelUtf8 = "Tina Asset",
            .patternUtf8 = "*.tasset",
        };
        switch (tab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            title = "Save Tina World2D";
            fallbackFileName = "world.tworld";
            extension = "tworld";
            filter = {.labelUtf8 = "Tina World2D", .patternUtf8 = "*.tworld"};
            break;
        case Tina::Editor::EditorDocumentKind::World3D:
            title = "Save Tina World3D Prefab";
            fallbackFileName = "world.tprefab";
            extension = "tprefab";
            filter = {.labelUtf8 = "Tina Prefab", .patternUtf8 = "*.tprefab"};
            break;
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            title = "Save Tina Sprite Animation";
            fallbackFileName = "animation.tasset";
            break;
        case Tina::Editor::EditorDocumentKind::TileMap2D:
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Active Editor document does not support file Save As");
        }

        auto location = makeSaveDialogLocation(currentPathUtf8, fallbackFileName, false);
        if (!location) {
            return Tina::Core::failure(std::move(location.error()));
        }
        const std::array filters{filter};
        auto selected = fileDialog_.saveFile({
            .titleUtf8 = title,
            .initialDirectoryUtf8 = location->initialDirectoryUtf8,
            .suggestedFileNameUtf8 = location->suggestedFileNameUtf8,
            .defaultExtensionUtf8 = extension,
            .filters = filters,
        });
        if (!selected) {
            return Tina::Core::failure(std::move(selected.error()));
        }
        if (!selected->selected()) {
            return std::optional<std::string>{};
        }
        return std::optional<std::string>{std::move(selected->selectedPathUtf8)};
    }

    [[nodiscard]] Tina::Core::Status saveActiveDocument(
        std::optional<std::string_view> saveAsPath = std::nullopt)
    {
        const auto* tab = documentTabs_.activeTab();
        WorkspaceSessionState* session = activeDocumentSession();
        if (tab == nullptr || session == nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Active Editor document does not support persistence");
        }

        std::string preparedPath{};
        try {
            const std::string_view target = saveAsPath.has_value()
                                                ? *saveAsPath
                                                : std::string_view{session->documentPathUtf8};
            if (target.empty() || !Tina::Core::isStrictUtf8WithoutNul(target)) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Editor save path must be non-empty strict UTF-8 without NUL");
            }
            if (documentPathOwnedByOtherSession(target, tab->key)) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Another open Editor document already owns this save path");
            }
            preparedPath.assign(target);
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                       "Editor save path allocation failed");
        }

        auto preparedBaseline = captureActiveDocumentSavedBaseline();
        if (!preparedBaseline) {
            return Tina::Core::failure(std::move(preparedBaseline.error()));
        }

        Tina::Core::Status status = Tina::Core::success();
        switch (tab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            status = Tina::Editor::saveWorld2DAuthoringDocument(preparedPath,
                                                                document_);
            break;
        case Tina::Editor::EditorDocumentKind::World3D:
            status = Tina::Editor::saveWorld3DAuthoringDocument(preparedPath,
                                                                document3D_);
            break;
        case Tina::Editor::EditorDocumentKind::TileMap2D: {
            auto saved = Tina::Editor::saveTileMapAuthoringDocument(
                preparedPath, tileMapDocument_, session->targetPlatform);
            if (!saved) {
                status = Tina::Core::failure(std::move(saved.error()));
            }
            break;
        }
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            status = Tina::Editor::saveSpriteAnimationAuthoringDocument(
                preparedPath, spriteAnimationDocument_, session->targetPlatform);
            break;
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Read-only Asset Inspector documents cannot be saved");
        }
        if (!status) {
            return status;
        }

        session->documentPathUtf8 = std::move(preparedPath);
        session->savedBaseline = std::move(*preparedBaseline);
        ++counters_.authoringSaves;
        return synchronizeActiveTabDirty();
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

    [[nodiscard]] std::span<const std::byte>
    workspaceSessionDocumentBytes(WorkspaceMode mode) const noexcept
    {
        if (mode == WorkspaceMode::World2D || !world3DDocumentOwnerKey_.assetId) {
            return documentBytes(mode);
        }
        const Tina::Editor::EditorDocumentKey baseWorld3DKey{
            .kind = Tina::Editor::EditorDocumentKind::World3D,
        };
        const auto* suspended = findSuspendedAuthoringDocument(baseWorld3DKey);
        if (suspended == nullptr) {
            return {};
        }
        const auto* document = std::get_if<Tina::Editor::World3DAuthoringDocument>(
            &suspended->document);
        return document != nullptr ? document->payloadBytes()
                                   : std::span<const std::byte>{};
    }

    [[nodiscard]] bool isDocumentDirty(WorkspaceMode mode) const noexcept
    {
        const WorkspaceSessionState& session = workspaceSession(mode);
        const std::span<const std::byte> current = workspaceSessionDocumentBytes(mode);
        return !session.savedBaseline.captured ||
               !baselineBytesMatch(session.savedBaseline.primaryBytes, current);
    }

    [[nodiscard]] bool activeTabDocumentDirty() const noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr) {
            return false;
        }
        const WorkspaceSessionState* session = findDocumentSession(tab->key);
        if (session == nullptr) {
            return false;
        }
        switch (tab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            return !savedBaselineMatches(session->savedBaseline, document_);
        case Tina::Editor::EditorDocumentKind::World3D:
            return !savedBaselineMatches(session->savedBaseline, document3D_);
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            return !savedBaselineMatches(session->savedBaseline, tileMapDocument_);
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            return !savedBaselineMatches(session->savedBaseline,
                                         spriteAnimationDocument_);
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return false;
        }
    }

    [[nodiscard]] Tina::Core::Status synchronizeActiveTabDirty() noexcept
    {
        if (documentTabs_.activeTab() == nullptr) {
            return Tina::Core::success();
        }
        return documentTabs_.setDirty(documentTabs_.activeIndex(),
                                      activeTabDocumentDirty());
    }

    void publishWorkspaceSessionCounters() noexcept
    {
        const bool world2DDirty = isDocumentDirty(WorkspaceMode::World2D);
        const bool world3DDirty = isDocumentDirty(WorkspaceMode::World3D);
        const WorkspaceSessionState* active = activeDocumentSession();

        counters_.finalWorkspaceWorld2D = workspaceMode_ == WorkspaceMode::World2D;
        counters_.world2DDocumentPathConfigured = world2DSession_.hasDocumentPath();
        counters_.world3DDocumentPathConfigured = world3DSession_.hasDocumentPath();
        counters_.world2DDocumentLoaded = world2DSession_.loadedFromPath;
        counters_.world3DDocumentLoaded = world3DSession_.loadedFromPath;
        counters_.world2DDocumentDirty = world2DDirty;
        counters_.world3DDocumentDirty = world3DDirty;
        counters_.world2DSavedSnapshotBytes =
            world2DSession_.savedBaseline.primaryBytes.size();
        counters_.world3DSavedSnapshotBytes =
            world3DSession_.savedBaseline.primaryBytes.size();
        counters_.documentPathConfigured =
            active != nullptr && active->hasDocumentPath();
        counters_.documentLoaded = active != nullptr && active->loadedFromPath;
        counters_.documentDirty = activeTabDocumentDirty();
        counters_.documentSaved = counters_.documentPathConfigured &&
                                  !counters_.documentDirty;
        counters_.savedSnapshotBytes =
            active != nullptr ? active->savedBaseline.byteCount() : 0U;
    }

    [[nodiscard]] std::span<const std::byte> activeDocumentBytes() const noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr) {
            return {};
        }
        switch (tab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            return document_.snapshotBytes();
        case Tina::Editor::EditorDocumentKind::World3D:
            return document3D_.payloadBytes();
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            return tileMapDocument_.rootPayloadBytes();
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            return spriteAnimationDocument_.payloadBytes();
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return {};
        }
    }

    [[nodiscard]] u64 activeDocumentCanonicalByteCount() const noexcept
    {
        u64 total = activeDocumentBytes().size();
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr ||
            tab->key.kind != Tina::Editor::EditorDocumentKind::TileMap2D) {
            return total;
        }
        for (Tina::Core::usize index = 0; index < tileMapDocument_.chunkCount();
             ++index) {
            const auto chunk = tileMapDocument_.chunkPayloadAt(index);
            if (chunk) {
                total += chunk->payloadBytes.size();
            }
        }
        return total;
    }

    [[nodiscard]] u64 activeDocumentRevision() const noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr) {
            return 0U;
        }
        switch (tab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            return document_.revision();
        case Tina::Editor::EditorDocumentKind::World3D:
            return document3D_.revision();
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            return tileMapDocument_.revision();
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            return spriteAnimationDocument_.revision();
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return 0U;
        }
    }

    [[nodiscard]] u64 activeDocumentItemCount() const noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr) {
            return 0U;
        }
        switch (tab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            return document_.entityCount();
        case Tina::Editor::EditorDocumentKind::World3D:
            return document3D_.nodeCount();
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            return tileMapDocument_.layerCount();
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            return spriteAnimationDocument_.frameCount();
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return 0U;
        }
    }

    [[nodiscard]] u64 activeUndoDepth() const noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr) {
            return 0U;
        }
        switch (tab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            return document_.undoDepth();
        case Tina::Editor::EditorDocumentKind::World3D:
            return document3D_.undoDepth();
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            return tileMapDocument_.undoDepth();
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            return spriteAnimationDocument_.undoDepth();
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return 0U;
        }
    }

    [[nodiscard]] u64 activeRedoDepth() const noexcept
    {
        const auto* tab = documentTabs_.activeTab();
        if (tab == nullptr) {
            return 0U;
        }
        switch (tab->key.kind) {
        case Tina::Editor::EditorDocumentKind::World2D:
            return document_.redoDepth();
        case Tina::Editor::EditorDocumentKind::World3D:
            return document3D_.redoDepth();
        case Tina::Editor::EditorDocumentKind::TileMap2D:
            return tileMapDocument_.redoDepth();
        case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
            return spriteAnimationDocument_.redoDepth();
        case Tina::Editor::EditorDocumentKind::AssetInspector:
        default:
            return 0U;
        }
    }

    [[nodiscard]] bool activeCanUndo() const noexcept
    {
        return activeUndoDepth() != 0U;
    }

    [[nodiscard]] bool activeCanRedo() const noexcept
    {
        return activeRedoDepth() != 0U;
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

    [[nodiscard]] static bool inspectorTransformValueDiffers(float current,
                                                             float requested) noexcept
    {
        const float scale = (std::max)({1.0F, std::abs(current),
                                        std::abs(requested)});
        return std::abs(current - requested) > 1.0e-4F * scale;
    }

    [[nodiscard]] static bool inspectorRotationEquivalent(
        const std::array<float, 4>& requested,
        float currentX, float currentY, float currentZ,
        float currentW) noexcept
    {
        const bool same =
            !inspectorTransformValueDiffers(currentX, requested[0]) &&
            !inspectorTransformValueDiffers(currentY, requested[1]) &&
            !inspectorTransformValueDiffers(currentZ, requested[2]) &&
            !inspectorTransformValueDiffers(currentW, requested[3]);
        const bool opposite =
            !inspectorTransformValueDiffers(currentX, -requested[0]) &&
            !inspectorTransformValueDiffers(currentY, -requested[1]) &&
            !inspectorTransformValueDiffers(currentZ, -requested[2]) &&
            !inspectorTransformValueDiffers(currentW, -requested[3]);
        return same || opposite;
    }

    [[nodiscard]] Tina::Core::Status applySelectedTransform(
        const InspectorTransformInput& input)
    {
        const auto finiteOptional = [](const std::optional<float>& value) noexcept {
            return !value.has_value() || std::isfinite(*value);
        };
        if (!finiteOptional(input.positionX) || !finiteOptional(input.positionY) ||
            !finiteOptional(input.positionZ) || !finiteOptional(input.rotationX) ||
            !finiteOptional(input.rotationY) || !finiteOptional(input.rotationZ) ||
            !finiteOptional(input.scaleX) || !finiteOptional(input.scaleY) ||
            !finiteOptional(input.scaleZ)) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Inspector transform contains a non-finite value");
        }
        const auto invalidScale = [](const std::optional<float>& value) noexcept {
            return value.has_value() && *value <= 0.0F;
        };
        if (invalidScale(input.scaleX) || invalidScale(input.scaleY) ||
            (workspaceMode_ == WorkspaceMode::World3D &&
             invalidScale(input.scaleZ))) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Inspector scale values must be greater than zero");
        }
        if (viewportSelectedEntityCount_ == 0U) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Inspector transform requires a viewport scene selection");
        }

        const std::span<const u64> selectedIds{
            viewportSelectedEntityIds_.data(), viewportSelectedEntityCount_};
        for (const u64 stableId : selectedIds) {
            if (stableId == 0U ||
                stableId > (std::numeric_limits<u32>::max)()) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::EntityNotFound,
                    "Inspector transform selection contains an invalid stable ID");
            }
        }
        std::array<bool, Tina::Editor::EditorMarqueeSelectionCapacity>
            matched{};
        const auto selectedIndex = [&](u32 stableId) noexcept
            -> std::optional<Tina::Core::usize> {
            const auto selected = std::find(
                selectedIds.begin(), selectedIds.end(), static_cast<u64>(stableId));
            if (selected == selectedIds.end()) {
                return std::nullopt;
            }
            return static_cast<Tina::Core::usize>(
                std::distance(selectedIds.begin(), selected));
        };

        if (workspaceMode_ == WorkspaceMode::World3D) {
            std::vector<Tina::AssetFormat::PrefabNodeView> views;
            auto prefab = document3D_.parseCurrentPrefab(views);
            if (!prefab) {
                return Tina::Core::failure(std::move(prefab.error()));
            }
            std::vector<Tina::AssetFormat::PrefabNodeDesc> staged;
            bool changed = false;
            try {
                staged.reserve(views.size());
                for (const auto& node : views) {
                    Tina::AssetFormat::PrefabNodeDesc edited{
                        .stableNodeId = node.stableNodeId,
                        .parentIndex = node.parentIndex,
                        .positionX = node.positionX,
                        .positionY = node.positionY,
                        .positionZ = node.positionZ,
                        .rotationX = node.rotationX,
                        .rotationY = node.rotationY,
                        .rotationZ = node.rotationZ,
                        .rotationW = node.rotationW,
                        .scaleX = node.scaleX,
                        .scaleY = node.scaleY,
                        .scaleZ = node.scaleZ,
                        .meshId = node.meshId,
                        .materialId = node.materialId,
                        .visible = node.visible,
                    };
                    const auto index = selectedIndex(node.stableNodeId);
                    if (index.has_value()) {
                        matched[*index] = true;
                        const auto applyValue = [&](const std::optional<float>& value,
                                                    float& field) {
                            if (value.has_value() &&
                                inspectorTransformValueDiffers(field, *value)) {
                                field = *value;
                                changed = true;
                            }
                        };
                        applyValue(input.positionX, edited.positionX);
                        applyValue(input.positionY, edited.positionY);
                        applyValue(input.positionZ, edited.positionZ);

                        if (input.rotationX.has_value() ||
                            input.rotationY.has_value() ||
                            input.rotationZ.has_value()) {
                            EulerDegrees rotation = eulerDegreesFromQuaternion(
                                node.rotationX, node.rotationY,
                                node.rotationZ, node.rotationW);
                            rotation.x = input.rotationX.value_or(rotation.x);
                            rotation.y = input.rotationY.value_or(rotation.y);
                            rotation.z = input.rotationZ.value_or(rotation.z);
                            const std::array requested =
                                quaternionFromEulerDegrees(rotation);
                            if (!inspectorRotationEquivalent(
                                    requested, node.rotationX, node.rotationY,
                                    node.rotationZ, node.rotationW)) {
                                edited.rotationX = requested[0];
                                edited.rotationY = requested[1];
                                edited.rotationZ = requested[2];
                                edited.rotationW = requested[3];
                                changed = true;
                            }
                        }
                        applyValue(input.scaleX, edited.scaleX);
                        applyValue(input.scaleY, edited.scaleY);
                        applyValue(input.scaleZ, edited.scaleZ);
                    }
                    staged.push_back(edited);
                }
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Inspector World3D batch staging allocation failed");
            }
            if (!std::all_of(matched.begin(),
                             matched.begin() + static_cast<std::ptrdiff_t>(
                                 selectedIds.size()),
                             [](bool value) { return value; })) {
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::EntityNotFound,
                    "Inspector selection is absent from the World3D document");
            }
            if (!changed) {
                return Tina::Core::success();
            }
            return document3D_.replace({
                .nodes = std::span<const Tina::AssetFormat::PrefabNodeDesc>{staged},
            });
        }

        std::vector<Tina::AssetFormat::World2DEntityDesc> staged;
        auto snapshot = document_.parseCurrentSnapshot(staged);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        bool changed = false;
        for (auto& entity : staged) {
            const auto index = selectedIndex(entity.stableEntityId);
            if (!index.has_value()) {
                continue;
            }
            matched[*index] = true;
            const auto applyValue = [&](const std::optional<float>& value,
                                        float& field) {
                if (value.has_value() &&
                    inspectorTransformValueDiffers(field, *value)) {
                    field = *value;
                    changed = true;
                }
            };
            applyValue(input.positionX, entity.positionX);
            applyValue(input.positionY, entity.positionY);
            if (input.rotationZ.has_value()) {
                const float normalizedDegrees =
                    std::remainder(*input.rotationZ, 360.0F);
                const float halfRadians =
                    normalizedDegrees * DegreesToRadians * 0.5F;
                const std::array requested{
                    0.0F,
                    0.0F,
                    std::sin(halfRadians),
                    std::cos(halfRadians),
                };
                if (!inspectorRotationEquivalent(
                        requested, entity.rotationX, entity.rotationY,
                        entity.rotationZ, entity.rotationW)) {
                    entity.rotationX = requested[0];
                    entity.rotationY = requested[1];
                    entity.rotationZ = requested[2];
                    entity.rotationW = requested[3];
                    changed = true;
                }
            }
            applyValue(input.scaleX, entity.scaleX);
            applyValue(input.scaleY, entity.scaleY);
        }
        if (!std::all_of(matched.begin(),
                         matched.begin() + static_cast<std::ptrdiff_t>(
                             selectedIds.size()),
                         [](bool value) { return value; })) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Inspector selection is absent from the World2D document");
        }
        if (!changed) {
            return Tina::Core::success();
        }
        return document_.replace({
            .entities =
                std::span<const Tina::AssetFormat::World2DEntityDesc>{staged},
            .gameplaySchema = snapshot->gameplaySchema,
            .gameplayVersion = snapshot->gameplayVersion,
            .gameplayBytes = snapshot->gameplayBytes,
        });
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

        counters_.catalogAssetsLoaded = 0;
        counters_.catalogGpuTextures = 0;
        counters_.catalogGpuMeshes = 0;
        counters_.catalogSpriteBindings = 0;
        counters_.catalogMeshBindings = 0;
        counters_.catalogMaterialBindings = 0;
        counters_.catalogUnresolvedReferences = 0;
        counters_.catalogResolved2DSprites = 0;
        counters_.catalogResolved3DMeshes = 0;
        previewResolvedSpriteCount_ = 0;
        previewResolvedMeshCount_ = 0;

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
        auto snapshot = playSessionActive()
                            ? Tina::AssetFormat::parseWorld2DSnapshot(
                                  playSession_->canonicalBytes(), storage)
                            : document_.parseCurrentSnapshot(storage);
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
            const u32 selectedStableId = stableEntityIdForHierarchyItem(selectionKey_);
            auto animationTarget = std::find_if(
                storage.begin(), storage.end(), [selectedStableId](const auto& entity) {
                    return entity.stableEntityId == selectedStableId &&
                           entity.sprite.has_value();
                });
            if (animationTarget == storage.end()) {
                animationTarget = std::find_if(
                    storage.begin(), storage.end(), [](const auto& entity) {
                        return entity.sprite.has_value();
                    });
            }
            if (animationTarget != storage.end()) {
                animationTarget->sprite->spriteId = animationFrame->spriteId;
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
        auto world = Tina::Scene::World::Create({.entityCapacity = AuthoringEntityCapacity + 1U});
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
        if (bindings->size() != previewSnapshot.entities.size() ||
            world->entityCount() != previewSnapshot.entities.size()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       "editor runtime preview entity count mismatch");
        }
        Tina::Scene::Camera2D editorCameraComponent{};
        Tina::Scene::Vec3 editorCameraPosition{};
        Tina::Scene::Quaternion editorCameraRotation{};
        bool cameraPoseSelected = false;
        bool activeCameraPoseSelected = false;
        for (const auto& binding : *bindings) {
            const Tina::Scene::Camera2D* authoredCamera =
                world->camera2D(binding.entity);
            if (authoredCamera == nullptr) {
                continue;
            }
            const Tina::Scene::WorldTransform* authoredTransform =
                world->worldTransform(binding.entity);
            if (authoredTransform != nullptr &&
                (!cameraPoseSelected ||
                 (authoredCamera->active && !activeCameraPoseSelected))) {
                editorCameraComponent = *authoredCamera;
                editorCameraPosition = authoredTransform->position;
                editorCameraRotation = authoredTransform->rotation;
                cameraPoseSelected = true;
                activeCameraPoseSelected = authoredCamera->active;
            }
            Tina::Scene::Camera2D disabledCamera = *authoredCamera;
            disabledCamera.active = false;
            if (auto status = world->setCamera2D(binding.entity, disabledCamera); !status) {
                return status;
            }
        }
        auto editorCamera = world->createEntity({
            .position = editorCameraPosition,
            .rotation = editorCameraRotation,
        });
        if (!editorCamera) {
            return Tina::Core::failure(std::move(editorCamera.error()));
        }
        editorCameraComponent.active = true;
        editorCameraComponent.normalizedViewport = viewportNormalized_.value_or(
            Tina::Render::RenderNormalizedViewport{});
        if (auto status = world->setCamera2D(*editorCamera, editorCameraComponent);
            !status) {
            return status;
        }
        if (auto status = world->updateWorldTransforms(); !status) {
            return status;
        }
        const u32 selectedStableId = stableEntityIdForHierarchyItem(selectionKey_);
        auto probe = std::find_if(
            bindings->begin(), bindings->end(), [selectedStableId](const auto& binding) {
                return binding.stableEntityId == selectedStableId;
            });
        if (probe == bindings->end() && !bindings->empty()) {
            probe = bindings->begin();
        }
        const Tina::Scene::LocalTransform* probeTransform =
            probe != bindings->end() ? world->localTransform(probe->entity) : nullptr;

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
        counters_.cookPreviewBytes = playSessionActive()
                                         ? playSession_->canonicalBytes().size()
                                         : document_.snapshotBytes().size();
        counters_.selectedTransformPositionX = 0.0F;
        counters_.selectedTransformPositionY = 0.0F;
        counters_.selectedTransformPositionZ = 0.0F;
        counters_.selectedTransformRotationXDegrees = 0.0F;
        counters_.selectedTransformRotationYDegrees = 0.0F;
        counters_.selectedTransformRotationZDegrees = 0.0F;
        counters_.selectedTransformScaleX = 1.0F;
        counters_.selectedTransformScaleY = 1.0F;
        counters_.selectedTransformScaleZ = 1.0F;
        if (probeTransform != nullptr) {
            counters_.selectedTransformPositionX = probeTransform->position.x;
            counters_.selectedTransformPositionY = probeTransform->position.y;
            counters_.selectedTransformPositionZ = probeTransform->position.z;
            const EulerDegrees probeRotation = eulerDegreesFromQuaternion(
                probeTransform->rotation.x, probeTransform->rotation.y,
                probeTransform->rotation.z, probeTransform->rotation.w);
            counters_.selectedTransformRotationXDegrees = probeRotation.x;
            counters_.selectedTransformRotationYDegrees = probeRotation.y;
            counters_.selectedTransformRotationZDegrees = probeRotation.z;
            counters_.selectedTransformScaleX = probeTransform->scale.x;
            counters_.selectedTransformScaleY = probeTransform->scale.y;
            counters_.selectedTransformScaleZ = probeTransform->scale.z;
        }
        previewWorld_.emplace(std::move(*world));
        previewBindings_ = std::move(*bindings);
        previewCamera2D_ = *editorCamera;
        preview3DBindings_.clear();
        previewCamera3D_ = {};
        if (auto status = initializeOrApplyViewportNavigation(); !status) {
            return status;
        }
        previewRevision_ = playSessionActive()
                               ? playSession_->snapshot().sourceDocumentRevision
                               : document_.revision();
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
        auto prefab = playSessionActive()
                          ? Tina::AssetFormat::parsePrefabPayload(
                                playSession_->canonicalBytes(), nodeStorage)
                          : document3D_.parseCurrentPrefab(nodeStorage);
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
        auto world = Tina::Scene::World::Create({.entityCapacity = AuthoringEntityCapacity + 1U});
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
            world->entityCount() != nodeStorage.size()) {
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
        constexpr std::array MeshPreviewColors{
            Tina::Render::RenderLinearColor{
                .red = 0.26F, .green = 0.68F, .blue = 0.92F, .alpha = 1.0F},
            Tina::Render::RenderLinearColor{
                .red = 0.91F, .green = 0.42F, .blue = 0.30F, .alpha = 1.0F},
            Tina::Render::RenderLinearColor{
                .red = 0.31F, .green = 0.82F, .blue = 0.49F, .alpha = 1.0F},
        };
        for (u32 index = 0; index < nodeStorage.size(); ++index) {
            const auto& node = nodeStorage[index];
            const Tina::Scene::EntityId entity = (*entities)[index];
            bindings.push_back({.stableNodeId = node.stableNodeId, .entity = entity});
            if (node.hasMesh) {
                const Tina::Render::RenderLinearColor color =
                    MeshPreviewColors[index % MeshPreviewColors.size()];
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
        if (auto status = world->updateWorldTransforms(); !status) {
            return status;
        }

        Tina::Scene::Vec3 boundsMinimum{};
        Tina::Scene::Vec3 boundsMaximum{};
        bool hasBounds = false;
        for (const Tina::Scene::EntityId entity : *entities) {
            const Tina::Scene::WorldTransform* transform = world->worldTransform(entity);
            if (transform == nullptr) {
                continue;
            }
            if (!hasBounds) {
                boundsMinimum = transform->position;
                boundsMaximum = transform->position;
                hasBounds = true;
                continue;
            }
            boundsMinimum.x = (std::min)(boundsMinimum.x, transform->position.x);
            boundsMinimum.y = (std::min)(boundsMinimum.y, transform->position.y);
            boundsMinimum.z = (std::min)(boundsMinimum.z, transform->position.z);
            boundsMaximum.x = (std::max)(boundsMaximum.x, transform->position.x);
            boundsMaximum.y = (std::max)(boundsMaximum.y, transform->position.y);
            boundsMaximum.z = (std::max)(boundsMaximum.z, transform->position.z);
        }
        const Tina::Scene::Vec3 sceneCenter = hasBounds
            ? (boundsMinimum + boundsMaximum) * 0.5F
            : Tina::Scene::Vec3{};
        const Tina::Scene::Vec3 boundsExtent = hasBounds
            ? (boundsMaximum - boundsMinimum) * 0.5F
            : Tina::Scene::Vec3{};
        const float boundsRadius = std::sqrt(
            (std::max)(0.0F, Tina::Scene::dot(boundsExtent, boundsExtent)));
        const float cameraDistance = (std::max)(
            PreviewWorld3DCameraDistance, boundsRadius * 2.5F);
        constexpr float InitialPitchRadians = 0.32F;
        auto editorCameraEntity = world->createEntity({
            .position = {
                .x = sceneCenter.x,
                .y = sceneCenter.y + std::sin(InitialPitchRadians) * cameraDistance,
                .z = sceneCenter.z + std::cos(InitialPitchRadians) * cameraDistance,
            },
            .rotation = viewportOrbitRotation(0.0F, InitialPitchRadians),
        });
        if (!editorCameraEntity) {
            return Tina::Core::failure(std::move(editorCameraEntity.error()));
        }
        if (auto status = world->setPerspectiveCamera3D(
                *editorCameraEntity,
                Tina::Scene::PerspectiveCamera3D{
                    .verticalFovDegrees = 55.0F,
                    .nearPlaneMeters = 0.1F,
                    .farPlaneMeters = (std::max)(1000.0F, cameraDistance * 4.0F),
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
        const u32 selectedStableId = stableEntityIdForHierarchyItem(selectionKey_);
        auto probe = std::find_if(
            bindings.begin(), bindings.end(), [selectedStableId](const auto& binding) {
                return binding.stableNodeId == selectedStableId;
            });
        if (probe == bindings.end() && !bindings.empty()) {
            probe = bindings.begin();
        }
        const Tina::Scene::LocalTransform* probeTransform =
            probe != bindings.end() ? world->localTransform(probe->entity) : nullptr;

        ++counters_.runtimePreviewInstantiations;
        counters_.documentRevision = document3D_.revision();
        counters_.documentEntityCount = document3D_.nodeCount();
        counters_.documentUndoDepth = document3D_.undoDepth();
        counters_.documentRedoDepth = document3D_.redoDepth();
        counters_.cookPreviewBytes = playSessionActive()
                                         ? playSession_->canonicalBytes().size()
                                         : document3D_.payloadBytes().size();
        counters_.selectedTransformPositionX = 0.0F;
        counters_.selectedTransformPositionY = 0.0F;
        counters_.selectedTransformPositionZ = 0.0F;
        counters_.selectedTransformRotationXDegrees = 0.0F;
        counters_.selectedTransformRotationYDegrees = 0.0F;
        counters_.selectedTransformRotationZDegrees = 0.0F;
        counters_.selectedTransformScaleX = 1.0F;
        counters_.selectedTransformScaleY = 1.0F;
        counters_.selectedTransformScaleZ = 1.0F;
        if (probeTransform != nullptr) {
            counters_.selectedTransformPositionX = probeTransform->position.x;
            counters_.selectedTransformPositionY = probeTransform->position.y;
            counters_.selectedTransformPositionZ = probeTransform->position.z;
            const EulerDegrees probeRotation = eulerDegreesFromQuaternion(
                probeTransform->rotation.x, probeTransform->rotation.y,
                probeTransform->rotation.z, probeTransform->rotation.w);
            counters_.selectedTransformRotationXDegrees = probeRotation.x;
            counters_.selectedTransformRotationYDegrees = probeRotation.y;
            counters_.selectedTransformRotationZDegrees = probeRotation.z;
            counters_.selectedTransformScaleX = probeTransform->scale.x;
            counters_.selectedTransformScaleY = probeTransform->scale.y;
            counters_.selectedTransformScaleZ = probeTransform->scale.z;
        }
        previewWorld_.emplace(std::move(*world));
        previewBindings_.clear();
        preview3DBindings_ = std::move(bindings);
        previewCamera2D_ = {};
        previewCamera3D_ = *editorCameraEntity;
        previewRevision_ = playSessionActive()
                               ? playSession_->snapshot().sourceDocumentRevision
                               : document3D_.revision();
        previewResolvedMeshCount_ = resolvedMeshCount;
        counters_.catalogResolved3DMeshes = resolvedMeshCount;
        counters_.world3DWorkspaceReady = true;
        counters_.runtimePreviewValid = true;
        if (auto status = initializeOrApplyViewportNavigation(); !status) {
            return status;
        }
        return Tina::Core::success();
    }

    [[nodiscard]] Tina::Core::Status publishRuntimePreviewStatus(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        std::string statusPreview = "Runtime preview: ";
        statusPreview += counters_.runtimePreviewValid ? "valid" : "invalid";
        if (playSessionActive()) {
            const auto& play = playSession_->snapshot();
            statusPreview += play.state == Tina::Editor::EditorPlayState::Paused
                                 ? "  |  Paused"
                                 : "  |  Playing";
            statusPreview += "  |  Tick ";
            statusPreview += std::to_string(play.simulationTickCount);
        } else {
            statusPreview += "  |  Editing";
        }
        statusPreview += "  |  Cook ";
        statusPreview += std::to_string(activeDocumentCanonicalByteCount());
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
        return tree.setText(statusPreview_, statusPreview);
    }

    [[nodiscard]] Tina::Core::Status refreshAuthoringUi(Tina::PrimaryWindowUITreeUpdater& tree)
    {
        publishWorkspaceSessionCounters();
        const bool dirty = counters_.documentDirty;
        const bool pathConfigured = counters_.documentPathConfigured;
        const bool selectionEditable = authoringEnabled() && !assetInspectorActive_ &&
                                       stableEntityIdForHierarchyItem(selectionKey_) != 0U &&
                                       !tileMapEditingContext();
        const bool selectionEditable3D = selectionEditable && workspaceMode_ == WorkspaceMode::World3D;

        if (auto status = refreshWorkspaceChrome(tree); !status) {
            return status;
        }
        if (auto status = refreshProjectAssetUi(tree); !status) {
            return status;
        }
        if (auto status = refreshDocumentTabsUi(tree); !status) {
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
        std::string documentStatus;
        if (assetInspectorActive_) {
            const auto* asset = inspectedProjectAsset();
            if (asset != nullptr) {
                documentStatus = "Catalog asset | Type v";
                documentStatus += std::to_string(asset->assetTypeVersion);
                documentStatus += " | Dependencies ";
                documentStatus += std::to_string(asset->dependencyCount);
            } else {
                documentStatus = "Catalog asset unavailable in active project";
            }
        } else {
            documentStatus = "Revision ";
            documentStatus += std::to_string(activeDocumentRevision());
            documentStatus += " | Undo ";
            documentStatus += std::to_string(activeUndoDepth());
            documentStatus += " | Redo ";
            documentStatus += std::to_string(activeRedoDepth());
            documentStatus += pathConfigured ? (dirty ? " | Modified" : " | Saved")
                                             : " | Unsaved";
        }
        if (auto status = tree.setText(inspectorDocument_, documentStatus); !status) {
            return status;
        }
        std::string_view documentKindLabel = "No document";
        std::string_view documentItemLabel = "items";
        if (const auto* activeTab = documentTabs_.activeTab(); activeTab != nullptr) {
            switch (activeTab->key.kind) {
            case Tina::Editor::EditorDocumentKind::World2D:
                documentKindLabel = "World2D v1";
                documentItemLabel = "entities";
                break;
            case Tina::Editor::EditorDocumentKind::World3D:
                documentKindLabel = "Prefab v2";
                documentItemLabel = "nodes";
                break;
            case Tina::Editor::EditorDocumentKind::TileMap2D:
                documentKindLabel = "TileMap v3/v1";
                documentItemLabel = "layers";
                break;
            case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
                documentKindLabel = "SpriteAnimationClip v1";
                documentItemLabel = "frames";
                break;
            case Tina::Editor::EditorDocumentKind::AssetInspector:
                documentKindLabel = "Asset Inspector";
                documentItemLabel = "items";
                break;
            }
        }
        std::string statusDocument{documentKindLabel};
        statusDocument += "  |  ";
        statusDocument += std::to_string(activeDocumentItemCount());
        statusDocument += ' ';
        statusDocument += documentItemLabel;
        statusDocument += "  |  Revision ";
        statusDocument += std::to_string(activeDocumentRevision());
        statusDocument += pathConfigured ? (dirty ? "  |  Modified" : "  |  Saved") : "  |  Unsaved";
        if (auto status = tree.setText(statusDocument_, statusDocument); !status) {
            return status;
        }
        if (auto status = publishRuntimePreviewStatus(tree); !status) {
            return status;
        }
        std::string selectionSummary = "Selected: ";
        selectionSummary += hierarchyDisplayLabel(selectionKey_);
        const u32 selectedEntityId = stableEntityIdForHierarchyItem(selectionKey_);
        if (selectedEntityId != 0U) {
            selectionSummary += "  |  ID ";
            selectionSummary += std::to_string(selectedEntityId);
        }
        if (!assetInspectorActive_ && viewportSelectedEntityCount_ > 1U) {
            selectionSummary += "  |  ";
            selectionSummary += std::to_string(viewportSelectedEntityCount_);
            selectionSummary += " selected  |  Group pivot";
        }
        if (auto status = tree.setText(hierarchySelectionSummary_, selectionSummary); !status) {
            return status;
        }
        std::string statusSelection = "Selected: ";
        if (assetInspectorActive_) {
            const auto* asset = inspectedProjectAsset();
            statusSelection += asset != nullptr ? asset->displayName : "Unavailable Catalog asset";
        } else {
            statusSelection += hierarchyDisplayLabel(selectionKey_);
            if (viewportSelectedEntityCount_ > 1U) {
                statusSelection += "  |  ";
                statusSelection += std::to_string(viewportSelectedEntityCount_);
                statusSelection += " selected  |  Group pivot";
            }
        }
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
        if (auto status = tree.setEnabled(inspectorParentStableId_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(applyTransformButton_, selectionEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(moveButton_, selectionEditable); !status) {
            return status;
        }
        const u32 selectedStableId = stableEntityIdForHierarchyItem(selectionKey_);
        const EditorHierarchyRow* selectedRow = hierarchyRow(selectedStableId);
        const bool sceneEditable = authoringEnabled() && sceneDocumentActive() &&
                                   !assetInspectorActive_;
        const bool sceneItemSelected = selectedStableId != 0U && selectedRow != nullptr;
        if (auto status = tree.setEnabled(addEntityButton_, sceneEditable); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                duplicateEntityButton_, sceneEditable && sceneItemSelected);
            !status) {
            return status;
        }
        const bool canDeleteSelection = sceneEditable && sceneItemSelected &&
            (workspaceMode_ == WorkspaceMode::World2D || document3D_.nodeCount() > 1U);
        if (auto status = tree.setEnabled(deleteEntityButton_, canDeleteSelection); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                reparentEntityRootButton_,
                sceneEditable && sceneItemSelected && selectedRow->parentStableId != 0U);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                reparentEntityButton_, sceneEditable && sceneItemSelected);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                focusEntityButton_, sceneDocumentActive() && sceneItemSelected &&
                                        counters_.runtimePreviewValid);
            !status) {
            return status;
        }
        const bool tileMapControlsEnabled = authoringEnabled() && tileMapEditingContext();
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
        if (auto status = tree.setEnabled(generateTileMapGameplayButton_, tileMapControlsEnabled);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(undoButton_, activeCanUndo()); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(redoButton_, activeCanRedo()); !status) {
            return status;
        }
        const bool documentCanSave = activeDocumentSession() != nullptr;
        if (auto status = tree.setEnabled(toolbarPath_, documentCanSave); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(saveAsButton_, documentCanSave); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                saveButton_, documentCanSave && pathConfigured && dirty);
            !status) {
            return status;
        }
        return refreshPlaySessionUi(tree);
    }

    [[nodiscard]] Tina::Core::Status refreshPlaySessionUi(
        Tina::PrimaryWindowUITreeUpdater& tree)
    {
        const Tina::Editor::EditorPlayState state = playSession_.has_value()
            ? playSession_->snapshot().state
            : Tina::Editor::EditorPlayState::Editing;
        const bool editing = state == Tina::Editor::EditorPlayState::Editing;
        const bool playing = state == Tina::Editor::EditorPlayState::Playing;
        const bool paused = state == Tina::Editor::EditorPlayState::Paused;

        if (auto status = tree.setText(playButton_, paused ? "Resume" : "Play"); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                playButton_,
                (editing && !pendingDirtyCloseKey_.has_value() &&
                 sceneDocumentActive() && playStartReady()) || paused);
            !status) {
            return status;
        }
        if (auto status = tree.setEnabled(pauseButton_, playing); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(stepButton_, paused); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(stopButton_, playing || paused); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(projectAssetList_, editing); !status) {
            return status;
        }
        if (editing) {
            return Tina::Core::success();
        }

        const std::array lockedButtons{
            mode2DButton_, mode3DButton_, undoButton_, redoButton_, saveButton_,
            saveAsButton_, addEntityButton_, duplicateEntityButton_, deleteEntityButton_,
            reparentEntityRootButton_, reparentEntityButton_, inspectorParentStableId_,
            moveButton_,
            translateToolButtons_[0], translateToolButtons_[1], rotateToolButtons_[0],
            rotateToolButtons_[1], scaleToolButtons_[0], scaleToolButtons_[1],
            orientationButton_, snapButton_, tilePaintToolButton_, tileEraseToolButton_,
            paintTileButton_, eraseTileButton_, toggleTileLayerButton_, addTileLayerButton_,
            addObjectLayerButton_, cookTileMapButton_, generateTileMapGameplayButton_,
            animationModeButton_, animationPlayButton_, animationCookButton_,
            animationPreviousButton_, animationNextButton_, animationAddButton_,
            animationDuplicateButton_, animationDeleteButton_, animationMoveLeftButton_,
            animationMoveRightButton_, animationCycleSpriteButton_,
            animationDurationDecreaseButton_, animationDurationIncreaseButton_,
            animationUndoButton_, animationRedoButton_, openProjectAssetButton_,
            refreshProjectCatalogButton_, createProjectButton_, openProjectButton_,
            importSourceButton_, closeDocumentButton_,
        };
        for (const UI::UINodeId button : lockedButtons) {
            if (auto status = tree.setEnabled(button, false); !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : projectFilterButtons_) {
            if (auto status = tree.setEnabled(button, false); !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : documentTabButtons_) {
            if (auto status = tree.setEnabled(button, false); !status) {
                return status;
            }
        }
        for (const UI::UINodeId button : animationFrameButtons_) {
            if (auto status = tree.setEnabled(button, false); !status) {
                return status;
            }
        }
        if (auto status = tree.setEnabled(toolbarPath_, false); !status) {
            return status;
        }
        return Tina::Core::success();
    }

    [[nodiscard]] const Tina::Editor::ProjectAssetDescriptor*
    inspectedProjectAsset() const noexcept
    {
        const auto* activeTab = documentTabs_.activeTab();
        if (activeTab != nullptr &&
            activeTab->key.kind == Tina::Editor::EditorDocumentKind::AssetInspector &&
            activeTab->key.assetId) {
            return projectAssets_.inspectorSnapshot(activeTab->key.assetId);
        }
        return projectAssets_.selectedInspectorSnapshot();
    }

    [[nodiscard]] Tina::Core::Result<InspectorMixedTransformFlags>
    inspectorMixedTransformFlags(u32 primaryStableId) const
    {
        InspectorMixedTransformFlags mixed{};
        if (viewportSelectedEntityCount_ <= 1U || primaryStableId == 0U) {
            return mixed;
        }
        const auto differs = [](float lhs, float rhs) noexcept {
            constexpr float Epsilon = 1.0e-4F;
            return std::abs(lhs - rhs) > Epsilon;
        };
        const auto angleDiffers = [](float lhs, float rhs) noexcept {
            constexpr float Epsilon = 1.0e-4F;
            return std::abs(std::remainder(lhs - rhs, 360.0F)) > Epsilon;
        };
        if (workspaceMode_ == WorkspaceMode::World2D) {
            std::vector<Tina::AssetFormat::World2DEntityDesc> entities;
            auto snapshot = document_.parseCurrentSnapshot(entities);
            if (!snapshot) {
                return Tina::Core::failure(std::move(snapshot.error()));
            }
            const auto primary = std::find_if(
                entities.begin(), entities.end(),
                [primaryStableId](const auto& entity) {
                    return entity.stableEntityId == primaryStableId;
                });
            if (primary == entities.end()) {
                return mixed;
            }
            const float primaryRotation = eulerDegreesFromQuaternion(
                primary->rotationX, primary->rotationY, primary->rotationZ,
                primary->rotationW).z;
            for (Tina::Core::usize index = 0;
                 index < viewportSelectedEntityCount_; ++index) {
                const u32 stableId = static_cast<u32>(
                    viewportSelectedEntityIds_[index]);
                const auto selected = std::find_if(
                    entities.begin(), entities.end(),
                    [stableId](const auto& entity) {
                        return entity.stableEntityId == stableId;
                    });
                if (selected == entities.end()) {
                    mixed[inspectorTransformFieldIndex(
                        InspectorTransformField::PositionX)] = true;
                    mixed[inspectorTransformFieldIndex(
                        InspectorTransformField::PositionY)] = true;
                    mixed[inspectorTransformFieldIndex(
                        InspectorTransformField::RotationZ)] = true;
                    mixed[inspectorTransformFieldIndex(
                        InspectorTransformField::ScaleX)] = true;
                    mixed[inspectorTransformFieldIndex(
                        InspectorTransformField::ScaleY)] = true;
                    continue;
                }
                const float rotation = eulerDegreesFromQuaternion(
                    selected->rotationX, selected->rotationY,
                    selected->rotationZ, selected->rotationW).z;
                mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionX)] =
                    mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionX)] ||
                    differs(selected->positionX, primary->positionX);
                mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionY)] =
                    mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionY)] ||
                    differs(selected->positionY, primary->positionY);
                mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationZ)] =
                    mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationZ)] ||
                    angleDiffers(rotation, primaryRotation);
                mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleX)] =
                    mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleX)] ||
                    differs(selected->scaleX, primary->scaleX);
                mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleY)] =
                    mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleY)] ||
                    differs(selected->scaleY, primary->scaleY);
            }
            return mixed;
        }

        std::vector<Tina::AssetFormat::PrefabNodeView> nodes;
        auto prefab = document3D_.parseCurrentPrefab(nodes);
        if (!prefab) {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        const auto primary = std::find_if(
            nodes.begin(), nodes.end(), [primaryStableId](const auto& node) {
                return node.stableNodeId == primaryStableId;
            });
        if (primary == nodes.end()) {
            return mixed;
        }
        const EulerDegrees primaryRotation = eulerDegreesFromQuaternion(
            primary->rotationX, primary->rotationY, primary->rotationZ,
            primary->rotationW);
        for (Tina::Core::usize index = 0;
             index < viewportSelectedEntityCount_; ++index) {
            const u32 stableId = static_cast<u32>(
                viewportSelectedEntityIds_[index]);
            const auto selected = std::find_if(
                nodes.begin(), nodes.end(), [stableId](const auto& node) {
                    return node.stableNodeId == stableId;
                });
            if (selected == nodes.end()) {
                mixed.fill(true);
                continue;
            }
            const EulerDegrees rotation = eulerDegreesFromQuaternion(
                selected->rotationX, selected->rotationY, selected->rotationZ,
                selected->rotationW);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionX)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionX)] ||
                differs(selected->positionX, primary->positionX);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionY)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionY)] ||
                differs(selected->positionY, primary->positionY);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionZ)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::PositionZ)] ||
                differs(selected->positionZ, primary->positionZ);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationX)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationX)] ||
                angleDiffers(rotation.x, primaryRotation.x);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationY)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationY)] ||
                angleDiffers(rotation.y, primaryRotation.y);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationZ)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::RotationZ)] ||
                angleDiffers(rotation.z, primaryRotation.z);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleX)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleX)] ||
                differs(selected->scaleX, primary->scaleX);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleY)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleY)] ||
                differs(selected->scaleY, primary->scaleY);
            mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleZ)] =
                mixed[inspectorTransformFieldIndex(InspectorTransformField::ScaleZ)] ||
                differs(selected->scaleZ, primary->scaleZ);
        }
        return mixed;
    }

    [[nodiscard]] Tina::Core::Status publishInspector(Tina::PrimaryWindowUITreeUpdater& tree,
                                                      UI::UITreeViewItemKey key)
    {
        if (assetInspectorActive_) {
            if (auto status = tree.setText(inspectorMode_, "Asset"); !status) {
                return status;
            }
            const auto* asset = inspectedProjectAsset();
            if (asset == nullptr) {
                if (auto status = tree.setText(inspectorName_, "Asset unavailable"); !status) {
                    return status;
                }
                if (auto status = tree.setText(inspectorKind_, "Kind: Missing from active Catalog");
                    !status) {
                    return status;
                }
                std::string note = "AssetId: ";
                const auto* activeTab = documentTabs_.activeTab();
                if (activeTab != nullptr && activeTab->key.assetId) {
                    const auto idText = activeTab->key.assetId.canonicalText();
                    note.append(idText.data(), idText.size());
                } else {
                    note += "none";
                }
                note += " | The active project no longer contains this asset";
                if (auto status = tree.setText(inspectorNote_, note); !status) {
                    return status;
                }
                if (auto status = tree.setText(inspectorAssetPath_, "Cooked: unavailable");
                    !status) {
                    return status;
                }
                if (auto status = tree.setText(inspectorDependencySummary_, "Dependencies 0");
                    !status) {
                    return status;
                }
                inspectorDependencyLabels_.clear();
                if (auto status = tree.invalidateListViewItems(inspectorDependencyList_); !status) {
                    return status;
                }
                for (const UI::UINodeId field : {
                         inspectorParentStableId_,
                         inspectorPositionX_, inspectorPositionY_, inspectorPositionZ_,
                         inspectorRotationX_, inspectorRotationY_, inspectorRotationZ_,
                         inspectorScaleX_, inspectorScaleY_, inspectorScaleZ_}) {
                    if (auto status = tree.setText(field, "n/a"); !status) {
                        return status;
                    }
                }
                return Tina::Core::success();
            }
            std::string name = "Asset: ";
            name += asset->displayName;
            if (auto status = tree.setText(inspectorName_, name); !status) {
                return status;
            }
            std::string kind = "Kind: ";
            kind += Tina::Editor::projectAssetKindLabel(asset->assetKind);
            if (auto status = tree.setText(inspectorKind_, kind); !status) {
                return status;
            }
            const auto idText = asset->assetId.canonicalText();
            std::string note = "AssetId: ";
            note.append(idText.data(), idText.size());
            note += " | v";
            note += std::to_string(asset->assetTypeVersion);
            note += " | deps ";
            note += std::to_string(asset->dependencyCount);
            note += " | ";
            note += std::to_string(asset->cookedFileBytes);
            note += " B";
            if (auto status = tree.setText(inspectorNote_, note); !status) {
                return status;
            }
            std::string path = "Cooked: ";
            path += asset->canonicalRelativeCookedPath;
            if (auto status = tree.setText(inspectorAssetPath_, path); !status) {
                return status;
            }
            std::string dependencySummary = "Dependencies ";
            dependencySummary += std::to_string(asset->dependencies.size());
            if (auto status = tree.setText(inspectorDependencySummary_, dependencySummary);
                !status) {
                return status;
            }
            try {
                std::vector<std::string> dependencyLabels;
                dependencyLabels.reserve(asset->dependencies.size());
                for (const Tina::AssetFormat::AssetDependency& dependency :
                     asset->dependencies) {
                    const auto dependencyId = dependency.assetId.canonicalText();
                    std::string label{
                        Tina::Editor::projectAssetKindLabel(dependency.expectedKind)};
                    label += "  ";
                    label.append(dependencyId.data(), 8U);
                    label += "  | Required";
                    if (Tina::AssetFormat::hasDependencyFlag(
                            dependency.flags,
                            Tina::AssetFormat::DependencyFlags::Deferred)) {
                        label += ", deferred load";
                    }
                    dependencyLabels.push_back(std::move(label));
                }
                inspectorDependencyLabels_ = std::move(dependencyLabels);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Project Asset Inspector dependency label allocation failed");
            }
            if (auto status = tree.invalidateListViewItems(inspectorDependencyList_); !status) {
                return status;
            }
            for (const UI::UINodeId field : {
                     inspectorParentStableId_,
                     inspectorPositionX_, inspectorPositionY_, inspectorPositionZ_,
                     inspectorRotationX_, inspectorRotationY_, inspectorRotationZ_,
                     inspectorScaleX_, inspectorScaleY_, inspectorScaleZ_}) {
                if (auto status = tree.setText(field, "n/a"); !status) {
                    return status;
                }
            }
            return Tina::Core::success();
        }
        inspectorDependencyLabels_.clear();
        if (auto status = tree.setText(inspectorAssetPath_, {}); !status) {
            return status;
        }
        if (auto status = tree.setText(inspectorDependencySummary_, {}); !status) {
            return status;
        }
        if (auto status = tree.invalidateListViewItems(inspectorDependencyList_); !status) {
            return status;
        }
        const bool mixedSelection = !tileMapEditingContext() &&
                                    stableEntityIdForHierarchyItem(key) != 0U &&
                                    viewportSelectedEntityCount_ > 1U;
        if (auto status = tree.setText(
                inspectorMode_,
                tileMapEditingContext()
                    ? std::string_view{"Document"}
                    : (mixedSelection ? std::string_view{"Mixed values"}
                                      : std::string_view{"Selection"}));
            !status) {
            return status;
        }
        std::string name = "Name: ";
        name += tileMapEditingContext() ? std::string_view{"TileMap2D"}
                                        : hierarchyDisplayLabel(key);
        if (auto status = tree.setText(inspectorName_, name); !status) {
            return status;
        }
        std::string kind = "Kind: ";
        kind += tileMapEditingContext() ? std::string_view{"TileMap document"}
                                        : hierarchyDisplayKind(key);
        if (auto status = tree.setText(inspectorKind_, kind); !status) {
            return status;
        }
        std::string note = "Note: ";
        note += tileMapEditingContext()
                    ? std::string_view{
                          "Root and streamed chunks publish one canonical revision."}
                    : hierarchyDisplayNote(key);
        if (tileMapEditingContext()) {
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
        const u32 stableEntityId = tileMapEditingContext()
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
        InspectorMixedTransformFlags mixed{};
        if (hasEntity && mixedSelection) {
            auto mixedResult = inspectorMixedTransformFlags(stableEntityId);
            if (!mixedResult) {
                return Tina::Core::failure(std::move(mixedResult.error()));
            }
            mixed = *mixedResult;
        }
        const auto transformText = [&](InspectorTransformField field,
                                       float value) -> std::string {
            if (!hasEntity) {
                return "n/a";
            }
            if (mixed[inspectorTransformFieldIndex(field)]) {
                return "Mixed";
            }
            return std::to_string(value);
        };
        const EditorHierarchyRow* selectedRow = hierarchyRow(stableEntityId);
        if (auto status = tree.setText(
                inspectorParentStableId_,
                hasEntity && selectedRow != nullptr
                    ? std::to_string(selectedRow->parentStableId)
                    : "n/a");
            !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorPositionX_,
                transformText(InspectorTransformField::PositionX, positionX));
            !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorPositionY_,
                transformText(InspectorTransformField::PositionY, positionY));
            !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorPositionZ_,
                transformText(InspectorTransformField::PositionZ, positionZ));
            !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorRotationX_,
                transformText(InspectorTransformField::RotationX,
                              rotationDegrees.x));
            !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorRotationY_,
                transformText(InspectorTransformField::RotationY,
                              rotationDegrees.y));
            !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorRotationZ_,
                transformText(InspectorTransformField::RotationZ,
                              rotationDegrees.z));
            !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorScaleX_,
                transformText(InspectorTransformField::ScaleX, scaleX));
            !status) {
            return status;
        }
        if (auto status = tree.setText(
                inspectorScaleY_,
                transformText(InspectorTransformField::ScaleY, scaleY));
            !status) {
            return status;
        }
        return tree.setText(
            inspectorScaleZ_,
            transformText(InspectorTransformField::ScaleZ, scaleZ));
    }

    [[nodiscard]] UI::UIListViewDataSource inspectorDependencyDataSource() const noexcept
    {
        return UI::UIListViewDataSource{
            .state = this,
            .itemCount = &EditorWorkspaceState::inspectorDependencyItemCount,
            .resolveItem = &EditorWorkspaceState::resolveInspectorDependencyItem,
        };
    }

    [[nodiscard]] static u64 inspectorDependencyItemCount(const void* state) noexcept
    {
        const auto* self = static_cast<const EditorWorkspaceState*>(state);
        return self != nullptr ? self->inspectorDependencyLabels_.size() : 0U;
    }

    static bool resolveInspectorDependencyItem(
        const void* state, u64 logicalIndex,
        UI::UIListViewItemDescriptor& output) noexcept
    {
        const auto* self = static_cast<const EditorWorkspaceState*>(state);
        if (self == nullptr || logicalIndex >= self->inspectorDependencyLabels_.size()) {
            return false;
        }
        output = UI::UIListViewItemDescriptor{
            .key = 20'000U + logicalIndex,
            .label = self->inspectorDependencyLabels_[
                static_cast<Tina::Core::usize>(logicalIndex)],
            .enabled = false,
        };
        return true;
    }

    [[nodiscard]] UI::UIListViewDataSource projectAssetDataSource() const noexcept
    {
        return UI::UIListViewDataSource{
            .state = this,
            .itemCount = &EditorWorkspaceState::projectAssetItemCount,
            .resolveItem = &EditorWorkspaceState::resolveProjectAssetItem,
        };
    }

    [[nodiscard]] static u64 projectAssetItemCount(const void* state) noexcept
    {
        const auto* self = static_cast<const EditorWorkspaceState*>(state);
        return self != nullptr ? self->projectAssets_.visibleItemCount() : 0U;
    }

    static bool resolveProjectAssetItem(const void* state, u64 logicalIndex,
                                        UI::UIListViewItemDescriptor& output) noexcept
    {
        const auto* self = static_cast<const EditorWorkspaceState*>(state);
        if (self == nullptr) {
            return false;
        }
        const auto* asset = self->projectAssets_.visibleItem(
            static_cast<Tina::Core::usize>(logicalIndex));
        if (asset == nullptr) {
            return false;
        }
        output = UI::UIListViewItemDescriptor{
            .key = 10'000U + logicalIndex,
            .label = asset->displayName,
            .enabled = true,
        };
        return true;
    }

    [[nodiscard]] bool hierarchyRowCollapsed(u32 stableId) const noexcept
    {
        return std::find(collapsedHierarchyIds_.begin(),
                         collapsedHierarchyIds_.end(), stableId) !=
               collapsedHierarchyIds_.end();
    }

    [[nodiscard]] const EditorHierarchyRow*
    hierarchyRow(u32 stableId) const noexcept
    {
        const auto found = std::find_if(
            hierarchyRows_.begin(), hierarchyRows_.end(),
            [stableId](const EditorHierarchyRow& row) {
                return row.stableId == stableId;
            });
        return found != hierarchyRows_.end() ? &*found : nullptr;
    }

    [[nodiscard]] std::string_view hierarchyDisplayLabel(
        UI::UITreeViewItemKey key) const noexcept
    {
        const u32 stableId = stableEntityIdForHierarchyItem(key);
        const EditorHierarchyRow* row = hierarchyRow(stableId);
        if (row != nullptr) {
            return row->label;
        }
        return stableId == 0U
                   ? (workspaceMode_ == WorkspaceMode::World2D
                          ? std::string_view{"World2D Scene"}
                          : std::string_view{"World3D Scene"})
                   : std::string_view{"Unavailable scene item"};
    }

    [[nodiscard]] std::string_view hierarchyDisplayKind(
        UI::UITreeViewItemKey key) const noexcept
    {
        if (stableEntityIdForHierarchyItem(key) == 0U) {
            return workspaceMode_ == WorkspaceMode::World2D
                       ? std::string_view{"World2D document"}
                       : std::string_view{"Prefab v2 document"};
        }
        return workspaceMode_ == WorkspaceMode::World2D
                   ? std::string_view{"Entity2D"}
                   : std::string_view{"Node3D"};
    }

    [[nodiscard]] std::string_view hierarchyDisplayNote(
        UI::UITreeViewItemKey key) const noexcept
    {
        if (stableEntityIdForHierarchyItem(key) == 0U) {
            return workspaceMode_ == WorkspaceMode::World2D
                       ? std::string_view{"Canonical World2D scene root."}
                       : std::string_view{"Canonical Prefab v2 scene root."};
        }
        return workspaceMode_ == WorkspaceMode::World2D
                   ? std::string_view{"Stable entity identity with validated components."}
                   : std::string_view{"Stable node identity in the parent-first hierarchy."};
    }

    [[nodiscard]] bool
    hierarchyRowVisible(const EditorHierarchyRow& row) const noexcept
    {
        if (row.stableId == 0) {
            return true;
        }
        if (hierarchyRowCollapsed(0)) {
            return false;
        }
        u32 parentStableId = row.parentStableId;
        for (Tina::Core::usize depth = 0;
             parentStableId != 0 && depth <= hierarchyRows_.size(); ++depth) {
            if (hierarchyRowCollapsed(parentStableId)) {
                return false;
            }
            const EditorHierarchyRow* parent = hierarchyRow(parentStableId);
            if (parent == nullptr) {
                return false;
            }
            parentStableId = parent->parentStableId;
        }
        return parentStableId == 0;
    }

    [[nodiscard]] std::optional<u64>
    visibleHierarchyIndex(u32 stableId) const noexcept
    {
        u64 index = 0;
        for (const EditorHierarchyRow& row : hierarchyRows_) {
            if (!hierarchyRowVisible(row)) {
                continue;
            }
            if (row.stableId == stableId) {
                return index;
            }
            ++index;
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::string hierarchyEntityLabel(
        WorkspaceMode mode, u32 stableId, bool hasRenderable,
        bool hasCamera, bool hasLight)
    {
        std::string label;
        if (mode == WorkspaceMode::World2D) {
            if (hasCamera) {
                label = "Camera2D";
            } else if (hasLight) {
                label = "PointLight2D";
            } else if (hasRenderable) {
                label = "Sprite2D";
            } else {
                label = "Entity2D";
            }
        } else {
            label = hasRenderable ? "Mesh3D" : "Node3D";
        }
        label += " #";
        label += std::to_string(stableId);
        return label;
    }

    [[nodiscard]] Tina::Core::Status rebuildHierarchyModel()
    {
        try {
            std::vector<EditorHierarchyRow> candidate;
            candidate.reserve(AuthoringEntityCapacity + 1U);
            candidate.push_back({
                .key = HierarchyDocumentRootKey,
                .stableId = 0,
                .parentStableId = 0,
                .level = 0,
                .expandable = true,
                .label = workspaceMode_ == WorkspaceMode::World2D
                             ? "World2D Scene"
                             : "World3D Scene",
            });

            struct PendingHierarchyNode final {
                Tina::Core::usize index = 0;
                u32 level = 0;
            };

            if (workspaceMode_ == WorkspaceMode::World2D) {
                std::vector<Tina::AssetFormat::World2DEntityDesc> entities;
                auto snapshot = document_.parseCurrentSnapshot(entities);
                if (!snapshot) {
                    return Tina::Core::failure(std::move(snapshot.error()));
                }

                for (Tina::Core::usize index = 0; index < entities.size(); ++index) {
                    if (entities[index].stableEntityId == 0U ||
                        std::any_of(
                            entities.begin(), entities.begin() +
                                static_cast<std::ptrdiff_t>(index),
                            [&](const auto& previous) {
                                return previous.stableEntityId ==
                                       entities[index].stableEntityId;
                            })) {
                        return Tina::Core::failure(
                            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                            "World2D hierarchy contains a duplicate or zero stable ID");
                    }
                }

                const Tina::Core::usize documentRootIndex = entities.size();
                std::vector<std::vector<Tina::Core::usize>> children(
                    entities.size() + 1U);
                for (Tina::Core::usize index = 0; index < entities.size(); ++index) {
                    Tina::Core::usize parentIndex = documentRootIndex;
                    if (entities[index].parentStableEntityId != 0U) {
                        const auto parent = std::find_if(
                            entities.begin(), entities.end(), [&](const auto& candidateEntity) {
                                return candidateEntity.stableEntityId ==
                                       entities[index].parentStableEntityId;
                            });
                        if (parent == entities.end()) {
                            return Tina::Core::failure(
                                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                                "World2D hierarchy contains a missing parent");
                        }
                        parentIndex = static_cast<Tina::Core::usize>(
                            std::distance(entities.begin(), parent));
                    }
                    children[parentIndex].push_back(index);
                }

                std::vector<PendingHierarchyNode> pending;
                pending.reserve(entities.size());
                for (auto root = children[documentRootIndex].rbegin();
                     root != children[documentRootIndex].rend(); ++root) {
                    pending.push_back({.index = *root, .level = 1U});
                }
                std::vector<u8> published(entities.size(), 0U);
                Tina::Core::usize publishedCount = 0;
                while (!pending.empty()) {
                    const PendingHierarchyNode current = pending.back();
                    pending.pop_back();
                    if (published[current.index] != 0U) {
                        return Tina::Core::failure(
                            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                            "World2D hierarchy would publish an entity more than once");
                    }
                    published[current.index] = 1U;
                    ++publishedCount;
                    const auto& entity = entities[current.index];
                    candidate.push_back({
                        .key = entity.stableEntityId,
                        .stableId = entity.stableEntityId,
                        .parentStableId = entity.parentStableEntityId,
                        .level = current.level,
                        .expandable = !children[current.index].empty(),
                        .label = hierarchyEntityLabel(
                            WorkspaceMode::World2D, entity.stableEntityId,
                            entity.sprite.has_value(), entity.camera.has_value(),
                            entity.pointLight.has_value()),
                    });
                    for (auto child = children[current.index].rbegin();
                         child != children[current.index].rend(); ++child) {
                        pending.push_back({
                            .index = *child,
                            .level = current.level + 1U,
                        });
                    }
                }
                if (publishedCount != entities.size()) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                        "World2D hierarchy contains a parent cycle");
                }
            } else {
                std::vector<Tina::AssetFormat::PrefabNodeView> nodes;
                auto prefab = document3D_.parseCurrentPrefab(nodes);
                if (!prefab) {
                    return Tina::Core::failure(std::move(prefab.error()));
                }

                for (Tina::Core::usize index = 0; index < nodes.size(); ++index) {
                    if (nodes[index].stableNodeId == 0U ||
                        std::any_of(
                            nodes.begin(), nodes.begin() +
                                static_cast<std::ptrdiff_t>(index),
                            [&](const auto& previous) {
                                return previous.stableNodeId == nodes[index].stableNodeId;
                            })) {
                        return Tina::Core::failure(
                            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                            "World3D hierarchy contains a duplicate or zero stable ID");
                    }
                    if (nodes[index].parentIndex < -1 ||
                        (nodes[index].parentIndex >= 0 &&
                         static_cast<Tina::Core::usize>(nodes[index].parentIndex) >=
                             nodes.size())) {
                        return Tina::Core::failure(
                            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                            "World3D hierarchy contains an out-of-range parent index");
                    }
                }

                const Tina::Core::usize documentRootIndex = nodes.size();
                std::vector<std::vector<Tina::Core::usize>> children(nodes.size() + 1U);
                for (Tina::Core::usize index = 0; index < nodes.size(); ++index) {
                    const Tina::Core::usize parentIndex = nodes[index].parentIndex < 0
                        ? documentRootIndex
                        : static_cast<Tina::Core::usize>(nodes[index].parentIndex);
                    children[parentIndex].push_back(index);
                }

                std::vector<PendingHierarchyNode> pending;
                pending.reserve(nodes.size());
                for (auto root = children[documentRootIndex].rbegin();
                     root != children[documentRootIndex].rend(); ++root) {
                    pending.push_back({.index = *root, .level = 1U});
                }
                std::vector<u8> published(nodes.size(), 0U);
                Tina::Core::usize publishedCount = 0;
                while (!pending.empty()) {
                    const PendingHierarchyNode current = pending.back();
                    pending.pop_back();
                    if (published[current.index] != 0U) {
                        return Tina::Core::failure(
                            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                            "World3D hierarchy would publish a node more than once");
                    }
                    published[current.index] = 1U;
                    ++publishedCount;
                    const auto& node = nodes[current.index];
                    const u32 parentStableId = node.parentIndex < 0
                        ? 0U
                        : nodes[static_cast<Tina::Core::usize>(node.parentIndex)]
                              .stableNodeId;
                    candidate.push_back({
                        .key = node.stableNodeId,
                        .stableId = node.stableNodeId,
                        .parentStableId = parentStableId,
                        .level = current.level,
                        .expandable = !children[current.index].empty(),
                        .label = hierarchyEntityLabel(
                            WorkspaceMode::World3D, node.stableNodeId,
                            node.hasMesh, false, false),
                    });
                    for (auto child = children[current.index].rbegin();
                         child != children[current.index].rend(); ++child) {
                        pending.push_back({
                            .index = *child,
                            .level = current.level + 1U,
                        });
                    }
                }
                if (publishedCount != nodes.size()) {
                    return Tina::Core::failure(
                        Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                        "World3D hierarchy contains a parent cycle");
                }
            }

            candidate.front().expandable = candidate.size() > 1U;
            collapsedHierarchyIds_.erase(
                std::remove_if(
                    collapsedHierarchyIds_.begin(),
                    collapsedHierarchyIds_.end(),
                    [&](u32 stableId) {
                        const auto found = std::find_if(
                            candidate.begin(), candidate.end(),
                            [stableId](const EditorHierarchyRow& row) {
                                return row.stableId == stableId && row.expandable;
                            });
                        return found == candidate.end();
                    }),
                collapsedHierarchyIds_.end());
            hierarchyRows_.swap(candidate);
            return Tina::Core::success();
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Editor hierarchy model allocation failed");
        }
    }

    [[nodiscard]] Tina::Core::Status refreshHierarchyTree(
        Tina::PrimaryWindowUITreeUpdater& tree, u32 preferredStableId)
    {
        if (auto status = rebuildHierarchyModel(); !status) {
            return status;
        }
        if (auto status = tree.invalidateTreeViewItems(hierarchyTree_); !status) {
            return status;
        }
        std::string countText =
            std::to_string(hierarchyRows_.empty() ? 0U : hierarchyRows_.size() - 1U);
        countText += " nodes";
        if (auto status = tree.setText(hierarchyCount_, countText); !status) {
            return status;
        }
        const u64 index = visibleHierarchyIndex(preferredStableId).value_or(0U);
        if (auto status = tree.setTreeViewSelectedIndex(hierarchyTree_, index); !status) {
            return status;
        }
        UI::UITreeViewItemDescriptor selected{};
        if (!resolveHierarchyItem(this, index, selected)) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Editor hierarchy could not resolve its selected item");
        }
        selectionKey_ = selected.key;
        synchronizeViewportSelectionFromHierarchy();
        counters_.hierarchyLogicalItems = hierarchyItemCount(this);
        return Tina::Core::success();
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
        if (self == nullptr) {
            return 0;
        }
        u64 count = 0;
        for (const EditorHierarchyRow& row : self->hierarchyRows_) {
            if (self->hierarchyRowVisible(row)) {
                ++count;
            }
        }
        return count;
    }

    static bool resolveHierarchyItem(const void* state, u64 logicalIndex,
                                     UI::UITreeViewItemDescriptor& output) noexcept
    {
        const auto* self = static_cast<const EditorWorkspaceState*>(state);
        if (self == nullptr) {
            return false;
        }
        u64 visibleIndex = 0;
        for (const EditorHierarchyRow& row : self->hierarchyRows_) {
            if (!self->hierarchyRowVisible(row)) {
                continue;
            }
            if (visibleIndex++ != logicalIndex) {
                continue;
            }
            output = UI::UITreeViewItemDescriptor{
                .key = row.key,
                .label = row.label,
                .level = row.level,
                .enabled = true,
                .expandable = row.expandable,
                .expanded = row.expandable &&
                    !self->hierarchyRowCollapsed(row.stableId),
            };
            return true;
        }
        return false;
    }

    static bool setHierarchyExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept
    {
        auto* self = static_cast<EditorWorkspaceState*>(state);
        if (self == nullptr) {
            return false;
        }
        const u32 stableId = stableEntityIdForHierarchyItem(key);
        const auto row = std::find_if(
            self->hierarchyRows_.begin(), self->hierarchyRows_.end(),
            [stableId](const EditorHierarchyRow& candidate) {
                return candidate.stableId == stableId;
            });
        if (row == self->hierarchyRows_.end() || !row->expandable) {
            return false;
        }
        const auto collapsed = std::find(self->collapsedHierarchyIds_.begin(),
                                         self->collapsedHierarchyIds_.end(),
                                         stableId);
        if (expanded) {
            if (collapsed != self->collapsedHierarchyIds_.end()) {
                self->collapsedHierarchyIds_.erase(collapsed);
            }
        } else if (collapsed == self->collapsedHierarchyIds_.end()) {
            if (self->collapsedHierarchyIds_.size() >=
                self->collapsedHierarchyIds_.capacity()) {
                return false;
            }
            self->collapsedHierarchyIds_.push_back(stableId);
        }
        return true;
    }

    EditorLaunchOptions options_;
    LifecycleCounters& counters_;
    Tina::Editor::World2DAuthoringDocument document_;
    Tina::Editor::World3DAuthoringDocument document3D_;
    Tina::Editor::TileMapAuthoringDocument tileMapDocument_;
    Tina::Editor::SpriteAnimationAuthoringDocument spriteAnimationDocument_;
    std::optional<Tina::Editor::EditorPlaySession> playSession_{};
    Tina::Editor::EditorDocumentKey world3DDocumentOwnerKey_{
        .kind = Tina::Editor::EditorDocumentKind::World3D,
    };
    Tina::Editor::EditorDocumentKey tileMapDocumentOwnerKey_{
        .kind = Tina::Editor::EditorDocumentKind::TileMap2D,
        .assetId = editorAssetId(0x42U),
    };
    Tina::Editor::EditorDocumentKey spriteAnimationDocumentOwnerKey_{
        .kind = Tina::Editor::EditorDocumentKind::SpriteAnimation2D,
        .assetId = editorAssetId(0x50U),
    };
    std::array<std::optional<SuspendedTabAuthoringDocument>, DocumentTabSlots>
        suspendedAuthoringDocuments_{};
    std::array<std::optional<WorkspaceSessionState>, DocumentTabSlots>
        tabDocumentSessions_{};
    WorkspaceMode workspaceMode_ = WorkspaceMode::World2D;
    WorkspaceSessionState world2DSession_{};
    WorkspaceSessionState world3DSession_{};
    Tina::Editor::ProjectAssetBrowserModel projectAssets_;
    Tina::Editor::EditorDocumentTabs documentTabs_;
    std::optional<Tina::Editor::EditorProjectWorkspace> pendingProjectSwitch_{};
    std::optional<Tina::Editor::EditorProjectWorkspace> activeProjectWorkspace_{};
    EditorAssetResources& assetResources_;
    EditorRenderDeviceAccess& renderDeviceAccess_;
    std::pmr::unsynchronized_pool_resource sourceImportMemory_{};
    Tina::EditorApp::Detail::EditorSourceImportService sourceImportService_;
    std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit> sourceImportUnits_{};
    std::string sourceImportPointerPathUtf8_{};
    std::string sourceImportPendingStageRootUtf8_{};
    std::string sourceImportSupersededCatalogRootUtf8_{};
    bool sourceImportStartPending_ = false;
    bool sourceImportCatalogCommitted_ = false;
    Tina::EditorApp::Detail::EditorFileDialog fileDialog_{};
    Tina::UI::UIRootOwner uiRoot_{};
    UI::UINodeId hierarchyTree_{};
    UI::UINodeId hierarchyCount_{};
    UI::UINodeId projectAssetList_{};
    UI::UINodeId projectAssetCount_{};
    UI::UINodeId projectAssetSource_{};
    UI::UINodeId inspectorMode_{};
    UI::UINodeId inspectorName_{};
    UI::UINodeId inspectorKind_{};
    UI::UINodeId inspectorNote_{};
    UI::UINodeId inspectorAssetPath_{};
    UI::UINodeId inspectorDependencySummary_{};
    UI::UINodeId inspectorDependencyList_{};
    UI::UINodeId inspectorDocument_{};
    UI::UINodeId hierarchySelectionSummary_{};
    UI::UINodeId viewportPreviewLayer_{};
    std::array<UI::UINodeId, ViewportGridVisualNodeCapacity>
        viewportGridNodes_{};
    std::array<UI::UINodeId, ViewportGizmoVisualNodeCapacity>
        viewportGizmoVisualNodes_{};
    UI::UINodeId viewportMarqueeNode_{};
    UI::UINodeId inspectorPositionX_{};
    UI::UINodeId inspectorPositionY_{};
    UI::UINodeId inspectorPositionZ_{};
    UI::UINodeId inspectorParentStableId_{};
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
    UI::UINodeId dirtyCloseModal_{};
    UI::UINodeId dirtyCloseTitle_{};
    UI::UINodeId dirtyCloseMessage_{};
    UI::UINodeId dirtyClosePath_{};
    UI::UINodeId breadcrumb_{};
    UI::UINodeId viewportTitle_{};
    UI::UINodeId viewportMode_{};
    UI::UINodeId gridStatus_{};
    UI::UINodeId previewAssetStatus_{};
    UI::UINodeId cameraStatus_{};
    UI::UINodeId viewportToolStatus_{};
    UI::UINodeId zoomSlider_{};
    UI::UINodeId zoomValue_{};
    UI::UINodeId frameAllButton_{};
    UI::UINodeId documentFormat_{};
    std::array<UI::UINodeId, 3> componentLabels_{};
    UI::UINodeId mode2DButton_{};
    UI::UINodeId mode3DButton_{};
    UI::UINodeId playButton_{};
    UI::UINodeId pauseButton_{};
    UI::UINodeId stepButton_{};
    UI::UINodeId stopButton_{};
    std::array<UI::UINodeId, 2> selectToolButtons_{};
    std::array<UI::UINodeId, 2> translateToolButtons_{};
    std::array<UI::UINodeId, 2> rotateToolButtons_{};
    std::array<UI::UINodeId, 2> scaleToolButtons_{};
    UI::UINodeId orientationButton_{};
    UI::UINodeId snapButton_{};
    std::array<UI::UINodeId, 3> marqueeModeButtons_{};
    UI::UINodeId moveButton_{};
    UI::UINodeId addEntityButton_{};
    UI::UINodeId duplicateEntityButton_{};
    UI::UINodeId deleteEntityButton_{};
    UI::UINodeId reparentEntityRootButton_{};
    UI::UINodeId reparentEntityButton_{};
    UI::UINodeId focusEntityButton_{};
    UI::UINodeId tilePaintToolButton_{};
    UI::UINodeId tileEraseToolButton_{};
    UI::UINodeId applyTransformButton_{};
    UI::UINodeId undoButton_{};
    UI::UINodeId redoButton_{};
    UI::UINodeId saveButton_{};
    UI::UINodeId saveAsButton_{};
    UI::UINodeId openProjectAssetButton_{};
    UI::UINodeId refreshProjectCatalogButton_{};
    UI::UINodeId createProjectButton_{};
    UI::UINodeId openProjectButton_{};
    UI::UINodeId importSourceButton_{};
    UI::UINodeId closeDocumentButton_{};
    UI::UINodeId dirtyCloseSaveButton_{};
    UI::UINodeId dirtyCloseDiscardButton_{};
    UI::UINodeId dirtyCloseCancelButton_{};
    std::array<UI::UINodeId, 4> projectFilterButtons_{};
    std::array<UI::UINodeId, DocumentTabSlots> documentTabButtons_{};
    UI::UINodeId paintTileButton_{};
    UI::UINodeId eraseTileButton_{};
    UI::UINodeId toggleTileLayerButton_{};
    UI::UINodeId addTileLayerButton_{};
    UI::UINodeId addObjectLayerButton_{};
    UI::UINodeId cookTileMapButton_{};
    UI::UINodeId generateTileMapGameplayButton_{};
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
    std::vector<EditorHierarchyRow> hierarchyRows_{};
    std::vector<u32> collapsedHierarchyIds_{};
    ViewportToolMode viewportToolMode_ = ViewportToolMode::Select;
    Tina::Editor::EditorTransformGizmo viewportTransformGizmo_{};
    ViewportTransformTransaction viewportGizmo_{};
    Tina::Core::usize viewportGizmoVisibleNodeCount_ = 0;
    ViewportNavigationDrag viewportNavigationDrag_{};
    ViewportMarqueeTransaction viewportMarquee_{};
    Tina::Editor::EditorMarqueeSelectionMode marqueeSelectionMode_ =
        Tina::Editor::EditorMarqueeSelectionMode::Replace;
    std::array<u64, Tina::Editor::EditorMarqueeSelectionCapacity>
        viewportSelectedEntityIds_{};
    Tina::Core::usize viewportSelectedEntityCount_ = 0;
    u64 viewportSelectionRevision_ = 0;
    bool preserveViewportSelectionOnHierarchyPublish_ = false;
    std::array<UI::UIRoutedPointerListenerToken, 5> viewportPointerListeners_{};
    bool queuedFirstSelection_ = false;
    bool queuedSecondStyleUpdate_ = false;
    bool pendingAutoTransformInput_ = false;
    u32 autoAuthoringStage_ = 0;
    UI::UILogicalPoint autoGizmoStart_{};
    UI::UILogicalPoint autoGizmoCenter_{};
    u64 automaticPlayStepBaseline_ = 0;
    u64 automaticDemoStartFrame_ = 0;
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
    Tina::Scene::EntityId previewCamera2D_{};
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
    bool previewAssetBindingsRefreshPending_ = false;
    bool catalogRefreshPending_ = false;
    bool projectBrowserUiRefreshPending_ = false;
    bool assetInspectorActive_ = false;
    bool pendingAnimationTimelineRefresh_ = false;
    std::optional<Tina::Editor::EditorViewportNavigation> viewportNavigation_{};
    Tina::Editor::EditorViewport2DNavigationState viewport2DSessionState_{
        .center = {.x = 0.0F, .y = 4.0F},
        .zoom = 1.0F,
    };
    Tina::Editor::EditorViewport3DNavigationState viewport3DSessionState_{
        .target = {.x = 0.0F, .y = 0.0F, .z = -1.0F},
        .distance = PreviewWorld3DCameraDistance,
    };
    Tina::Editor::EditorViewport2DNavigationState viewport2DFrameAllState_{
        .center = {.x = 0.0F, .y = 4.0F},
        .zoom = 1.0F,
    };
    Tina::Editor::EditorViewport3DNavigationState viewport3DFrameAllState_{
        .target = {.x = 0.0F, .y = 0.0F, .z = -1.0F},
        .distance = PreviewWorld3DCameraDistance,
    };
    std::optional<Tina::Editor::EditorViewport3DViewPreset>
        viewport3DViewPreset_{};
    bool viewportViewModeRefreshPending_ = false;
    bool viewport2DNavigationInitialized_ = false;
    bool viewport3DNavigationInitialized_ = false;
    std::array<
        Tina::Editor::EditorViewportNavigationInput,
        Tina::Editor::EditorViewportNavigationLimits::MaximumInputCommandsPerBatch>
        pendingViewportNavigationInputs_{};
    Tina::Core::usize pendingViewportNavigationCount_ = 0;
    bool viewportNavigationQueueOverflowed_ = false;
    Tina::Editor::EditorViewportGrid viewportGrid_{};
    Tina::Core::usize viewportGridVisibleNodeCount_ = 0;
    float viewportZoomPercent_ = 100.0F;
    UI::UILogicalRect viewportLogicalRect_{};
    std::optional<Tina::Render::RenderNormalizedViewport> viewportNormalized_{};
    u32 surfacePixelWidth_ = WindowLogicalWidth;
    u32 surfacePixelHeight_ = WindowLogicalHeight;
    std::string authoringFeedback_ = "One validated revision per command";
    std::optional<u32> pendingSelectionStableId_{};
    std::optional<u64> observedProjectAssetSelectionIndex_{};
    std::vector<std::string> inspectorDependencyLabels_{};
    std::optional<u32> pendingDocumentTabActivation_{};
    std::optional<EditorCommand> pendingEditorCommand_{};
    std::optional<Tina::Editor::EditorDocumentKey> pendingDirtyCloseKey_{};
    std::optional<u32> pendingAnimationFrameSelection_{};
    std::optional<ViewportToolMode> pendingViewportToolMode_{};
    std::optional<UI::UIStraightSrgba8Color> pendingViewportTokenColor_{};
    std::optional<float> pendingViewportSliderValue_{};
    std::optional<float> pendingViewportZoomPercent_{};
    std::optional<Tina::Editor::EditorMarqueeSelectionMode>
        pendingMarqueeSelectionMode_{};
    std::optional<u32> pendingAutoParentStableId_{};
    bool pendingGizmoOrientationToggle_ = false;
    bool pendingGizmoSnapToggle_ = false;
    u64 observedPlaySessionRevision_ = 0;
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
        auto projectAssets = createProjectAssetBrowser(assetResources_);
        if (!projectAssets) {
            return Tina::Core::failure(std::move(projectAssets.error()));
        }
        auto documentTabs = createEditorDocumentTabs(options_.initialWorkspace);
        if (!documentTabs) {
            return Tina::Core::failure(std::move(documentTabs.error()));
        }
        try {
            std::unique_ptr<Tina::IGameState> state =
                std::make_unique<EditorWorkspaceState>(
                    options_, counters_, std::move(initialDocuments->world2D),
                    std::move(initialDocuments->world3D),
                    std::move(initialDocuments->tileMap),
                    std::move(initialDocuments->spriteAnimation),
                    std::move(initialDocuments->world2DSession),
                    std::move(initialDocuments->world3DSession),
                    std::move(*projectAssets), std::move(*documentTabs), assetResources_,
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

[[nodiscard]] Tina::InputActionBinding editorShortcutBinding(
    Tina::Platform::Key key,
    Tina::InputActionId action) noexcept
{
    return Tina::InputActionBinding{
        .input = Tina::PrimaryWindowKeyBinding{.key = key},
        .action = action,
        .domain = Tina::InputActionDomain::Frame,
        .composition = Tina::ActionCompositionMode::StrongestMagnitude,
    };
}

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
    using Key = Tina::Platform::Key;
    config.inputActions.bindings = {
        editorShortcutBinding(Key::LeftControl, EditorShortcutActions::Control),
        editorShortcutBinding(Key::RightControl, EditorShortcutActions::Control),
        editorShortcutBinding(Key::LeftShift, EditorShortcutActions::Shift),
        editorShortcutBinding(Key::RightShift, EditorShortcutActions::Shift),
        editorShortcutBinding(Key::S, EditorShortcutActions::Save),
        editorShortcutBinding(Key::Z, EditorShortcutActions::Undo),
        editorShortcutBinding(Key::Y, EditorShortcutActions::Redo),
        editorShortcutBinding(Key::D, EditorShortcutActions::Duplicate),
        editorShortcutBinding(Key::Delete, EditorShortcutActions::DeleteSelection),
        editorShortcutBinding(Key::Digit1, EditorShortcutActions::Switch2D),
        editorShortcutBinding(Key::Digit2, EditorShortcutActions::Switch3D),
        editorShortcutBinding(Key::Digit0, EditorShortcutActions::FrameAll),
        editorShortcutBinding(Key::F, EditorShortcutActions::FocusSelection),
        editorShortcutBinding(Key::F6, EditorShortcutActions::Play),
        editorShortcutBinding(Key::F7, EditorShortcutActions::Step),
        editorShortcutBinding(Key::F8, EditorShortcutActions::Stop),
        editorShortcutBinding(Key::Escape, EditorShortcutActions::Escape),
    };
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
    if (options.targetFrameCount == 0) {
        if (exitReason != Tina::RunExitReason::PrimaryWindowRequestedClose ||
            counters.stateEnters != 1 || counters.stateExits != 1 ||
            counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
            counters.uiRootsReleased != 1) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Interactive Tina Editor did not complete its window-owned lifecycle");
        }
        return Tina::Core::success();
    }
    const bool world2D = options.initialWorkspace == WorkspaceMode::World2D;
    const bool world2DPathConfigured = !options.world2DDocumentPathUtf8.empty();
    const bool world3DPathConfigured = !options.world3DDocumentPathUtf8.empty();
    const bool projectCatalogConfigured =
        !options.catalogRootUtf8.empty() ||
        !options.sourceImport.projectRootUtf8.empty() ||
        counters.projectSwitches != 0U;
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
                      Tina::AssetFormat::World2DSnapshotWire::EntityBytes +
                  counters.tileMapGameplayBytes
            : Tina::AssetFormat::PrefabWire::HeaderBytes +
                  InitialAuthoringEntityCount * Tina::AssetFormat::PrefabWire::NodeBytes;
    if (exitReason != Tina::RunExitReason::GameRequestedExitAfterCurrentFrame) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Tina Editor stopped for an unexpected reason");
    }
    const bool frameCountMatches = options.sourceImport.importOnStart
                                       ? counters.frameUpdates >= options.targetFrameCount
                                       : counters.frameUpdates == options.targetFrameCount;
    const bool viewportBoundsValid =
        std::isfinite(counters.viewportLogicalX) &&
        std::isfinite(counters.viewportLogicalY) &&
        std::isfinite(counters.viewportLogicalWidth) &&
        std::isfinite(counters.viewportLogicalHeight) &&
        std::isfinite(counters.viewportNormalizedX) &&
        std::isfinite(counters.viewportNormalizedY) &&
        std::isfinite(counters.viewportNormalizedWidth) &&
        std::isfinite(counters.viewportNormalizedHeight) &&
        counters.viewportLogicalWidth > 0.0F &&
        counters.viewportLogicalHeight > 0.0F &&
        counters.viewportNormalizedX >= 0.0F &&
        counters.viewportNormalizedY >= 0.0F &&
        counters.viewportNormalizedWidth > 0.0F &&
        counters.viewportNormalizedHeight > 0.0F &&
        static_cast<double>(counters.viewportNormalizedX) +
                counters.viewportNormalizedWidth <=
            1.0 &&
        static_cast<double>(counters.viewportNormalizedY) +
                counters.viewportNormalizedHeight <=
            1.0;
    const bool selectedTransformFinite =
        std::isfinite(counters.selectedTransformPositionX) &&
        std::isfinite(counters.selectedTransformPositionY) &&
        std::isfinite(counters.selectedTransformPositionZ) &&
        std::isfinite(counters.selectedTransformRotationXDegrees) &&
        std::isfinite(counters.selectedTransformRotationYDegrees) &&
        std::isfinite(counters.selectedTransformRotationZDegrees) &&
        std::isfinite(counters.selectedTransformScaleX) &&
        std::isfinite(counters.selectedTransformScaleY) &&
        std::isfinite(counters.selectedTransformScaleZ) &&
        counters.selectedTransformScaleX > 0.0F &&
        counters.selectedTransformScaleY > 0.0F &&
        counters.selectedTransformScaleZ > 0.0F;
    if (!options.autoDemo) {
        const bool sourceImportFinished =
            !options.sourceImport.importOnStart ||
            (counters.sourceImportStarts == 1U &&
             counters.sourceImportCompletions == 1U &&
             counters.sourceImportFailures == 0U &&
             counters.sourceImportStateCommitted && !counters.sourceImportRunning &&
             !counters.sourceImportReady);
        if (!frameCountMatches || counters.stateEnters != 1U ||
            counters.stateExits != 1U || counters.applicationShutdowns != 1U ||
            counters.uiRootsCreated != 1U || counters.uiRootsReleased != 1U ||
            !counters.selectionVerified || counters.hierarchyLogicalItems == 0U ||
            counters.finalSelectionKey == UI::InvalidUITreeViewItemKey ||
            counters.finalSelectionIndex >= counters.hierarchyLogicalItems ||
            !counters.stylesheetInstalled || counters.styleRegisteredClasses == 0U ||
            counters.styleRegisteredTokens == 0U || counters.styleActiveRules == 0U ||
            !counters.editorActionsReady || !counters.runtimePreviewValid ||
            counters.editorLayoutRegions == 0U || !counters.viewportLayoutReady ||
            !counters.gpuViewportReady || !counters.viewportGridReady ||
            counters.viewportGridRevision == 0U || counters.viewportGridSegments == 0U ||
            counters.viewportGridAxisLines == 0U ||
            !std::isfinite(counters.viewportZoomPercent) ||
            counters.viewportZoomPercent < 25.0F ||
            counters.viewportZoomPercent > 400.0F ||
            (world2D ? !counters.viewportGrid2DObserved
                     : !counters.viewportGrid3DObserved) ||
            !counters.inspectorScrollConfigured || !counters.projectAssetBrowserReady ||
            !counters.documentTabsReady || counters.documentTabCount == 0U ||
            counters.projectAssetVisibleItems == 0U || !counters.catalogReady ||
            counters.documentPathConfigured != documentPathConfigured ||
            counters.documentLoaded != activeDocumentLoaded ||
            counters.documentDirty != activeDocumentDirty ||
            counters.documentSaved != (documentPathConfigured && !activeDocumentDirty) ||
            counters.savedSnapshotBytes != activeSavedSnapshotBytes ||
            !uneditedSessionMatches(documentPathConfigured, activeDocumentLoaded,
                                    activeDocumentDirty, activeSavedSnapshotBytes) ||
            counters.finalWorkspaceWorld2D != world2D ||
            counters.projectCatalogConfigured != projectCatalogConfigured ||
            counters.runtimePreviewInstantiations == 0U ||
            counters.renderExtractions != counters.frameUpdates ||
            (world2D ? !counters.world2DWorkspaceReady
                     : !counters.world3DWorkspaceReady) ||
            !viewportBoundsValid || !selectedTransformFinite ||
            counters.documentRevision == 0U ||
            counters.gpuViewportDocumentRevision != counters.documentRevision ||
            counters.documentEntityCount == 0U || counters.cookPreviewBytes == 0U ||
            !sourceImportFinished) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor finite-frame lifecycle did not satisfy its general invariants");
        }
        return Tina::Core::success();
    }
    if (!frameCountMatches || counters.stateEnters != 1 ||
        counters.stateExits != 1 || counters.applicationShutdowns != 1 || counters.uiRootsCreated != 1 ||
        counters.uiRootsReleased != 1 || !counters.selectionVerified || counters.hierarchyLogicalItems == 0 ||
        !counters.stylesheetInstalled || counters.styleRegisteredClasses != 2 ||
        counters.styleRegisteredTokens != 2 || counters.styleActiveRules != 2 ||
        !counters.editorActionsReady || !counters.runtimePreviewValid ||
        counters.editorLayoutRegions != EditorLayoutRegionCount || !counters.viewportLayoutReady ||
        !counters.gpuViewportReady || !counters.viewportGridReady ||
        counters.viewportGridRevision == 0U || counters.viewportGridSegments == 0U ||
        counters.viewportGridAxisLines == 0U || counters.viewportZoomPercent < 25.0F ||
        counters.viewportZoomPercent > 400.0F ||
        (options.autoDemo &&
         (!counters.viewportGrid2DObserved || !counters.viewportGrid3DObserved)) ||
        (!options.autoDemo &&
         (world2D ? !counters.viewportGrid2DObserved
                  : !counters.viewportGrid3DObserved)) ||
        !counters.inspectorScrollConfigured ||
        !counters.projectAssetBrowserReady || !counters.documentTabsReady ||
        (!projectCatalogConfigured && counters.projectAssetVisibleItems == 0) ||
        counters.documentTabCount != (options.autoDemo ? 5U : 4U) ||
        (options.autoDemo && counters.projectAssetOpenCount != 1U) ||
        (options.autoDemo &&
         (counters.tabOwnedDocumentLoads != 1U ||
          counters.tabOwnedDocumentSwaps != 2U ||
          counters.previewAssetBindingRefreshes !=
              2U + counters.sourceImportCatalogReloads)) ||
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
        counters.runtimePreviewInstantiations < 1 ||
        counters.renderExtractions != counters.frameUpdates ||
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
        std::string message = "Tina Editor lifecycle counters did not match contract";
        if (options.autoDemo) {
            message += ": stage=";
            message += std::to_string(counters.automaticAuthoringStage);
            message += ", frames=";
            message += std::to_string(counters.frameUpdates);
            message += ", entities=";
            message += std::to_string(counters.documentEntityCount);
            message += ", gizmo(begin/preview/commit/reject)=";
            message += std::to_string(counters.viewportGizmoBegins);
            message += "/";
            message += std::to_string(counters.viewportGizmoPreviews);
            message += "/";
            message += std::to_string(counters.viewportGizmoCommits);
            message += "/";
            message += std::to_string(counters.viewportGizmoRejects);
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   std::move(message));
    }
    if (options.sourceImport.importOnStart &&
        (counters.sourceImportStarts != 1U ||
         counters.sourceImportCompletions != 1U ||
         counters.sourceImportFailures != 0U ||
         !counters.sourceImportStateCommitted || counters.sourceImportRunning ||
         counters.sourceImportReady)) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Tina Editor startup source import did not complete and commit state");
    }
    if (!projectCatalogConfigured && counters.projectSwitches == 0U &&
        (counters.catalogEntryCount != 9 || counters.catalogAssetsLoaded != 7 ||
         counters.catalogGpuTextures != 1 || counters.catalogGpuMeshes != 1 ||
         counters.catalogSpriteBindings != 1 || counters.catalogMeshBindings != 1 ||
         counters.catalogMaterialBindings != 1 || counters.catalogUnresolvedReferences != 0 ||
         (world2D && counters.catalogResolved2DSprites != GpuViewportSpriteCount) ||
         (world2D &&
          (counters.tileMapLayerCount != 2 ||
           counters.tileMapChunkCount != InitialTileMapChunkCount ||
           counters.tileMapAuthoredCells != InitialTileMapCellCount ||
           counters.tileMapCookArtifacts != InitialTileMapChunkCount + 1U ||
           counters.tileMapCookPreviewBytes == 0 ||
           counters.tileMapEmittedSprites != InitialTileMapCellCount)) ||
         ((world2D || options.autoDemo) &&
          (counters.animationFrameCount != 4 ||
           counters.animationCookPreviewBytes == 0 ||
           counters.animationPreviewFrameIndex >= counters.animationFrameCount)) ||
         (!world2D && counters.catalogResolved3DMeshes != GpuViewportMeshCount))) {
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
        const bool gizmoDeltaFinite =
            std::isfinite(counters.viewportGizmoWorldDeltaX) &&
            std::isfinite(counters.viewportGizmoWorldDeltaY) &&
            std::isfinite(counters.viewportGizmoWorldDeltaZ) &&
            std::isfinite(counters.viewportGizmoRotationDegrees) &&
            std::isfinite(counters.viewportGizmoScaleFactorX) &&
            std::isfinite(counters.viewportGizmoScaleFactorY) &&
            std::isfinite(counters.viewportGizmoScaleFactorZ);
        constexpr float GizmoDeltaEpsilon = 1.0e-5F;
        const bool gizmoTranslated =
            std::abs(counters.viewportGizmoWorldDeltaX) > GizmoDeltaEpsilon ||
            std::abs(counters.viewportGizmoWorldDeltaY) > GizmoDeltaEpsilon ||
            std::abs(counters.viewportGizmoWorldDeltaZ) > GizmoDeltaEpsilon;
        const bool gizmoRotated =
            std::abs(counters.viewportGizmoRotationDegrees) > GizmoDeltaEpsilon;
        const bool gizmoScaled =
            std::abs(counters.viewportGizmoScaleFactorX - 1.0F) >
                GizmoDeltaEpsilon ||
            std::abs(counters.viewportGizmoScaleFactorY - 1.0F) >
                GizmoDeltaEpsilon ||
            std::abs(counters.viewportGizmoScaleFactorZ - 1.0F) >
                GizmoDeltaEpsilon;
        const bool gameplayGenerationMatches =
            world2D
                ? counters.tileMapGameplayGenerations != 0U &&
                      counters.tileMapGameplaySpawnRecords != 0U &&
                      counters.tileMapGameplayBytes != 0U &&
                      counters.tileMapGameplaySourceRevision != 0U
                : counters.tileMapGameplayGenerations == 0U &&
                      counters.tileMapGameplaySpawnRecords == 0U &&
                      counters.tileMapGameplayBytes == 0U &&
                      counters.tileMapGameplaySourceRevision == 0U;
        if (counters.automaticTransformStableId == 0U ||
            counters.hierarchySelectionChanges == 0U || counters.styleTokenUpdates < 2U ||
            counters.authoringEdits < 3U || counters.inspectorTransactions != 1U ||
            counters.inspectorRejectedTransactions != 0U ||
            counters.viewportGizmoBegins != 3U || counters.viewportGizmoPreviews < 3U ||
            counters.viewportGizmoCommits != 3U ||
            counters.viewportTranslateGizmoCommits != 1U ||
            counters.viewportRotateGizmoCommits != 1U ||
            counters.viewportScaleGizmoCommits != 1U ||
            counters.viewportGroupGizmoCommits != 2U ||
            counters.viewportGroupRotateGizmoCommits != 1U ||
            counters.viewportGroupScaleGizmoCommits != 1U ||
            counters.viewportMaximumGizmoTargets < 2U ||
            counters.viewportGizmoCancels != 0U ||
            counters.viewportGizmoRejects != 0U || !gizmoDeltaFinite ||
            !gizmoTranslated || !gizmoRotated || !gizmoScaled ||
            counters.viewportNavigationBatches < 2U ||
            counters.viewportPan2DInputs == 0U ||
            counters.viewportZoom2DInputs == 0U ||
            counters.viewportOrbit3DInputs == 0U ||
            counters.viewportPan3DInputs == 0U ||
            counters.viewportDolly3DInputs == 0U ||
            counters.viewportMarqueeCommits != 3U ||
            counters.viewportMarqueeReplaceCommits != 1U ||
            counters.viewportMarqueeAddCommits != 1U ||
            counters.viewportMarqueeToggleCommits != 1U ||
            counters.viewportMarqueeSelectionChanges != 3U ||
            counters.viewportMarqueeAddedItems == 0U ||
            counters.viewportMarqueeRemovedItems == 0U ||
            counters.viewportMarqueeMaximumSelection < 2U ||
            counters.viewportMarqueeCancels != 0U ||
            counters.viewportMarqueeRejects != 0U ||
            counters.sceneAddCommands != 1U ||
            counters.sceneDuplicateCommands != 1U ||
            counters.sceneReparentRootCommands != 1U ||
            counters.sceneReparentCommands != 1U ||
            counters.sceneDeleteCommands != 2U ||
            counters.automaticAddedStableId == 0U ||
            counters.automaticDuplicatedStableId == 0U ||
            counters.playStarts != 1U || counters.playPauses != 1U ||
            counters.playStepRequests != 1U || counters.playResumes != 1U ||
            counters.playStops != 1U || counters.playSimulationSteps == 0U ||
            counters.playMaximumSimulationTick == 0U ||
            counters.authoringUndos == 0U ||
            counters.authoringUndos != counters.authoringRedos ||
            counters.animationEdits == 0U || counters.animationUndos == 0U ||
            counters.animationUndos != counters.animationRedos ||
            counters.animationDocumentRevision == 0U ||
            counters.animationFrameCount == 0U ||
            counters.animationPreviewFrameIndex >= counters.animationFrameCount ||
            counters.documentRevision == 0U ||
            counters.gpuViewportDocumentRevision != counters.documentRevision ||
            counters.documentUndoDepth == 0U || counters.documentRedoDepth != 0U ||
            !selectedTransformFinite || !gameplayGenerationMatches) {
            std::string message =
                "Tina Editor automatic authoring demo did not finish: stage=";
            message += std::to_string(counters.automaticAuthoringStage);
            message += "/";
            message += std::to_string(AutomaticAuthoringStageCount);
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                       std::move(message));
        }
        if (counters.automaticFinalSelectionStableId == 0U ||
            counters.finalSelectionKey == UI::InvalidUITreeViewItemKey ||
            stableEntityIdForHierarchyItem(counters.finalSelectionKey) !=
                counters.automaticFinalSelectionStableId ||
            counters.finalSelectionIndex >= counters.hierarchyLogicalItems) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor automatic hierarchy selection did not finish");
        }
        if (counters.workspaceSwitches < 2) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "Tina Editor workspace round-trip did not execute two mode switches");
        }
        if (counters.runtimePreviewInstantiations < 8 ||
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
    std::cout << ",\"projectRoot\":";
    writeJsonString(std::cout, options.sourceImport.projectRootUtf8);
    std::cout << ",\"sourceImportOnStart\":"
              << (options.sourceImport.importOnStart ? "true" : "false")
              << ",\"sourceImportIntendedUnits\":"
              << counters.sourceImportIntendedUnits;
    std::cout << ",\"activeCatalogRoot\":";
    writeJsonString(std::cout, assetResources.catalogRootUtf8);
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
              << ",\"projectAssetSelectionChanges\":"
              << counters.projectAssetSelectionChanges
              << ",\"projectAssetOpenCount\":" << counters.projectAssetOpenCount
              << ",\"projectAssetVisibleItems\":" << counters.projectAssetVisibleItems
              << ",\"projectAssetBrowserReady\":"
              << (counters.projectAssetBrowserReady ? "true" : "false")
              << ",\"documentTabCount\":" << counters.documentTabCount
              << ",\"documentTabSwitches\":" << counters.documentTabSwitches
              << ",\"tabOwnedDocumentLoads\":" << counters.tabOwnedDocumentLoads
              << ",\"tabOwnedDocumentSwaps\":" << counters.tabOwnedDocumentSwaps
              << ",\"previewAssetBindingRefreshes\":"
              << counters.previewAssetBindingRefreshes
              << ",\"documentTabsReady\":"
              << (counters.documentTabsReady ? "true" : "false")
              << ",\"styleRegisteredClasses\":" << counters.styleRegisteredClasses
              << ",\"styleRegisteredTokens\":" << counters.styleRegisteredTokens
              << ",\"styleActiveRules\":" << counters.styleActiveRules
              << ",\"styleRevision\":" << counters.styleRevision
              << ",\"styleTokenUpdates\":" << counters.styleTokenUpdates
              << ",\"editorActionsReady\":"
              << (counters.editorActionsReady ? "true" : "false")
              << ",\"authoringEdits\":" << counters.authoringEdits
              << ",\"authoringUndos\":" << counters.authoringUndos
              << ",\"authoringRedos\":" << counters.authoringRedos
              << ",\"authoringSaves\":" << counters.authoringSaves
              << ",\"inspectorTransactions\":" << counters.inspectorTransactions
              << ",\"inspectorRejectedTransactions\":" << counters.inspectorRejectedTransactions
              << ",\"viewportGizmoBegins\":" << counters.viewportGizmoBegins
              << ",\"viewportGizmoPreviews\":" << counters.viewportGizmoPreviews
              << ",\"viewportGizmoCommits\":" << counters.viewportGizmoCommits
              << ",\"viewportTranslateGizmoCommits\":"
              << counters.viewportTranslateGizmoCommits
              << ",\"viewportRotateGizmoCommits\":"
              << counters.viewportRotateGizmoCommits
              << ",\"viewportScaleGizmoCommits\":"
              << counters.viewportScaleGizmoCommits
              << ",\"viewportGroupGizmoCommits\":"
              << counters.viewportGroupGizmoCommits
              << ",\"viewportGroupRotateGizmoCommits\":"
              << counters.viewportGroupRotateGizmoCommits
              << ",\"viewportGroupScaleGizmoCommits\":"
              << counters.viewportGroupScaleGizmoCommits
              << ",\"viewportMaximumGizmoTargets\":"
              << counters.viewportMaximumGizmoTargets
              << ",\"viewportGizmoCancels\":" << counters.viewportGizmoCancels
              << ",\"viewportGizmoRejects\":" << counters.viewportGizmoRejects
              << ",\"viewportNavigationBatches\":"
              << counters.viewportNavigationBatches
              << ",\"viewportPan2DInputs\":" << counters.viewportPan2DInputs
              << ",\"viewportZoom2DInputs\":" << counters.viewportZoom2DInputs
              << ",\"viewportOrbit3DInputs\":" << counters.viewportOrbit3DInputs
              << ",\"viewportPan3DInputs\":" << counters.viewportPan3DInputs
              << ",\"viewportDolly3DInputs\":" << counters.viewportDolly3DInputs
              << ",\"viewportMarqueeCommits\":" << counters.viewportMarqueeCommits
              << ",\"viewportMarqueeReplaceCommits\":"
              << counters.viewportMarqueeReplaceCommits
              << ",\"viewportMarqueeAddCommits\":"
              << counters.viewportMarqueeAddCommits
              << ",\"viewportMarqueeToggleCommits\":"
              << counters.viewportMarqueeToggleCommits
              << ",\"viewportMarqueeSelectionChanges\":"
              << counters.viewportMarqueeSelectionChanges
              << ",\"viewportMarqueeAddedItems\":"
              << counters.viewportMarqueeAddedItems
              << ",\"viewportMarqueeRemovedItems\":"
              << counters.viewportMarqueeRemovedItems
              << ",\"viewportMarqueeMaximumSelection\":"
              << counters.viewportMarqueeMaximumSelection
              << ",\"viewportMarqueeCancels\":"
              << counters.viewportMarqueeCancels
              << ",\"viewportMarqueeRejects\":"
              << counters.viewportMarqueeRejects
              << ",\"sceneAddCommands\":" << counters.sceneAddCommands
              << ",\"sceneDuplicateCommands\":" << counters.sceneDuplicateCommands
              << ",\"sceneReparentRootCommands\":"
              << counters.sceneReparentRootCommands
              << ",\"sceneReparentCommands\":" << counters.sceneReparentCommands
              << ",\"sceneDeleteCommands\":" << counters.sceneDeleteCommands
              << ",\"automaticAddedStableId\":" << counters.automaticAddedStableId
              << ",\"automaticDuplicatedStableId\":"
              << counters.automaticDuplicatedStableId
              << ",\"automaticAuthoringStage\":"
              << counters.automaticAuthoringStage
              << ",\"playStarts\":" << counters.playStarts
              << ",\"playPauses\":" << counters.playPauses
              << ",\"playStepRequests\":" << counters.playStepRequests
              << ",\"playResumes\":" << counters.playResumes
              << ",\"playStops\":" << counters.playStops
              << ",\"playSimulationSteps\":" << counters.playSimulationSteps
              << ",\"playMaximumSimulationTick\":"
              << counters.playMaximumSimulationTick
              << ",\"viewportGridRevision\":" << counters.viewportGridRevision
              << ",\"viewportGridSegments\":" << counters.viewportGridSegments
              << ",\"viewportGridMinorLines\":" << counters.viewportGridMinorLines
              << ",\"viewportGridMajorLines\":" << counters.viewportGridMajorLines
              << ",\"viewportGridAxisLines\":" << counters.viewportGridAxisLines
              << ",\"viewportZoomPercent\":" << counters.viewportZoomPercent
              << ",\"viewportGridReady\":"
              << (counters.viewportGridReady ? "true" : "false")
              << ",\"viewportGrid2DObserved\":"
              << (counters.viewportGrid2DObserved ? "true" : "false")
              << ",\"viewportGrid3DObserved\":"
              << (counters.viewportGrid3DObserved ? "true" : "false")
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
              << ",\"projectSwitches\":" << counters.projectSwitches
              << ",\"sourceImportStarts\":" << counters.sourceImportStarts
              << ",\"sourceImportCompletions\":" << counters.sourceImportCompletions
              << ",\"sourceImportFailures\":" << counters.sourceImportFailures
              << ",\"sourceImportBusyRetries\":" << counters.sourceImportBusyRetries
              << ",\"sourceImportCatalogReloads\":" << counters.sourceImportCatalogReloads
              << ",\"sourceImportUnitsTotal\":" << counters.sourceImportUnitsTotal
              << ",\"sourceImportUnitsRecooked\":" << counters.sourceImportUnitsRecooked
              << ",\"sourceImportUnitsRemoved\":" << counters.sourceImportUnitsRemoved
              << ",\"sourceImportObjectsReused\":" << counters.sourceImportObjectsReused
              << ",\"sourceImportObjectsCooked\":" << counters.sourceImportObjectsCooked
              << ",\"sourceImportRunning\":"
              << (counters.sourceImportRunning ? "true" : "false")
              << ",\"sourceImportReady\":"
              << (counters.sourceImportReady ? "true" : "false")
              << ",\"sourceImportStateCommitted\":"
              << (counters.sourceImportStateCommitted ? "true" : "false")
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
              << ",\"tileMapGameplayGenerations\":" << counters.tileMapGameplayGenerations
              << ",\"tileMapGameplaySpawnRecords\":" << counters.tileMapGameplaySpawnRecords
              << ",\"tileMapGameplayBytes\":" << counters.tileMapGameplayBytes
              << ",\"tileMapGameplaySourceRevision\":"
              << counters.tileMapGameplaySourceRevision
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
              << ",\"viewportGizmoRotationDegrees\":"
              << counters.viewportGizmoRotationDegrees
              << ",\"viewportGizmoScaleFactorX\":"
              << counters.viewportGizmoScaleFactorX
              << ",\"viewportGizmoScaleFactorY\":"
              << counters.viewportGizmoScaleFactorY
              << ",\"viewportGizmoScaleFactorZ\":"
              << counters.viewportGizmoScaleFactorZ
              << ",\"runtimePreviewInstantiations\":" << counters.runtimePreviewInstantiations
              << ",\"runtimePreviewValid\":" << (counters.runtimePreviewValid ? "true" : "false")
              << ",\"documentRevision\":" << counters.documentRevision
              << ",\"documentEntityCount\":" << counters.documentEntityCount
              << ",\"documentUndoDepth\":" << counters.documentUndoDepth
              << ",\"documentRedoDepth\":" << counters.documentRedoDepth
              << ",\"documentSaved\":" << (counters.documentSaved ? "true" : "false")
              << ",\"documentDirty\":" << (counters.documentDirty ? "true" : "false")
              << ",\"cookPreviewBytes\":" << counters.cookPreviewBytes
              << ",\"selectedTransformPositionX\":" << counters.selectedTransformPositionX
              << ",\"selectedTransformPositionY\":" << counters.selectedTransformPositionY
              << ",\"selectedTransformPositionZ\":" << counters.selectedTransformPositionZ
              << ",\"selectedTransformRotationXDegrees\":"
              << counters.selectedTransformRotationXDegrees
              << ",\"selectedTransformRotationYDegrees\":"
              << counters.selectedTransformRotationYDegrees
              << ",\"selectedTransformRotationZDegrees\":"
              << counters.selectedTransformRotationZDegrees
              << ",\"selectedTransformScaleX\":" << counters.selectedTransformScaleX
              << ",\"selectedTransformScaleY\":" << counters.selectedTransformScaleY
              << ",\"selectedTransformScaleZ\":" << counters.selectedTransformScaleZ
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
