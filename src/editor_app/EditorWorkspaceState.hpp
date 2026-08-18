#pragma once

// Tina Editor desktop composition: shared retained tool chrome backed by
// validated World2D and World3D authoring documents and Scene GPU previews.

#include "EditorAnimationPreview.hpp"
#include "EditorFileDialog.hpp"
#include "EditorSourceImportLaunchOptions.hpp"
#include "EditorSourceImportSelection.hpp"
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
#include <tina/editor/EditorComponentOperations.hpp>
#include <tina/editor/EditorDocumentTabs.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/EditorMarqueeSelection.hpp>
#include <tina/editor/EditorPlaySession.hpp>
#include <tina/editor/EditorProjectCreation.hpp>
#include <tina/editor/EditorSceneOperations.hpp>
#include <tina/editor/EditorTransformGizmo.hpp>
#include <tina/editor/EditorViewportGrid.hpp>
#include <tina/editor/EditorViewportNavigation.hpp>
#include <tina/editor/Navigation2DAuthoringDocument.hpp>
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
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

namespace Tina::EditorApp::WorkspaceInternal {

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
inline constexpr u32 SourceImportMaterializedCapacity = 5;
inline constexpr u32 AuthoringEntityCapacity = 16;
inline constexpr u32 InitialAuthoringEntityCount = 5;
inline constexpr u32 AnimationVisibleFrameSlots = 6;
inline constexpr u32 DocumentTabSlots = 6;
inline constexpr u32 AutomaticAuthoringStageCount = 51;
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
inline constexpr Tina::Core::u8 NavigationPreviewAssetMarker = 0x52U;
inline constexpr u32 EditorGameplaySpawnSchema = 0x5453504EU;
inline constexpr u32 EditorGameplaySpawnVersion = 1;
inline constexpr u32 EditorPlayerArchetypeId = 1;
inline constexpr u32 EditorCrateArchetypeId = 2;
inline constexpr float PreviewWorldWidth = 16.0F;
inline constexpr float PreviewWorldHeight = 9.0F;
inline constexpr float PreviewWorld3DCameraDistance = 8.0F;
inline constexpr float DegreesToRadians = 0.01745329251994329577F;
inline constexpr float RadiansToDegrees = 57.295779513082320876F;
inline constexpr Tina::Core::usize ViewportGridVisualNodeCapacity =
    Tina::Editor::EditorViewportGridSegmentCapacity;
inline constexpr Tina::Core::usize ViewportGizmoVisualNodeCapacity = 256;
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

}
// namespace EditorShortcutActions
inline 
[[nodiscard]] Tina::Core::AssetId editorAssetId(u8 marker)
{
    Tina::Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(marker);
    return *Tina::Core::AssetId::fromBytes(bytes);
}
inline void appendGameplayU32(std::vector<std::byte>& bytes, u32 value)
{
    for (u32 shift = 0; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}
[[nodiscard]] inline Tina::Core::Result<std::vector<std::byte>>
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
[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}
[[nodiscard]] constexpr Tina::AssetFormat::TargetPlatform editorTargetPlatform() noexcept;
[[nodiscard]] inline bool pathComponentEquals(const std::filesystem::path& left,
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
[[nodiscard]] inline bool pathIsSameOrDescendant(const std::filesystem::path& candidate,
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
[[nodiscard]] inline bool pathsReferToSameLocation(const std::filesystem::path& left,
                                            const std::filesystem::path& right) noexcept
{
    return pathIsSameOrDescendant(left, right) &&
           pathIsSameOrDescendant(right, left);
}
[[nodiscard]] inline Tina::Core::Status validatePhysicalProjectDirectory(
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
[[nodiscard]] inline Tina::Core::Result<Tina::Editor::EditorProjectWorkspace>
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
struct EditorAuthoringCachePaths final {
    std::filesystem::path root{};
    std::filesystem::path stages{};
    std::filesystem::path activeCatalogPointer{};
};
struct ResolvedEditorProjectCatalog final {
    std::string catalogRootUtf8{};
    std::string sourceImportCatalogRootUtf8{};
    std::string sourceImportStatePathUtf8{};
    std::string authoringCatalogRootUtf8{};
    std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit> sourceImportUnits{};
};
[[nodiscard]] inline EditorSourceImportCachePaths sourceImportCachePaths(
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
[[nodiscard]] inline EditorAuthoringCachePaths authoringCachePaths(
    const Tina::Editor::EditorProjectWorkspace& workspace)
{
    const auto projectRoot = std::filesystem::u8path(
        workspace.projectRootUtf8().begin(), workspace.projectRootUtf8().end());
    const auto root = projectRoot / ".tina" / "cache" / "authoring";
    return {
        .root = root,
        .stages = root / "stages",
        .activeCatalogPointer = root / "active-catalog.path",
    };
}
[[nodiscard]] inline Tina::Core::Status ensurePhysicalDirectory(
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
[[nodiscard]] inline Tina::Core::Result<EditorSourceImportCachePaths>
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
[[nodiscard]] inline Tina::Core::Result<EditorSourceImportStagePaths>
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
[[nodiscard]] inline Tina::Core::Result<std::filesystem::path>
createAuthoringStageRoot(const Tina::Editor::EditorProjectWorkspace& workspace)
{
    try {
        const auto paths = authoringCachePaths(workspace);
        const auto projectRoot = std::filesystem::u8path(
            workspace.projectRootUtf8().begin(), workspace.projectRootUtf8().end());
        std::error_code canonicalError;
        const auto physicalProject = std::filesystem::weakly_canonical(projectRoot, canonicalError);
        if (canonicalError) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                       "Editor could not resolve the authoring project root");
        }
        const std::array directories{
            projectRoot / ".tina", projectRoot / ".tina" / "cache", paths.root, paths.stages,
        };
        constexpr std::array<std::string_view, directories.size()> labels{
            "toolRoot", "cacheRoot", "authoringRoot", "authoringStages",
        };
        for (u32 index = 0; index < directories.size(); ++index) {
            if (auto status = ensurePhysicalDirectory(directories[index], physicalProject, labels[index]);
                !status) {
                return Tina::Core::failure(std::move(status.error()));
            }
        }

        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        constexpr u32 MaximumAttempts = 256;
        for (u32 attempt = 0; attempt < MaximumAttempts; ++attempt) {
            const auto candidate = paths.stages /
                                   ("stage_" + std::to_string(seed) + "_" +
                                    std::to_string(attempt));
            std::error_code createError;
            if (!std::filesystem::create_directory(candidate, createError)) {
                if (!createError) {
                    continue;
                }
                Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                        "Editor could not reserve an authoring stage path"};
                error.setNativeCode(createError.value());
                return Tina::Core::failure(std::move(error));
            }
            std::error_code physicalError;
            const auto physicalStages = std::filesystem::weakly_canonical(paths.stages, physicalError);
            const auto physicalCandidate = std::filesystem::weakly_canonical(candidate, physicalError);
            if (physicalError || physicalCandidate.parent_path() != physicalStages) {
                std::error_code cleanupError;
                std::filesystem::remove(candidate, cleanupError);
                return Tina::Core::failure(Tina::Core::CoreErrorCode::PermissionDenied,
                                           "Editor authoring stage escaped its project cache");
            }
            return candidate;
        }
        return Tina::Core::failure(Tina::Core::CoreErrorCode::AlreadyExists,
                                   "Editor exhausted authoring stage path attempts");
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Editor authoring stage path allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                "Editor authoring stage filesystem operation failed"};
        error.setNativeCode(exception.code().value());
        return Tina::Core::failure(std::move(error));
    }
}
[[nodiscard]] inline Tina::Core::Result<std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit>>
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
        } else if (metadataUnit->importerKind ==
                   static_cast<u32>(Tina::Asset::SourceImporterKind::Texture)) {
            kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::Texture;
        } else if (metadataUnit->importerKind ==
                   static_cast<u32>(Tina::Asset::SourceImporterKind::Audio)) {
            kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::Audio;
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
[[nodiscard]] inline Tina::Core::Result<ResolvedEditorProjectCatalog> resolveSourceImportCatalog(
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
                .sourceImportCatalogRootUtf8 = std::string{workspace.cookedCatalogRootUtf8()},
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
                .sourceImportCatalogRootUtf8 = std::string{workspace.cookedCatalogRootUtf8()},
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
            .sourceImportCatalogRootUtf8 = pathToUtf8(catalogRoot.lexically_normal()),
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
[[nodiscard]] inline Tina::Core::Result<ResolvedEditorProjectCatalog> resolveProjectCatalog(
    const Tina::Editor::EditorProjectWorkspace& workspace)
{
    auto resolved = resolveSourceImportCatalog(workspace);
    if (!resolved) {
        return Tina::Core::failure(std::move(resolved.error()));
    }
    try {
        const auto cache = authoringCachePaths(workspace);
        std::error_code pointerError;
        const auto pointerStatus = std::filesystem::symlink_status(
            cache.activeCatalogPointer, pointerError);
        if (pointerError == std::errc::no_such_file_or_directory ||
            (!pointerError && !std::filesystem::exists(pointerStatus))) {
            return resolved;
        }
        if (pointerError || !std::filesystem::is_regular_file(pointerStatus)) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "Editor active authoring Catalog pointer is invalid");
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
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "Editor active authoring Catalog pointer is not strict UTF-8");
        }
        const auto catalogRoot = std::filesystem::u8path(pointerText.begin(), pointerText.end());
        const auto stageRoot = catalogRoot.parent_path().lexically_normal();
        if (!catalogRoot.is_absolute() || stageRoot.empty()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "Editor active authoring Catalog pointer must be absolute");
        }
        const auto projectRoot = std::filesystem::u8path(
            workspace.projectRootUtf8().begin(), workspace.projectRootUtf8().end());
        std::error_code canonicalError;
        const auto physicalProject = std::filesystem::weakly_canonical(projectRoot, canonicalError);
        const auto physicalStages = std::filesystem::weakly_canonical(cache.stages, canonicalError);
        const auto physicalStage = std::filesystem::weakly_canonical(stageRoot, canonicalError);
        const auto physicalCatalog = std::filesystem::weakly_canonical(catalogRoot, canonicalError);
        if (canonicalError || !pathIsSameOrDescendant(physicalStages, physicalProject) ||
            physicalStage.parent_path() != physicalStages ||
            !pathIsSameOrDescendant(physicalCatalog, physicalStage)) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::PermissionDenied,
                                       "Editor active authoring Catalog escaped its project cache");
        }
        Tina::Asset::CatalogPackageOpenConfig openConfig{};
        openConfig.manifest.catalog.maxEntries = 4096;
        openConfig.manifest.catalog.maxDependencies = 16384;
        openConfig.manifest.catalog.maxDependenciesPerAsset = 4096;
        openConfig.manifest.catalog.memoryResource = &validationMemory;
        openConfig.validation.file.memoryResource = &validationMemory;
        openConfig.validation.verifyTypedPayload = true;
        if (auto catalog = Tina::Asset::openCatalogPackage(pathToUtf8(catalogRoot), openConfig);
            !catalog) {
            return Tina::Core::failure(std::move(catalog.error()));
        }
        resolved->catalogRootUtf8 = pathToUtf8(catalogRoot.lexically_normal());
        resolved->authoringCatalogRootUtf8 = resolved->catalogRootUtf8;
        return resolved;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Editor authoring Catalog resolution allocation failed");
    } catch (const std::filesystem::filesystem_error& exception) {
        Tina::Core::Error error{Tina::Core::CoreErrorCode::Io,
                                "Editor authoring Catalog resolution failed"};
        error.setNativeCode(exception.code().value());
        return Tina::Core::failure(std::move(error));
    }
}
[[nodiscard]] inline Tina::Core::Result<std::filesystem::path> createUniqueEditorTempDirectory()
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
[[nodiscard]] inline Tina::Core::Result<Tina::Asset::CatalogCookRequest>
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
    std::vector<std::filesystem::path> ownedCatalogStageRoots{};
    std::string catalogRootUtf8{};
    std::string sourceImportCatalogRootUtf8{};
    std::string sourceImportStatePathUtf8{};
    std::string authoringCatalogRootUtf8{};
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
        for (const auto& stageRoot : ownedCatalogStageRoots) {
            std::error_code cleanupError;
            std::filesystem::remove_all(stageRoot, cleanupError);
        }
        if (!ownedWorkRoot.empty()) {
            std::error_code cleanupError;
            std::filesystem::remove_all(ownedWorkRoot, cleanupError);
        }
    }
};
[[nodiscard]] inline Tina::Core::Status prepareEditorCatalog(const EditorLaunchOptions& options,
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
        resources.sourceImportCatalogRootUtf8 =
            std::move(resolvedCatalog->sourceImportCatalogRootUtf8);
        resources.sourceImportStatePathUtf8 =
            std::move(resolvedCatalog->sourceImportStatePathUtf8);
        resources.authoringCatalogRootUtf8 =
            std::move(resolvedCatalog->authoringCatalogRootUtf8);
        resources.initialSourceImportUnits =
            std::move(resolvedCatalog->sourceImportUnits);
        resources.initialProjectWorkspace.emplace(std::move(*workspace));
    } else if (resources.projectCatalogConfigured) {
        resources.catalogRootUtf8 = options.catalogRootUtf8;
        resources.sourceImportCatalogRootUtf8 = options.catalogRootUtf8;
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
    u64 navigationBakeRevision = 0;
    u64 navigationSourceTileMapRevision = 0;
    u64 navigationPayloadBytes = 0;
    u64 navigationCatalogPublishes = 0;
    u64 animationDocumentRevision = 0;
    u64 animationFrameCount = 0;
    u64 animationCookPreviewBytes = 0;
    u64 animationPreviewFrameIndex = 0;
    u64 animationEventCount = 0;
    u64 animationSelectedEventIndex = 0;
    u64 animationEventEdits = 0;
    u64 animationEventRejectedEdits = 0;
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
    bool navigationBakeReady = false;
    bool navigationBakeDirty = true;
};
enum class EditorCommand : u32 {
    SwitchToWorld2D,
    SwitchToWorld3D,
    MoveSelectedPositiveX,
    ApplyTransform,
    ComponentAddSprite,
    ComponentAddCamera,
    ComponentAddPointLight,
    ComponentAddShadowOccluder,
    ComponentAddSpriteAnimation,
    ComponentAddMeshRenderer,
    ComponentRemoveSprite,
    ComponentRemoveCamera,
    ComponentRemovePointLight,
    ComponentRemoveShadowOccluder,
    ComponentRemoveSpriteAnimation,
    ComponentRemoveMeshRenderer,
    ComponentApplySprite,
    ComponentApplyCamera,
    ComponentApplyPointLight,
    ComponentApplyShadowOccluder,
    ComponentApplySpriteAnimation,
    ComponentToggleSpriteVisible,
    ComponentToggleCameraActive,
    ComponentTogglePointLightActive,
    ComponentToggleShadowOccluderActive,
    ComponentToggleSpriteAnimationAutoPlay,
    ComponentToggleMeshVisible,
    ComponentAssignSprite,
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
    BakeNavigation2D,
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
    AnimationPreviousEvent,
    AnimationNextEvent,
    AnimationAddEvent,
    AnimationApplyEvent,
    AnimationRemoveEvent,
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
    RemoveSelectedSourceImport,
    OpenSelectedProjectAsset,
    CloseActiveDocument,
    DirtyCloseSave,
    DirtyCloseDiscard,
    DirtyCloseCancel,
};
[[nodiscard]] inline bool editorShortcutStarted(
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
inline void writeJsonString(std::ostream& output, std::string_view value)
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
inline void writeError(const Tina::Core::Error& error)
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
[[nodiscard]] inline bool parseFiniteFloat(std::string_view text, float& value) noexcept
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
[[nodiscard]] inline Tina::Core::Result<std::optional<float>>
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
[[nodiscard]] inline std::array<float, 4> quaternionFromEulerDegrees(EulerDegrees degrees) noexcept
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
[[nodiscard]] inline EulerDegrees eulerDegreesFromQuaternion(float rotationX, float rotationY,
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
[[nodiscard]] inline Tina::Core::Result<EditorLaunchOptions> parseOptions(int argumentCount, char** arguments)
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
[[nodiscard]] inline UI::UILayoutStyle percentSize(float widthPercent, float heightPercent) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(widthPercent);
    style.size.height = UI::UILayoutLength::Percent(heightPercent);
    return style;
}
[[nodiscard]] inline UI::UILayoutStyle flexChild(float grow, float basisPercent, float heightPercent) noexcept
{
    UI::UILayoutStyle style = percentSize(basisPercent, heightPercent);
    style.flexItem.grow = grow;
    style.flexItem.shrink = 1.0F;
    style.flexItem.basis = UI::UILayoutLength::Percent(basisPercent);
    return style;
}
[[nodiscard]] inline UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}
[[nodiscard]] inline UI::UILayoutStyle fillWidth(float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Percent(100.0F);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}
[[nodiscard]] inline UI::UILayoutStyle growingRegion() noexcept
{
    UI::UILayoutStyle style = percentSize(100.0F, 100.0F);
    style.flexItem.grow = 1.0F;
    style.flexItem.shrink = 1.0F;
    style.flexItem.basis = UI::UILayoutLength::Px(0.0F);
    return style;
}
[[nodiscard]] inline UI::UILayoutStyle dirtyCloseModalLayout(
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
[[nodiscard]] inline UI::UILayoutStyle boundedDock(float basisPercent, float minimumWidth,
                                            float maximumWidth) noexcept
{
    UI::UILayoutStyle style = flexChild(0.0F, basisPercent, 100.0F);
    style.minMax.minWidth = UI::UILayoutLength::Px(minimumWidth);
    style.minMax.maxWidth = UI::UILayoutLength::Px(maximumWidth);
    return style;
}
[[nodiscard]] inline u32 stableEntityIdForHierarchyItem(UI::UITreeViewItemKey key) noexcept
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
[[nodiscard]] inline Tina::Core::Result<SavedDocumentBaseline>
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
[[nodiscard]] inline Tina::Core::Result<SavedDocumentBaseline>
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
[[nodiscard]] inline Tina::Core::Result<SavedDocumentBaseline>
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
[[nodiscard]] inline Tina::Core::Result<SavedDocumentBaseline>
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
[[nodiscard]] inline bool baselineBytesMatch(const std::vector<std::byte>& baseline,
                                      std::span<const std::byte> current) noexcept
{
    return baseline.size() == current.size() &&
           std::equal(baseline.begin(), baseline.end(), current.begin());
}
[[nodiscard]] inline bool savedBaselineMatches(
    const SavedDocumentBaseline& baseline,
    const Tina::Editor::World2DAuthoringDocument& document) noexcept
{
    return baseline.captured &&
           baselineBytesMatch(baseline.primaryBytes, document.snapshotBytes());
}
[[nodiscard]] inline bool savedBaselineMatches(
    const SavedDocumentBaseline& baseline,
    const Tina::Editor::World3DAuthoringDocument& document) noexcept
{
    return baseline.captured &&
           baselineBytesMatch(baseline.primaryBytes, document.payloadBytes());
}
[[nodiscard]] inline bool savedBaselineMatches(
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
[[nodiscard]] inline bool savedBaselineMatches(
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

// Owner of every document save session (the two pinned workspace sessions plus
// the bounded per-tab sessions) and of the suspended per-tab authoring
// documents. Sessions are found by document key; the two workspace sessions
// are fixed and can only be replaced through workspaceSession() references.
class EditorDocumentSessionStore final {
  public:
    EditorDocumentSessionStore(WorkspaceSessionState world2DSession,
                               WorkspaceSessionState world3DSession) noexcept
        : world2DSession_(std::move(world2DSession)),
          world3DSession_(std::move(world3DSession))
    {
    }

    [[nodiscard]] WorkspaceSessionState& workspaceSession(WorkspaceMode mode) noexcept
    {
        return mode == WorkspaceMode::World2D ? world2DSession_ : world3DSession_;
    }
    [[nodiscard]] const WorkspaceSessionState& workspaceSession(WorkspaceMode mode) const noexcept
    {
        return mode == WorkspaceMode::World2D ? world2DSession_ : world3DSession_;
    }

    [[nodiscard]] WorkspaceSessionState* find(Tina::Editor::EditorDocumentKey key) noexcept
    {
        if (key == world2DSession_.key) {
            return &world2DSession_;
        }
        if (key == world3DSession_.key) {
            return &world3DSession_;
        }
        for (auto& slot : tabSessions_) {
            if (slot.has_value() && slot->key == key) {
                return &*slot;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const WorkspaceSessionState* find(Tina::Editor::EditorDocumentKey key) const noexcept
    {
        return const_cast<EditorDocumentSessionStore*>(this)->find(key);
    }

    [[nodiscard]] Tina::Core::Status install(WorkspaceSessionState session)
    {
        if (find(session.key) != nullptr) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Editor document already owns a save session");
        }
        for (auto& slot : tabSessions_) {
            if (!slot.has_value()) {
                slot.emplace(std::move(session));
                return Tina::Core::success();
            }
        }
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabCapacityExceeded,
            "Editor document save session capacity is exhausted");
    }

    void discard(Tina::Editor::EditorDocumentKey key) noexcept
    {
        for (auto& slot : tabSessions_) {
            if (slot.has_value() && slot->key == key) {
                slot.reset();
                return;
            }
        }
    }

    [[nodiscard]] bool pathOwnedByOtherSession(
        std::string_view path, Tina::Editor::EditorDocumentKey activeKey) const noexcept
    {
        const auto ownsPath = [path, activeKey](const WorkspaceSessionState& session) {
            return session.key != activeKey && session.documentPathUtf8 == path;
        };
        if (ownsPath(world2DSession_) || ownsPath(world3DSession_)) {
            return true;
        }
        return std::any_of(tabSessions_.begin(), tabSessions_.end(),
                           [&](const auto& slot) {
                               return slot.has_value() && ownsPath(*slot);
                           });
    }

    [[nodiscard]] SuspendedTabAuthoringDocument* findSuspended(
        Tina::Editor::EditorDocumentKey key) noexcept
    {
        for (auto& slot : suspendedDocuments_) {
            if (slot.has_value() && slot->key == key) {
                return &*slot;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const SuspendedTabAuthoringDocument* findSuspended(
        Tina::Editor::EditorDocumentKey key) const noexcept
    {
        return const_cast<EditorDocumentSessionStore*>(this)->findSuspended(key);
    }

    [[nodiscard]] Tina::Core::Status storeSuspended(SuspendedTabAuthoringDocument suspended)
    {
        for (auto& slot : suspendedDocuments_) {
            if (!slot.has_value()) {
                try {
                    slot.emplace(std::move(suspended));
                } catch (const std::bad_alloc&) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::OutOfMemory,
                        "Editor authoring tab state allocation failed");
                }
                return Tina::Core::success();
            }
        }
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentTabCapacityExceeded,
            "Editor authoring document state capacity is exhausted");
    }

    void discardSuspended(Tina::Editor::EditorDocumentKey key) noexcept
    {
        for (auto& slot : suspendedDocuments_) {
            if (slot.has_value() && slot->key == key) {
                slot.reset();
                return;
            }
        }
    }

    // Project switches drop every per-tab session and suspended document but
    // keep the two pinned workspace sessions.
    void resetTabState() noexcept
    {
        for (auto& slot : suspendedDocuments_) {
            slot.reset();
        }
        for (auto& slot : tabSessions_) {
            slot.reset();
        }
    }

  private:
    WorkspaceSessionState world2DSession_{};
    WorkspaceSessionState world3DSession_{};
    std::array<std::optional<WorkspaceSessionState>, DocumentTabSlots> tabSessions_{};
    std::array<std::optional<SuspendedTabAuthoringDocument>, DocumentTabSlots>
        suspendedDocuments_{};
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
[[nodiscard]] inline Tina::Core::Result<InitialAuthoringDocuments>
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
[[nodiscard]] inline Tina::Core::Result<Tina::Editor::ProjectAssetBrowserModel>
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
[[nodiscard]] inline Tina::Core::Result<Tina::Editor::ProjectAssetBrowserModel>
createProjectAssetBrowser(const EditorAssetResources& resources)
{
    if (!resources.system.has_value() || resources.system->catalog() == nullptr) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Tina Editor Project browser requires an open Catalog");
    }
    return createProjectAssetBrowser(*resources.system->catalog());
}
[[nodiscard]] inline Tina::Core::Result<Tina::Editor::EditorDocumentTabs>
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
[[nodiscard]] inline Tina::Asset::CatalogPackageStageConfig createEditorImportStageConfig(
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
          documentSessions_(std::move(world2DSession), std::move(world3DSession)),
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
                auto kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::CatalogRecipe;
                switch (unit.kind) {
                case Tina::EditorApp::Detail::EditorSourceImportLaunchUnitKind::CatalogRecipe:
                    kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::CatalogRecipe;
                    break;
                case Tina::EditorApp::Detail::EditorSourceImportLaunchUnitKind::Gltf:
                    kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::Gltf;
                    break;
                case Tina::EditorApp::Detail::EditorSourceImportLaunchUnitKind::Texture:
                    kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::Texture;
                    break;
                case Tina::EditorApp::Detail::EditorSourceImportLaunchUnitKind::Audio:
                    kind = Tina::EditorApp::Detail::EditorSourceImportUnitKind::Audio;
                    break;
                }
                sourceImportUnits_.push_back({
                    .kind = kind,
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
    Tina::Core::Status onEnter(Tina::GameStateEnterContext& context) override;

    // Shared retained-UI build context: one tree updater plus the fixed Editor
    // typography set, threaded through the per-region build methods below.
    struct UiBuildContext final {
        Tina::PrimaryWindowUITreeUpdater& tree;
        UI::UITheme productTheme{};
        UI::UITextStyle titleText{};
        UI::UITextStyle sectionText{};
        UI::UITextStyle bodyText{};
        UI::UITextStyle compactText{};
        UI::UITextStyle secondaryText{};
        UI::UITextStyle accentText{};

        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createPanel(
            UI::UINodeId parent, UI::UILayoutStyle layout, UI::UIStyleRoleId role,
            UI::UIStyleClassId styleClass = {});
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createLabel(
            UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
            const UI::UITextStyle& style);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createButton(
            UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
            bool enabled = true,
            UI::UIStyleRoleId role = UI::UIStyleRoleId::ButtonTonal);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createSegmentedButton(
            UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
            bool enabled = true);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createTextEdit(
            UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
            bool enabled);
    };

    [[nodiscard]] Tina::Core::Status buildToolbarUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildContextBarUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildDocumentTabsUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildLeftDockUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildViewportUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildInspectorUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildTimelineUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildStatusBarUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildDirtyCloseModalUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status registerUiCallbacks(UiBuildContext& ui);

    void onExit(Tina::GameStateExitContext&) noexcept override;
    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override;
    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override;
    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override;
    Tina::Core::Status updateUI(Tina::UIUpdateContext& context) override;
  private:
    void queueViewportZoomStep(float deltaPercent) noexcept;
    void resetViewportInteractionState() noexcept;
    [[nodiscard]] Tina::Core::Status processEditorShortcuts(
        const Tina::FrameActionSnapshot& actions);
    bool queueEditorCommand(EditorCommand command) noexcept;
    void queueViewportToolMode(ViewportToolMode mode) noexcept;
    [[nodiscard]] bool tileMapEditingContext() const noexcept;
    [[nodiscard]] bool playSessionActive() const noexcept;
    [[nodiscard]] bool authoringEnabled() const noexcept;
    [[nodiscard]] bool playStartReady() const noexcept;
    [[nodiscard]] bool sceneDocumentActive() const noexcept;
    [[nodiscard]] static std::string_view
    animationModeLabel(Tina::AssetFormat::SpriteAnimationPlaybackMode mode) noexcept;
    [[nodiscard]] Tina::Core::Status applyAnimationPreviewFrame(u32 frameIndex);
    [[nodiscard]] Tina::Core::Status rebuildAnimationAnimator();
    [[nodiscard]] Tina::Core::Result<Tina::AssetFormat::SpriteAnimationEventDesc>
    readAnimationEventInput(Tina::PrimaryWindowUITreeUpdater& tree) const;
    [[nodiscard]] Tina::Core::Status processPendingAnimationFrameSelection(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshAnimationTimelineUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status
    registerViewportPointerListeners(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] static Tina::Editor::EditorViewportNavigationConfig
    viewportNavigationConfig() noexcept;
    [[nodiscard]] Tina::Core::Status ensureViewportNavigation();
    [[nodiscard]] static Tina::Scene::Quaternion viewportOrbitRotation(
        float yawRadians, float pitchRadians) noexcept;
    void syncViewportZoomFromNavigation() noexcept;
    void persistViewportNavigationState() noexcept;
    [[nodiscard]] Tina::Core::Status applyViewportNavigationToPreview();
    [[nodiscard]] Tina::Core::Status initializeOrApplyViewportNavigation();
    [[nodiscard]] Tina::Core::Status focusViewportOnSelection();
    [[nodiscard]] bool queueViewportNavigationInput(
        Tina::Editor::EditorViewportNavigationInput input) noexcept;
    [[nodiscard]] Tina::Core::Status applyViewportZoomPercent(float percent);
    [[nodiscard]] Tina::Core::Status frameViewportContents();
    [[nodiscard]] Tina::Core::Status processViewportNavigation();
    [[nodiscard]] bool queueAutomaticViewportNavigation() noexcept;
    [[nodiscard]] bool prepareAutomaticViewportGizmo(
        Tina::Editor::EditorTransformGizmoMode mode) noexcept;
    [[nodiscard]] std::optional<UI::UILogicalPoint>
    automaticViewportGizmoPoint(float fraction) const noexcept;
    [[nodiscard]] bool prepareAutomaticViewportMarquee(
        Tina::Editor::EditorMarqueeSelectionMode mode) noexcept;
    [[nodiscard]] bool beginViewportGizmo(Tina::Platform::PointerId pointer,
                                          UI::UILogicalPoint position) noexcept;
    [[nodiscard]] bool updateViewportGizmo(Tina::Platform::PointerId pointer,
                                           UI::UILogicalPoint position) noexcept;
    [[nodiscard]] bool requestViewportGizmoCommit(Tina::Platform::PointerId pointer,
                                                  UI::UILogicalPoint position) noexcept;
    [[nodiscard]] bool beginViewportNavigation(
        Tina::Platform::PointerId pointer,
        Tina::Platform::PointerButton button) noexcept;
    [[nodiscard]] bool updateViewportNavigation(
        const UI::UIPointerInputEvent& input) noexcept;
    [[nodiscard]] bool beginViewportMarquee(
        Tina::Platform::PointerId pointer,
        UI::UILogicalPoint position) noexcept;
    [[nodiscard]] bool updateViewportMarquee(
        Tina::Platform::PointerId pointer,
        UI::UILogicalPoint position) noexcept;
    [[nodiscard]] bool queueViewportTileBrush(UI::UILogicalPoint position) noexcept;
    void handleViewportPointerDown(UI::UIRoutedPointerEvent& event) noexcept;
    void handleViewportPointerMove(UI::UIRoutedPointerEvent& event) noexcept;
    void handleViewportPointerUp(UI::UIRoutedPointerEvent& event) noexcept;
    void handleViewportPointerCancel(UI::UIRoutedPointerEvent& event) noexcept;
    void handleViewportPointerWheel(UI::UIRoutedPointerEvent& event) noexcept;
    [[nodiscard]] Tina::Scene::EntityId findPreviewEntity(u32 stableEntityId) const noexcept;
    [[nodiscard]] u32 stableIdForPreviewEntity(
        Tina::Scene::EntityId entity) const noexcept;
    [[nodiscard]] bool viewportSelectionContains(u64 stableId) const noexcept;
    [[nodiscard]] bool previewEntityHasAncestorInSelection(
        Tina::Scene::EntityId entity,
        std::span<const u64> selection) const noexcept;
    [[nodiscard]] bool previewEntityHasSelectedAncestor(
        Tina::Scene::EntityId entity) const noexcept;
    [[nodiscard]] Tina::Core::usize viewportTransformTargetCount(
        std::span<const u64> selection) const noexcept;
    [[nodiscard]] Tina::Scene::Vec3 viewportSelectionPivot() const noexcept;
    [[nodiscard]] Tina::Core::Status captureViewportTransformTargets(
        ViewportTransformTransaction& transaction);
    [[nodiscard]] static Tina::Core::Result<Tina::Scene::WorldTransform>
    applyViewportWorldTransformDelta(
        const Tina::Scene::WorldTransform& baseline,
        Tina::Scene::Vec3 pivot,
        const Tina::Editor::EditorTransformGizmoDelta& delta,
        Tina::Scene::Quaternion localBasis) noexcept;
    [[nodiscard]] static bool viewportTransformNearlyEqual(float left,
                                                           float right) noexcept;
    [[nodiscard]] static bool viewportWorldTransformsEquivalent(
        Tina::Scene::WorldTransform left,
        Tina::Scene::WorldTransform right) noexcept;
    [[nodiscard]] Tina::Core::Result<Tina::Scene::LocalTransform>
    localTransformFromWorld(Tina::Scene::EntityId entity,
                            Tina::Scene::WorldTransform world) const;
    [[nodiscard]] bool viewportGizmoContextMatches() const noexcept;
    [[nodiscard]] static bool viewportTransformDeltaIsIdentity(
        const Tina::Editor::EditorTransformGizmoDelta& delta) noexcept;
    [[nodiscard]] Tina::Core::Status
    finishViewportGizmoWithoutCommit(Tina::PrimaryWindowUITreeUpdater& tree, bool rejected,
                                     std::string_view feedback);
    [[nodiscard]] Tina::Core::Status
    commitViewportGizmoTransform(const ViewportTransformTransaction& transaction);
    [[nodiscard]] Tina::Core::Status
    processViewportGizmo(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status
    processPendingTileBrush(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status
    refreshViewportToolUi(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] const Tina::Scene::World2DEntityBinding*
    findPreviewBinding(UI::UITreeViewItemKey hierarchyKey) const noexcept;
    [[nodiscard]] Tina::Core::Status
    extractWorld3DViewport(Tina::RenderSceneExtractionContext& context) const;
    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewSprite(void* userData, Tina::Asset::AssetHandle asset,
                         Tina::Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewTexture(void* userData, Tina::Asset::AssetHandle asset,
                          Tina::Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewTileset(void* userData, Tina::Asset::AssetHandle asset,
                          Tina::Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewMesh(void* userData, Tina::Asset::AssetHandle asset,
                       Tina::Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewMaterial(void* userData, Tina::Asset::AssetHandle asset,
                           Tina::Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] ViewportProjectedPoint projectViewportWorldPoint(
        Tina::Scene::Vec3 worldPoint) const noexcept;
    [[nodiscard]] static UI::UIStraightSrgba8Color viewportGizmoColor(
        Tina::Editor::EditorTransformGizmoHandle handle,
        bool highlighted) noexcept;
    [[nodiscard]] Tina::Core::Status collapseViewportGizmoVisuals(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status materializeViewportGizmoVisuals(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status updateViewportTransformGizmo(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status updateViewportMarqueeVisual(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] std::optional<u64> hierarchyIndexForStableId(
        u64 stableId) const noexcept;
    [[nodiscard]] std::optional<u32> automaticHierarchyStableId(
        bool preferLast) const noexcept;
    void synchronizeViewportSelectionFromHierarchy() noexcept;
    [[nodiscard]] Tina::Core::usize collectViewportMarqueeCandidates(
        std::span<Tina::Editor::EditorMarqueeCandidate> output) const noexcept;
    [[nodiscard]] Tina::Core::Status processViewportMarquee(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] float viewportWorldHeight() const noexcept;
    [[nodiscard]] float viewportWorldWidth() const noexcept;
    [[nodiscard]] static UI::UIStraightSrgba8Color viewportGridColor(
        Tina::Editor::EditorViewportGridSegmentKind kind) noexcept;
    [[nodiscard]] static UI::UILayoutStyle viewportGridLayout(
        const Tina::Editor::EditorViewportGridSegment& segment,
        float viewportWidth,
        float viewportHeight,
        UI::UILineGeometry& line) noexcept;
    [[nodiscard]] Tina::Core::Status
    updateViewportGrid(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status updateGpuViewport(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status editTileMapBrushCell(bool erase);
    [[nodiscard]] Tina::Core::Status toggleActiveTileMapLayer();
    [[nodiscard]] Tina::Core::Status
    addTileMapLayer(Tina::AssetFormat::TileMapLayerKind kind);
    [[nodiscard]] Tina::Core::Status refreshProjectAssetUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status reportAuthoringFailure(
        std::string_view prefix, const Tina::Core::Error& error);
    [[nodiscard]] Tina::Core::Result<Tina::Editor::ProjectAssetBrowserModel>
    prepareProjectBrowserForSnapshot(
        const Tina::Asset::CatalogSnapshot& catalog,
        Tina::Editor::ProjectAssetFilter filter,
        std::optional<Tina::Core::AssetId> selectedAsset);
    [[nodiscard]] Tina::Core::Result<Tina::Editor::EditorDocumentTabs>
    prepareProjectSwitchDocumentTabs();
    [[nodiscard]] Tina::Core::Status switchCatalogAuthoringOwnersToPinnedTabs();
    [[nodiscard]] Tina::Core::Status refreshPinnedCatalogAuthoringDocuments(
        const Tina::Editor::ProjectAssetBrowserModel& browser);
    void commitProjectSwitchDocumentTabs(
        Tina::Editor::EditorDocumentTabs candidateTabs) noexcept;
    [[nodiscard]] Tina::Core::Status rebuildLiveCatalogPreview(
        std::string successFeedback);
    [[nodiscard]] Tina::Core::Status switchLiveProjectCatalog(
        Tina::Editor::EditorProjectWorkspace workspace);
    [[nodiscard]] Tina::Core::Status createNewProjectFromDialog();
    [[nodiscard]] Tina::Core::Status openProjectFromDialog();
    [[nodiscard]] Tina::Core::Status startSourceImport(
        std::span<const Tina::EditorApp::Detail::EditorSourceImportUnit> intendedUnits);
    [[nodiscard]] Tina::Core::Status importSourceFromDialog();
    [[nodiscard]] Tina::Core::Status removeSelectedSourceImport();
    void cleanupOwnedSourceImportStage(std::string_view catalogRootUtf8) noexcept;
    void cleanupOwnedAuthoringStage(std::string_view catalogRootUtf8) noexcept;
    void cleanupFailedSourceImportStage() noexcept;
    [[nodiscard]] Tina::Core::Status publishCommittedSourceImportState(
        const Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready);
    [[nodiscard]] Tina::Core::Status commitSourceImportCatalog(
        const Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready);
    [[nodiscard]] Tina::Core::Status updateSourceImport();
    [[nodiscard]] Tina::Core::Status refreshProjectCatalog();
    [[nodiscard]] Tina::Core::Status bakeAndPublishNavigation2D();
    [[nodiscard]] Tina::Core::Status refreshDocumentTabsUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status applyProjectAssetFilter(
        Tina::PrimaryWindowUITreeUpdater& tree,
        Tina::Editor::ProjectAssetFilter filter);
    [[nodiscard]] WorkspaceSessionState*
    findDocumentSession(Tina::Editor::EditorDocumentKey key) noexcept;
    [[nodiscard]] const WorkspaceSessionState*
    findDocumentSession(Tina::Editor::EditorDocumentKey key) const noexcept;
    [[nodiscard]] Tina::Core::Status
    installDocumentSession(WorkspaceSessionState session);
    void discardDocumentSession(Tina::Editor::EditorDocumentKey key) noexcept;
    [[nodiscard]] Tina::Core::Status initializePinnedDocumentSessions();
    [[nodiscard]] Tina::Core::Result<WorkspaceSessionState>
    makeProjectAssetSession(Tina::Editor::EditorDocumentKey key,
                            const TabAuthoringDocument& document,
                            Tina::AssetFormat::TargetPlatform targetPlatform) const;
    [[nodiscard]] WorkspaceSessionState* activeDocumentSession() noexcept;
    [[nodiscard]] const WorkspaceSessionState* activeDocumentSession() const noexcept;
    [[nodiscard]] bool documentPathOwnedByOtherSession(
        std::string_view path,
        Tina::Editor::EditorDocumentKey activeKey) const noexcept;
    [[nodiscard]] Tina::Core::Status refreshToolbarPathForActiveTab(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Editor::EditorDocumentKey*
    activeAuthoringDocumentOwner(Tina::Editor::EditorDocumentKind kind) noexcept;
    [[nodiscard]] const SuspendedTabAuthoringDocument*
    findSuspendedAuthoringDocument(Tina::Editor::EditorDocumentKey key) const noexcept;
    [[nodiscard]] SuspendedTabAuthoringDocument*
    findSuspendedAuthoringDocument(Tina::Editor::EditorDocumentKey key) noexcept;
    [[nodiscard]] Tina::Core::Status
    switchActiveAuthoringDocument(Tina::Editor::EditorDocumentKey key) noexcept;
    [[nodiscard]] Tina::Core::Status
    installNewAuthoringDocument(Tina::Editor::EditorDocumentKey key,
                                TabAuthoringDocument document);
    void discardSuspendedAuthoringDocument(
        Tina::Editor::EditorDocumentKey key) noexcept;
    [[nodiscard]] Tina::Core::Result<LoadedProjectAssetDocument>
    loadProjectAssetDocument(
        const Tina::Editor::ProjectAssetDescriptor& asset);
    [[nodiscard]] Tina::Core::Status openSelectedProjectAsset(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status showDirtyCloseModal(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status hideDirtyCloseModal(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status closeActiveDocument(
        Tina::PrimaryWindowUITreeUpdater& tree,
        bool discardDirty = false);
    [[nodiscard]] bool dirtyCloseTargetsActiveDocument() const noexcept;
    [[nodiscard]] Tina::Core::Status confirmDirtyCloseSave(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status confirmDirtyCloseDiscard(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status cancelDirtyClose(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status activateDocumentTab(
        Tina::PrimaryWindowUITreeUpdater& tree, u32 index);
    [[nodiscard]] Tina::Core::Status executeEditorCommand(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status
    activateWorkspace(Tina::PrimaryWindowUITreeUpdater& tree, WorkspaceMode mode);
    [[nodiscard]] Tina::Core::Status
    refreshViewportViewModeUi(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status
    refreshWorkspaceChrome(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Result<SavedDocumentBaseline>
    captureActiveDocumentSavedBaseline() const;
    struct SaveDialogLocation final {
        std::string initialDirectoryUtf8{};
        std::string suggestedFileNameUtf8{};
    };
    [[nodiscard]] Tina::Core::Result<SaveDialogLocation>
    makeSaveDialogLocation(std::string_view currentPathUtf8,
                           std::string_view fallbackFileNameUtf8,
                           bool directoryTarget) const;
    [[nodiscard]] Tina::Core::Result<std::optional<std::string>>
    requestNativeSaveAsPath(std::string_view currentPathUtf8);
    [[nodiscard]] Tina::Core::Status saveActiveDocument(
        std::optional<std::string_view> saveAsPath = std::nullopt);
    [[nodiscard]] WorkspaceSessionState& activeWorkspaceSession() noexcept;
    [[nodiscard]] const WorkspaceSessionState& activeWorkspaceSession() const noexcept;
    [[nodiscard]] const WorkspaceSessionState& workspaceSession(WorkspaceMode mode) const noexcept;
    [[nodiscard]] std::span<const std::byte> documentBytes(WorkspaceMode mode) const noexcept;
    [[nodiscard]] std::span<const std::byte>
    workspaceSessionDocumentBytes(WorkspaceMode mode) const noexcept;
    [[nodiscard]] bool isDocumentDirty(WorkspaceMode mode) const noexcept;
    [[nodiscard]] bool activeTabDocumentDirty() const noexcept;
    [[nodiscard]] Tina::Core::Status synchronizeActiveTabDirty() noexcept;
    void publishWorkspaceSessionCounters() noexcept;
    [[nodiscard]] std::span<const std::byte> activeDocumentBytes() const noexcept;
    [[nodiscard]] u64 activeDocumentCanonicalByteCount() const noexcept;
    [[nodiscard]] u64 activeDocumentRevision() const noexcept;
    [[nodiscard]] u64 activeDocumentItemCount() const noexcept;
    [[nodiscard]] u64 activeUndoDepth() const noexcept;
    [[nodiscard]] u64 activeRedoDepth() const noexcept;
    [[nodiscard]] bool activeCanUndo() const noexcept;
    [[nodiscard]] bool activeCanRedo() const noexcept;
    [[nodiscard]] Tina::Core::Status moveSelectedPositiveX();
    [[nodiscard]] static bool inspectorTransformValueDiffers(float current,
                                                             float requested) noexcept;
    [[nodiscard]] static bool inspectorRotationEquivalent(
        const std::array<float, 4>& requested,
        float currentX, float currentY, float currentZ,
        float currentW) noexcept;
    [[nodiscard]] Tina::Core::Status applySelectedTransform(
        const InspectorTransformInput& input);
    [[nodiscard]] Tina::Core::Result<std::vector<u32>>
    componentSelectionStableIds() const;
    [[nodiscard]] Tina::Core::AssetId
    selectedProjectAssetIdOfKind(Tina::AssetFormat::AssetKind kind) const noexcept;
    // Runs one Inspector component command against the active world document.
    // Rejections (invalid selection, redundant add/remove, invalid values) are
    // reported through authoringFeedback_ and return success; `published` is
    // true only when exactly one document revision was published.
    [[nodiscard]] Tina::Core::Status runComponentCommand(
        Tina::PrimaryWindowUITreeUpdater& tree, EditorCommand command,
        bool& published);
    [[nodiscard]] Tina::Core::Status refreshComponentSectionsUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Asset::AssetHandle
    loadedAsset(Tina::Core::AssetId assetId, Tina::AssetFormat::AssetKind expectedKind) const noexcept;
    [[nodiscard]] static bool containsHandle(std::span<const Tina::Asset::AssetHandle> handles,
                                             Tina::Asset::AssetHandle handle) noexcept;
    [[nodiscard]] Tina::Core::Status preparePreviewAssetBindings();
    void releasePreviewAssetBindings() noexcept;
    [[nodiscard]] Tina::Core::Status validateRuntimePreview();
    [[nodiscard]] Tina::Core::Status validateWorld3DRuntimePreview();
    [[nodiscard]] Tina::Core::Status publishRuntimePreviewStatus(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshAuthoringUi(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshPlaySessionUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] const Tina::Editor::ProjectAssetDescriptor*
    inspectedProjectAsset() const noexcept;
    [[nodiscard]] Tina::Core::Result<InspectorMixedTransformFlags>
    inspectorMixedTransformFlags(u32 primaryStableId) const;
    [[nodiscard]] Tina::Core::Status publishInspector(Tina::PrimaryWindowUITreeUpdater& tree,
                                                      UI::UITreeViewItemKey key);
    [[nodiscard]] UI::UIListViewDataSource inspectorDependencyDataSource() const noexcept;
    [[nodiscard]] static u64 inspectorDependencyItemCount(const void* state) noexcept;
    static bool resolveInspectorDependencyItem(
        const void* state, u64 logicalIndex,
        UI::UIListViewItemDescriptor& output) noexcept;
    [[nodiscard]] UI::UIListViewDataSource sourceImportDataSource() const noexcept;
    [[nodiscard]] static u64 sourceImportItemCount(const void* state) noexcept;
    static bool resolveSourceImportItem(
        const void* state, u64 logicalIndex,
        UI::UIListViewItemDescriptor& output) noexcept;
    [[nodiscard]] UI::UIListViewDataSource projectAssetDataSource() const noexcept;
    [[nodiscard]] static u64 projectAssetItemCount(const void* state) noexcept;
    static bool resolveProjectAssetItem(const void* state, u64 logicalIndex,
                                        UI::UIListViewItemDescriptor& output) noexcept;
    [[nodiscard]] bool hierarchyRowCollapsed(u32 stableId) const noexcept;
    [[nodiscard]] const EditorHierarchyRow*
    hierarchyRow(u32 stableId) const noexcept;
    [[nodiscard]] std::string_view hierarchyDisplayLabel(
        UI::UITreeViewItemKey key) const noexcept;
    [[nodiscard]] std::string_view hierarchyDisplayKind(
        UI::UITreeViewItemKey key) const noexcept;
    [[nodiscard]] std::string_view hierarchyDisplayNote(
        UI::UITreeViewItemKey key) const noexcept;
    [[nodiscard]] bool
    hierarchyRowVisible(const EditorHierarchyRow& row) const noexcept;
    [[nodiscard]] std::optional<u64>
    visibleHierarchyIndex(u32 stableId) const noexcept;
    [[nodiscard]] static std::string hierarchyEntityLabel(
        WorkspaceMode mode, u32 stableId, bool hasRenderable,
        bool hasCamera, bool hasLight);
    [[nodiscard]] Tina::Core::Status rebuildHierarchyModel();
    [[nodiscard]] Tina::Core::Status refreshHierarchyTree(
        Tina::PrimaryWindowUITreeUpdater& tree, u32 preferredStableId);
    [[nodiscard]] UI::UITreeViewDataSource hierarchyDataSource() noexcept;
    [[nodiscard]] static u64 hierarchyItemCount(const void* state) noexcept;
    static bool resolveHierarchyItem(const void* state, u64 logicalIndex,
                                     UI::UITreeViewItemDescriptor& output) noexcept;
    static bool setHierarchyExpanded(void* state, UI::UITreeViewItemKey key, bool expanded) noexcept;
    EditorLaunchOptions options_;
    LifecycleCounters& counters_;
    Tina::Editor::World2DAuthoringDocument document_;
    Tina::Editor::World3DAuthoringDocument document3D_;
    Tina::Editor::TileMapAuthoringDocument tileMapDocument_;
    Tina::Editor::Navigation2DAuthoringDocument navigationDocument_{};
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
    WorkspaceMode workspaceMode_ = WorkspaceMode::World2D;
    EditorDocumentSessionStore documentSessions_;
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
    std::string sourceImportSupersededAuthoringCatalogRootUtf8_{};
    bool sourceImportStartPending_ = false;
    bool sourceImportCatalogCommitted_ = false;
    Tina::EditorApp::Detail::EditorFileDialog fileDialog_{};
    Tina::UI::UIRootOwner uiRoot_{};
    UI::UINodeId hierarchyTree_{};
    UI::UINodeId hierarchyCount_{};
    UI::UINodeId projectAssetList_{};
    UI::UINodeId projectAssetCount_{};
    UI::UINodeId projectAssetSource_{};
    UI::UINodeId sourceImportList_{};
    UI::UINodeId sourceImportCount_{};
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
    UI::UINodeId zoomOutButton_{};
    UI::UINodeId zoomSlider_{};
    UI::UINodeId zoomValue_{};
    UI::UINodeId zoomInButton_{};
    UI::UINodeId frameAllButton_{};
    UI::UINodeId documentFormat_{};
    struct ComponentSectionUi final {
        UI::UINodeId activeCheckbox{};
        UI::UINodeId headerLabel{};
        UI::UINodeId addButton{};
        UI::UINodeId removeButton{};
        std::array<UI::UINodeId, 6> fields{};
        Tina::Core::usize fieldCount = 0;
        UI::UINodeId assignButton{};
        UI::UINodeId applyButton{};
    };
    // 0..4 mirror Tina::Editor::World2DComponentKind; 5 is the 3D MeshRenderer.
    static constexpr Tina::Core::usize MeshRendererSectionIndex = 5;
    std::array<ComponentSectionUi, 6> componentSections_{};
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
    UI::UINodeId removeSourceImportButton_{};
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
    UI::UINodeId bakeNavigationButton_{};
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
    UI::UINodeId animationEventPreviousButton_{};
    UI::UINodeId animationEventNextButton_{};
    UI::UINodeId animationEventTag_{};
    UI::UINodeId animationEventOffset_{};
    UI::UINodeId animationEventAddButton_{};
    UI::UINodeId animationEventApplyButton_{};
    UI::UINodeId animationEventRemoveButton_{};
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
    std::vector<Tina::Asset::AssetHandle> loadedPreviewHandles_{};
    std::vector<Tina::Asset::AssetHandle> boundSpriteAssets_{};
    std::vector<Tina::Asset::AssetHandle> boundTilesetAssets_{};
    std::vector<Tina::Asset::AssetHandle> boundMeshAssets_{};
    std::vector<Tina::Asset::AssetHandle> boundMaterialAssets_{};
    u64 previewResolvedSpriteCount_ = 0;
    u64 previewResolvedMeshCount_ = 0;
    u64 previewRevision_ = 0;
    Tina::EditorApp::Detail::EditorAnimationPreview animationPreview_{};
    u32 animationSelectedEventIndex_ = 0;
    bool previewAssetBindingsRefreshPending_ = false;
    bool catalogRefreshPending_ = false;
    bool projectBrowserUiRefreshPending_ = false;
    bool sourceImportUiRefreshPending_ = false;
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
    std::optional<u64> observedSourceImportSelectionIndex_{};
    std::vector<std::string> inspectorDependencyLabels_{};
    std::optional<u32> pendingDocumentTabActivation_{};
    std::optional<EditorCommand> pendingEditorCommand_{};
    std::optional<Tina::Editor::EditorDocumentKey> pendingDirtyCloseKey_{};
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

} // namespace Tina::EditorApp::WorkspaceInternal
