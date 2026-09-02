#pragma once

// Tina Editor desktop composition: shared retained tool chrome backed by
// validated World2D and World3D authoring documents and Scene GPU previews.

#include "EditorAnimationPreview.hpp"
#include "core/io/PathUtil.hpp"
#include "EditorCompositeImageResolver.hpp"
#include "EditorFileDialog.hpp"
#include "EditorIconResources.hpp"
#include "EditorSourceImportLaunchOptions.hpp"
#include "EditorSourceFileRename.hpp"
#include "EditorSourceImportSelection.hpp"
#include "EditorSourceImportService.hpp"
#include "EditorWorkspaceUiRecipes.hpp"

#include <tina/animation3d/Skeleton3D.hpp>
#include <tina/asset/AssetGpuMesh.hpp>
#include <tina/asset/AssetGpuTexture.hpp>
#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/AssetSystem.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageChangeDetector.hpp>
#include <tina/asset/CatalogPackagePublish.hpp>
#include <tina/asset/Mesh3DBindingRegistry.hpp>
#include <tina/asset/MediaCook.hpp>
#include <tina/asset/SourceImportCapture.hpp>
#include <tina/asset/SourceImportPipeline.hpp>
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
#include <tina/core/text/ArgParser.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/text/Utf8.hpp>
#include <tina/desktop/DesktopEngine.hpp>
#include <tina/editor/EditorNodePropertyOperations.hpp>
#include <tina/editor/EditorDocumentTabs.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/EditorMarqueeSelection.hpp>
#include <tina/editor/EditorPlaySession.hpp>
#include <tina/editor/EditorProjectCreation.hpp>
#include <tina/editor/EditorSceneOperations.hpp>
#include <tina/editor/EditorTransformGizmo.hpp>
#include <tina/editor/EditorViewportGrid.hpp>
#include <tina/editor/EditorViewportNavigation.hpp>
#include <tina/editor/EditorViewportPick.hpp>
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
#include <tina/runtime/PlatformEvents.hpp>
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
#include <tina/ui/UICollapsibleSection.hpp>
#include <tina/ui/UIColorField.hpp>
#include <tina/ui/UIColorPicker.hpp>
#include <tina/ui/UIDataGrid.hpp>
#include <tina/ui/UIDialog.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UILayoutDebugger.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UISnackbar.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UITheme.hpp>
#include <tina/ui/UITreeView.hpp>
#include <tina/ui/UIVirtualGridView.hpp>

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
#include <fstream>
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
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")
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
inline constexpr float LeftDockInitialFraction = 0.22F;
inline constexpr float MainCenterInitialFraction = 0.68F;
inline constexpr float ViewportInitialFraction = 0.68F;
inline constexpr u32 HierarchyMaterializedCapacity = 64;
inline constexpr u32 AssetBrowserMaterializedCapacity = 24;
inline constexpr u32 ProjectAssetTypeFilterCount = 7;
inline constexpr u32 ProjectAssetCompactIdCharacterCount = 4;
inline constexpr float ProjectAssetMinimumItemWidth = 116.0F;
inline constexpr u32 SourceImportColumnCapacity = 3;
inline constexpr u32 SourceImportMaterializedCapacity = 5;
inline constexpr float SourceImportKindColumnWidth = 88.0F;
inline constexpr float SourceImportSourceColumnWidth = 190.0F;
inline constexpr float SourceImportStatusColumnWidth = 84.0F;
inline constexpr u32 OutputColumnCapacity = 3;
inline constexpr u32 OutputMaterializedCapacity = 8;
inline constexpr u32 LayoutDebugProjectionCapacity = 4096;
inline constexpr u32 LayoutDebugTreeMaterializedCapacity = 96;
inline constexpr u32 LayoutDebugDetailRowCount = 25;
inline constexpr u32 LayoutDebugOverlayCommandsPerNode = 4;
// Each overlay border quad is split around the debugger window so no outline
// paints across it. A border strip is thin on its short axis, so a single split
// along the long axis is exact and yields at most two pieces per quad.
inline constexpr u32 LayoutDebugOverlayExclusionSplitFactor = 2;
inline constexpr u32 LayoutDebugSelectedOverlayCommandCapacity = 12;
inline constexpr u32 LayoutDebugDisplayListEntryCapacity = 48U * 1024U;
inline constexpr u64 LayoutDebugProfileWarmupFrameCount = 30U;
inline constexpr u64 LayoutDebugProfileMutationFrameCount = 120U;
inline constexpr u64 LayoutDebugProfileCooldownFrameCount = 5U;
inline constexpr u64 LayoutDebugProfileMinimumFrameCount =
    LayoutDebugProfileWarmupFrameCount +
    LayoutDebugProfileMutationFrameCount +
    LayoutDebugProfileCooldownFrameCount;
inline constexpr u32 EditorRenderDrawCallHeadroom = 1024U;
inline constexpr u32 EditorRenderDrawCallCapacity =
    LayoutDebugDisplayListEntryCapacity + EditorRenderDrawCallHeadroom;
static_assert(
    LayoutDebugDisplayListEntryCapacity >=
    LayoutDebugProjectionCapacity * LayoutDebugOverlayCommandsPerNode *
            LayoutDebugOverlayExclusionSplitFactor +
        LayoutDebugSelectedOverlayCommandCapacity *
            LayoutDebugOverlayExclusionSplitFactor + 8192U);
static_assert(EditorRenderDrawCallCapacity % 1024U == 0U);
static_assert(EditorRenderDrawCallCapacity < 65535U);
inline constexpr float OutputSeverityColumnWidth = 76.0F;
inline constexpr float OutputContextColumnWidth = 132.0F;
inline constexpr float OutputMessageColumnWidth = 720.0F;
inline constexpr u32 AuthoringEntityCapacity = 128;
inline constexpr u32 InitialAuthoringEntityCount = 5;
inline constexpr u32 AnimationVisibleFrameSlots = 6;
inline constexpr u32 DocumentTabSlots = 6;
inline constexpr u32 RecentProjectCapacity = 10;
inline constexpr u32 TilePaletteMaterializedCapacity = 64;
// Status-bar frame metrics refresh interval. Long enough that the readout text
// is byte-identical across most frames, short enough to stay legible as a live
// reading.
inline constexpr double StatusFrameMetricIntervalSeconds = 0.25;
inline constexpr float TilePaletteMinimumItemWidth = 72.0F;
inline constexpr float TilePaletteItemHeight = 64.0F;
inline constexpr u32 MainMenuCount = 4;
inline constexpr u32 AutomaticColorPickerVisibleStage = 52;
inline constexpr u32 AutomaticFinalSelectionCommitStage = 56;
inline constexpr u32 AutomaticAuthoringStageCount = 57;
// Covers the stages that need more than one frame: the scene Add picker and the
// Delete dialog each spend an extra frame opening and confirming.
inline constexpr u64 AutomaticAuthoringFrameReserve = 13;
inline constexpr float AutomaticInspectorCaptureInset = 8.0F;
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
// Angle conversions come from Tina::Math rather than a local copy of the same two
// constants.
using Tina::Math::DegreesToRadians;
using Tina::Math::RadiansToDegrees;
// Vertical field of view of the 3D preview camera. Single definition on purpose:
// the pick ray must be built from the same value the viewport projects with, and a
// drifted second copy yields picking that misses by a little without ever looking
// obviously wrong.
inline constexpr float ViewportPerspectiveFovDegrees = 55.0F;
inline constexpr Tina::Core::usize ViewportGridVisualNodeCapacity =
    Tina::Editor::EditorViewportGridSegmentCapacity;
inline constexpr Tina::Core::usize ViewportGizmoVisualNodeCapacity = 256;
inline constexpr Tina::Core::usize ViewportPreselectionVisualNodeCapacity = 1;
// Four box edges, or one ellipse ring, per CollisionShape2D. Capacity bounds how
// many shapes an outline pass can draw; extra shapes are skipped, never dropped
// from the document.
inline constexpr Tina::Core::usize ViewportCollisionShapeVisualNodeCapacity = 128;
inline constexpr Tina::Core::usize ViewportOrientationAxisCount = 3;
inline constexpr float ViewportOrientationCompassExtent = 82.0F;
inline constexpr float ViewportOrientationCompass2DExtent = 58.0F;
inline constexpr float ViewportOrientationCompassInset = 12.0F;
inline constexpr float ViewportOrientationAxisLength = 25.0F;
inline constexpr float ViewportOrientationAxis2DLength = 18.0F;
inline constexpr float ViewportOrientationEndpointExtent = 18.0F;
inline constexpr float ViewportOrientationCenterExtent = 6.0F;
inline constexpr Tina::Core::usize ViewportOrientationOrbLayerCount = 5;
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
inline constexpr Tina::InputActionId ConfirmRename{16};

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
enum class BottomPanelKind : u8 {
    None,
    Animation,
    Output,
};
enum class WorkspacePanelKind : u8 {
    LeftDock,
    Inspector,
};

// Path helpers come from the one shared definition; see src/core/io/PathUtil.hpp and ADR 0040.
// These were four inline copies here, and the Windows spelling disagreed with the asset
// pipeline's, so the editor and the cooker could reach opposite verdicts on the same pair.
using Tina::Core::Detail::pathComponentEquals;
using Tina::Core::Detail::pathIsSameOrDescendant;
using Tina::Core::Detail::pathsReferToSameLocation;
using Tina::Core::Detail::pathToUtf8;

struct EditorAssetMetadataRecord final {
    Tina::Core::AssetId assetId{};
    std::string displayName{};
    std::string folderPathUtf8{};
};

inline constexpr Tina::Core::u32 EditorAssetMetadataCapacity = 4096U;

[[nodiscard]] inline std::filesystem::path editorAssetMetadataPath(
    const Tina::Editor::EditorProjectWorkspace& workspace)
{
    return std::filesystem::u8path(
               workspace.projectRootUtf8().begin(), workspace.projectRootUtf8().end()) /
           ".tina" / "editor-assets.tmeta";
}

[[nodiscard]] inline Tina::Core::Result<std::vector<EditorAssetMetadataRecord>>
loadEditorAssetMetadata(const Tina::Editor::EditorProjectWorkspace& workspace) noexcept
{
    try {
        const auto path = editorAssetMetadataPath(workspace);
        auto bytes = Tina::Core::readFile(
            pathToUtf8(path), {.maxBytes = 2U * 1024U * 1024U});
        if (!bytes) {
            return std::vector<EditorAssetMetadataRecord>{};
        }
        const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        if (!Tina::Core::isStrictUtf8WithoutNul(text)) {
            return std::vector<EditorAssetMetadataRecord>{};
        }
        std::vector<EditorAssetMetadataRecord> records;
        records.reserve(EditorAssetMetadataCapacity);
        std::size_t cursor = 0U;
        while (cursor < text.size() && records.size() < EditorAssetMetadataCapacity) {
            const std::size_t lineEnd = text.find('\n', cursor);
            const std::size_t end = lineEnd == std::string::npos ? text.size() : lineEnd;
            const std::string_view line{text.data() + cursor, end - cursor};
            cursor = lineEnd == std::string::npos ? text.size() : lineEnd + 1U;
            if (line.empty() || line == "version=1") {
                continue;
            }
            const std::size_t first = line.find('\t');
            const std::size_t second = first == std::string_view::npos
                                            ? std::string_view::npos
                                            : line.find('\t', first + 1U);
            if (first == std::string_view::npos || second == std::string_view::npos ||
                second <= first + 1U) {
                continue;
            }
            const auto id = Tina::Core::AssetId::parseCanonical(line.substr(0U, first));
            const std::string_view display = line.substr(first + 1U, second - first - 1U);
            const std::string_view folder = line.substr(second + 1U);
            if (!id || display.empty() || !Tina::Core::isStrictUtf8WithoutNul(display) ||
                !Tina::Core::isStrictUtf8WithoutNul(folder)) {
                continue;
            }
            records.push_back({
                .assetId = *id,
                .displayName = std::string(display),
                .folderPathUtf8 = std::string(folder),
            });
        }
        return records;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor asset metadata allocation failed");
    } catch (...) {
        return std::vector<EditorAssetMetadataRecord>{};
    }
}

[[nodiscard]] inline Tina::Core::Status saveEditorAssetMetadata(
    const Tina::Editor::EditorProjectWorkspace& workspace,
    std::span<const Tina::Editor::ProjectAssetDescriptor> assets) noexcept
{
    try {
        std::string text = "version=1\n";
        for (const auto& asset : assets) {
            if (asset.displayName.empty() ||
                !Tina::Core::isStrictUtf8WithoutNul(asset.displayName) ||
                asset.displayName.find_first_of("\t\r\n") != std::string::npos ||
                asset.folderPathUtf8.find_first_of("\t\r\n") != std::string::npos) {
                continue;
            }
            const auto id = asset.assetId.canonicalText();
            text.append(id.data(), id.size());
            text.push_back('\t');
            text += asset.displayName;
            text.push_back('\t');
            text += asset.folderPathUtf8;
            text.push_back('\n');
        }
        return Tina::Core::writeFile(
            pathToUtf8(editorAssetMetadataPath(workspace)),
            std::as_bytes(std::span{text.data(), text.size()}));
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor asset metadata serialization allocation failed");
    } catch (...) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Io,
            "Editor asset metadata serialization failed");
    }
}

struct EditorSettings final {
    static constexpr u32 SchemaVersion = 2;
    float leftDockFraction = LeftDockInitialFraction;
    float inspectorFraction = MainCenterInitialFraction;
    float bottomPanelFraction = ViewportInitialFraction;
    bool leftDockVisible = true;
    bool inspectorVisible = true;
    BottomPanelKind bottomPanel = BottomPanelKind::None;
    bool layoutDebuggerVisible = false;
    bool snapEnabled = false;
    std::array<std::string, RecentProjectCapacity> recentProjects{};
    u32 recentProjectCount = 0;
};

[[nodiscard]] inline std::filesystem::path editorSettingsPath()
{
    const char* appData = std::getenv("APPDATA");
    if (appData != nullptr && *appData != '\0') {
        return std::filesystem::u8path(appData) / "TinaEditor" / "settings";
    }
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return std::filesystem::u8path(xdg) / "TinaEditor" / "settings";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::u8path(home) / ".config" / "TinaEditor" / "settings";
    }
    return std::filesystem::path{};
}

[[nodiscard]] inline EditorSettings loadEditorSettings() noexcept
{
    EditorSettings settings{};
    try {
        const auto path = editorSettingsPath();
        if (path.empty()) {
            return settings;
        }
        auto bytes = Tina::Core::readFile(pathToUtf8(path), {.maxBytes = 128U * 1024U});
        if (!bytes) {
            return settings;
        }
        const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        if (!Tina::Core::isStrictUtf8WithoutNul(text)) {
            return settings;
        }
        const auto value = [&text](std::string_view key) -> std::string_view {
            const std::string prefix = std::string(key) + "=";
            const auto begin = text.find(prefix);
            if (begin == std::string::npos) return {};
            const auto start = begin + prefix.size();
            const auto end = text.find('\n', start);
            return std::string_view{text}.substr(start, end == std::string::npos ? text.size() - start : end - start);
        };
        if (value("version") != std::to_string(EditorSettings::SchemaVersion)) return settings;
        const auto parseFloat = [&value](std::string_view key, float fallback) {
            const auto raw = value(key);
            if (raw.empty()) return fallback;
            char* end = nullptr;
            const std::string copy(raw);
            const float parsed = std::strtof(copy.c_str(), &end);
            return end != copy.c_str() && std::isfinite(parsed) ? parsed : fallback;
        };
        settings.leftDockFraction = std::clamp(parseFloat("leftDock", settings.leftDockFraction), 0.1F, 0.8F);
        settings.inspectorFraction = std::clamp(parseFloat("inspector", settings.inspectorFraction), 0.2F, 0.95F);
        settings.bottomPanelFraction = std::clamp(parseFloat("bottom", settings.bottomPanelFraction), 0.2F, 0.95F);
        settings.leftDockVisible = value("leftVisible") != "0";
        settings.inspectorVisible = value("inspectorVisible") != "0";
        settings.snapEnabled = value("snap") == "1";
        const auto panel = value("bottomPanel");
        settings.bottomPanel = panel == "animation" ? BottomPanelKind::Animation : panel == "output" ? BottomPanelKind::Output : BottomPanelKind::None;
        settings.layoutDebuggerVisible = value("layoutDebugger") == "1";
        for (u32 index = 0; index < RecentProjectCapacity; ++index) {
            const std::string key = "recent" + std::to_string(index);
            const auto recent = value(key);
            if (!recent.empty() && Tina::Core::isStrictUtf8WithoutNul(recent)) {
                settings.recentProjects[settings.recentProjectCount++] = std::string(recent);
            }
        }
    } catch (...) {
        return EditorSettings{};
    }
    return settings;
}

[[nodiscard]] inline Tina::Core::Status saveEditorSettings(const EditorSettings& settings) noexcept
{
    try {
        const auto path = editorSettingsPath();
        if (path.empty()) return Tina::Core::success();
        std::string text = "version=" + std::to_string(EditorSettings::SchemaVersion) +
            "\nleftDock=" + std::to_string(settings.leftDockFraction) +
            "\ninspector=" + std::to_string(settings.inspectorFraction) +
            "\nbottom=" + std::to_string(settings.bottomPanelFraction) +
            "\nleftVisible=" + std::to_string(settings.leftDockVisible ? 1 : 0) +
            "\ninspectorVisible=" + std::to_string(settings.inspectorVisible ? 1 : 0) +
            "\nlayoutDebugger=" + std::to_string(settings.layoutDebuggerVisible ? 1 : 0) +
            "\nsnap=" + std::to_string(settings.snapEnabled ? 1 : 0) + "\nbottomPanel=" +
            std::string(settings.bottomPanel == BottomPanelKind::Animation ? "animation" : settings.bottomPanel == BottomPanelKind::Output ? "output" : "none") + "\n";
        for (u32 index = 0; index < settings.recentProjectCount && index < RecentProjectCapacity; ++index) {
            text += "recent" + std::to_string(index) + "=" + settings.recentProjects[index] + "\n";
        }
        return Tina::Core::writeFile(pathToUtf8(path), std::as_bytes(std::span{text.data(), text.size()}));
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory, "Editor settings serialization allocation failed");
    } catch (...) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Io, "Editor settings serialization failed");
    }
}
[[nodiscard]] inline bool isWorkspaceContextDocumentTab(
    const Tina::Editor::EditorDocumentTabDesc& tab) noexcept
{
    if (!tab.pinned) {
        return false;
    }
    switch (tab.key.kind) {
    case Tina::Editor::EditorDocumentKind::World2D:
    case Tina::Editor::EditorDocumentKind::World3D:
    case Tina::Editor::EditorDocumentKind::TileMap2D:
    case Tina::Editor::EditorDocumentKind::SpriteAnimation2D:
        return true;
    case Tina::Editor::EditorDocumentKind::AssetInspector:
    default:
        return false;
    }
}
enum class RgbaCaptureStage : u8 {
    Workspace,
    ColorPicker,
    DeleteDialog,
};
[[nodiscard]] constexpr std::string_view rgbaCaptureStageName(
    RgbaCaptureStage stage) noexcept
{
    switch (stage) {
    case RgbaCaptureStage::Workspace:
        return "workspace";
    case RgbaCaptureStage::ColorPicker:
        return "color-picker";
    case RgbaCaptureStage::DeleteDialog:
        return "delete-dialog";
    }
    return "workspace";
}
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
    Tina::Math::Vec3 pivot{};
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
struct ViewportOrientationCompassVisualState final {
    WorkspaceMode workspace = WorkspaceMode::World2D;
    float viewportWidth = 0.0F;
    float viewportHeight = 0.0F;
    float cameraYawRadians = 0.0F;
    float cameraPitchRadians = 0.0F;

    friend bool operator==(const ViewportOrientationCompassVisualState&,
                           const ViewportOrientationCompassVisualState&) = default;
};
inline constexpr UI::UITreeViewItemKey HierarchyDocumentRootKey =
    (std::numeric_limits<UI::UITreeViewItemKey>::max)();
struct EditorLaunchOptions final {
    u64 targetFrameCount = DefaultFrameCount;
    u32 frameDelayMilliseconds = DefaultFrameDelayMilliseconds;
    std::string world2DDocumentPathUtf8{};
    std::string world3DDocumentPathUtf8{};
    std::string catalogRootUtf8{};
    std::string rgbaOutputUtf8{};
    Tina::EditorApp::Detail::EditorSourceImportLaunchOptions sourceImport{};
    WorkspaceMode initialWorkspace = WorkspaceMode::World2D;
    RgbaCaptureStage rgbaStage = RgbaCaptureStage::Workspace;
    bool autoDemo = false;
    bool profileUi = false;
    // Deterministic profile-only workload: repeatedly moves the Layout
    // Debugger overlay so layout/paint/display-list costs can be compared
    // without relying on an external pointer automation harness.
    bool profileUiLayoutDrag = false;
};
enum class OutputFilter : u8 {
    All,
    Info,
    Warning,
    Error,
};

enum class EditorOutputTargetKind : u8 {
    None,
    Asset,
    Document,
    SceneNode,
};

struct EditorOutputHistoryEntry final {
    u64 sequence = 0U;
    OutputFilter filter = OutputFilter::Info;
    std::string context{};
    std::string message{};
    EditorOutputTargetKind targetKind = EditorOutputTargetKind::None;
    Tina::Core::AssetId targetAssetId{};
    Tina::Editor::EditorDocumentKey targetDocumentKey{};
    u32 targetStableId = 0U;
};

inline constexpr Tina::Core::usize EditorOutputHistoryCapacity = 64U;
class EditorRenderDeviceAccess final {
  public:
    void set(Tina::Render::IRenderDevice* device) noexcept { device_ = device; }
    [[nodiscard]] Tina::Render::IRenderDevice* get() const noexcept { return device_; }

  private:
    Tina::Render::IRenderDevice* device_ = nullptr;
};
[[nodiscard]] constexpr Tina::AssetFormat::TargetPlatform editorTargetPlatform() noexcept;
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
    std::vector<Tina::Asset::SourceImportPipelineUnitOutput> sourceImportUnitOutputs{};
    std::vector<EditorAssetMetadataRecord> assetMetadata{};
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
        Tina::Core::AssetId mediaAssetId{};
        if (kind == Tina::EditorApp::Detail::EditorSourceImportUnitKind::Texture ||
            kind == Tina::EditorApp::Detail::EditorSourceImportUnitKind::Audio) {
            if (metadataUnit->outputCount != 1U) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Editor media source-import metadata must own exactly one output");
            }
            const auto output = metadata.outputForUnit(unitIndex, 0U);
            const auto expectedKind =
                kind == Tina::EditorApp::Detail::EditorSourceImportUnitKind::Texture
                    ? Tina::AssetFormat::AssetKind::Texture2D
                    : Tina::AssetFormat::AssetKind::AudioClip;
            if (!output || output->assetKind != expectedKind) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Editor media source-import metadata has an incompatible output");
            }
            auto derivedId =
                kind == Tina::EditorApp::Detail::EditorSourceImportUnitKind::Texture
                    ? Tina::Asset::deriveTextureMediaAssetId(*primaryPath)
                    : Tina::Asset::deriveAudioMediaAssetId(*primaryPath);
            if (!derivedId) {
                return Tina::Core::failure(std::move(derivedId.error()));
            }
            if (output->assetId != *derivedId) {
                mediaAssetId = output->assetId;
            }
        }
        units.push_back({
            .kind = kind,
            .sourcePathUtf8 = pathToUtf8(absolutePath),
            .mediaAssetId = mediaAssetId,
        });
    }
    return units;
}
[[nodiscard]] inline Tina::Core::Result<std::vector<Tina::Asset::SourceImportPipelineUnitOutput>>
restoreSourceImportUnitOutputs(
    const Tina::Editor::EditorProjectWorkspace& workspace,
    const Tina::AssetFormat::SourceImportMetadataView& metadata)
{
    try {
        std::vector<Tina::Asset::SourceImportPipelineUnitOutput> outputs;
        outputs.reserve(metadata.header().unitCount);
        const auto sourceRoot = std::filesystem::u8path(
            workspace.sourceRootUtf8().begin(), workspace.sourceRootUtf8().end())
                                    .lexically_normal();
        for (u32 unitIndex = 0; unitIndex < metadata.header().unitCount; ++unitIndex) {
            const auto metadataUnit = metadata.unit(unitIndex);
            if (!metadataUnit) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Editor source-import output unit disappeared after validation");
            }
            std::optional<std::string_view> primaryPath{};
            for (u32 inputIndex = 0; inputIndex < metadataUnit->inputCount; ++inputIndex) {
                const auto input = metadata.unitInputForUnit(unitIndex, inputIndex);
                if (!input || !Tina::AssetFormat::hasSourceImportInputFlag(
                                   input->flags,
                                   Tina::AssetFormat::SourceImportInputFlags::Primary)) {
                    continue;
                }
                primaryPath = metadata.sourcePath(input->sourceIndex);
                break;
            }
            if (!primaryPath.has_value()) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Editor source-import output unit has no primary input");
            }
            const auto relativePath = std::filesystem::u8path(
                primaryPath->begin(), primaryPath->end());
            const auto absolutePath = (sourceRoot / relativePath).lexically_normal();
            if (relativePath.is_absolute() || !pathIsSameOrDescendant(absolutePath, sourceRoot) ||
                absolutePath == sourceRoot) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::PermissionDenied,
                    "Editor source-import output path escaped the Source root");
            }
            Tina::Asset::SourceImportPipelineUnitOutput mapping{
                .unitId = metadataUnit->unitId,
                .sourceUtf8Path = pathToUtf8(absolutePath),
            };
            mapping.outputs.reserve(metadataUnit->outputCount);
            for (u32 outputIndex = 0; outputIndex < metadataUnit->outputCount; ++outputIndex) {
                const auto output = metadata.outputForUnit(unitIndex, outputIndex);
                if (!output) {
                    return Tina::Core::failure(
                        Tina::Core::CoreErrorCode::Internal,
                        "Editor source-import output disappeared after validation");
                }
                mapping.outputs.push_back({
                    .assetId = output->assetId,
                    .assetKind = output->assetKind,
                });
            }
            outputs.push_back(std::move(mapping));
        }
        return outputs;
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Editor source-import output mapping allocation failed");
    }
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
        auto unitOutputs = restoreSourceImportUnitOutputs(workspace, *metadata);
        if (!unitOutputs) {
            return Tina::Core::failure(std::move(unitOutputs.error()));
        }
        auto assetMetadata = loadEditorAssetMetadata(workspace);
        if (!assetMetadata) {
            return Tina::Core::failure(std::move(assetMetadata.error()));
        }
        return ResolvedEditorProjectCatalog{
            .catalogRootUtf8 = pathToUtf8(catalogRoot.lexically_normal()),
            .sourceImportCatalogRootUtf8 = pathToUtf8(catalogRoot.lexically_normal()),
            .sourceImportStatePathUtf8 = pathToUtf8(statePath.lexically_normal()),
            .sourceImportUnits = std::move(*units),
            .sourceImportUnitOutputs = std::move(*unitOutputs),
            .assetMetadata = std::move(*assetMetadata),
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
[[nodiscard]] inline Tina::Core::Result<std::filesystem::path> createUniqueEditorTempDirectory(
    std::string_view directoryPrefix = "tina_editor_catalog_")
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
            tempRoot / (std::string{directoryPrefix} + std::to_string(seed) + "_" +
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
                                    "Tina Editor could not create a temporary directory"};
            error.setNativeCode(createError.value());
            error.addContext("candidate", pathToUtf8(candidate));
            return Tina::Core::failure(std::move(error));
        }
    }
    return Tina::Core::failure(Tina::Core::CoreErrorCode::AlreadyExists,
                               "Tina Editor exhausted temporary directory attempts");
}
[[nodiscard]] inline Tina::Core::Result<Tina::Asset::CatalogCookRequest>
createEditorAutoDemoCatalogFixtureRequest()
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
    recipe += " unlit opaque 0.26 0.68 0.92 1.0\n";
    return Tina::Asset::parseCatalogCookRecipe(recipe, ".");
}

[[nodiscard]] inline Tina::Core::Status publishEmptyEditorCatalog(
    std::string_view catalogRootUtf8,
    Tina::AssetFormat::TargetPlatform targetPlatform)
{
    auto manifest = Tina::AssetFormat::writeCookedManifestBytes({
        .targetPlatform = targetPlatform,
    });
    if (!manifest) {
        return Tina::Core::failure(std::move(manifest.error()));
    }
    return Tina::Asset::publishCatalogPackage(
        catalogRootUtf8, Tina::Asset::DefaultCatalogManifestRelativePath,
        *manifest, {}, {.writeObjects = false});
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
    std::vector<Tina::Asset::SourceImportPipelineUnitOutput> initialSourceImportUnitOutputs{};
    std::vector<EditorAssetMetadataRecord> initialAssetMetadata{};
    std::optional<Tina::Editor::EditorProjectWorkspace> initialProjectWorkspace{};
    u32 catalogEntryCount = 0;
    bool projectCatalogConfigured = false;
    bool testFixtureCatalog = false;

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
    resources.testFixtureCatalog = options.autoDemo && !resources.projectCatalogConfigured;
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
        resources.initialSourceImportUnitOutputs =
            std::move(resolvedCatalog->sourceImportUnitOutputs);
        resources.initialAssetMetadata = std::move(resolvedCatalog->assetMetadata);
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
        if (resources.testFixtureCatalog) {
            auto request = createEditorAutoDemoCatalogFixtureRequest();
            if (!request) {
                return Tina::Core::failure(std::move(request.error()));
            }
            if (auto status = Tina::Asset::cookAndPublishCatalogPackage(
                    resources.catalogRootUtf8, *request);
                !status) {
                return status;
            }
        } else if (auto status = publishEmptyEditorCatalog(
                       resources.catalogRootUtf8, editorTargetPlatform());
                   !status) {
            return status;
        }
    }

    auto system = Tina::Asset::AssetSystem::Create({
        .storeCapacity = 128,
        .memoryResource = &resources.memory,
        .batch = {
            .file = {
                .memoryResource = std::pmr::new_delete_resource(),
            },
            .memoryResource = &resources.memory,
        },
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
struct EditorProcessMemorySnapshot final {
    u64 workingSetBytes = 0;
    u64 peakWorkingSetBytes = 0;
    u64 privateBytes = 0;
    bool sampled = false;
};

struct EditorFrameTimingStatistics final {
    u64 sampleCount = 0;
    double totalSeconds = 0.0;
    double maximumSeconds = 0.0;
};

struct LifecycleCounters final {
    u64 frameUpdates = 0;
    double lastFrameSeconds = 0.0;
    // Profile-only CPU timing for the Editor UI orchestration and the layout
    // debugger refresh. These are intentionally separate from Runtime phase
    // timings so a Tracy capture can be correlated with the JSON report without
    // claiming that they cover UI commit or Render submit.
    EditorFrameTimingStatistics updateUiTiming{};
    EditorFrameTimingStatistics layoutDebuggerUiTiming{};
    EditorFrameTimingStatistics layoutDebuggerDragFrameTiming{};
    u64 layoutDebugSnapshotChangedFrames = 0;
    u64 layoutDebugProjectionChangedFrames = 0;
    u64 layoutDebugDragFrames = 0;
    u64 layoutDebugAllBoundsSuppressedFrames = 0;
    u64 layoutDebugProfileMutationFrames = 0;
    u64 layoutDebugProfileCommittedSamples = 0;
    u64 layoutDebugProfileLayoutRebuildFrames = 0;
    u64 layoutDebugProfileLayoutMeasuredNodes = 0;
    u64 layoutDebugProfileLayoutArrangedNodes = 0;
    u64 layoutDebugProfileHitRebuildFrames = 0;
    u64 layoutDebugProfilePaintSnapshotRebuildFrames = 0;
    bool layoutDebugProfileCompleted = false;
    u64 uiStatisticsSamples = 0;
    UI::UIContextStatistics uiStatisticsFirst{};
    UI::UIContextStatistics uiStatisticsLast{};
    Tina::Core::usize uiStatisticsPeakPmrBytes = 0;
    u64 processWorkingSetBytes = 0;
    u64 processPrivateBytes = 0;
    u64 processPeakWorkingSetBytes = 0;
    u64 processPeakPrivateBytes = 0;
    EditorProcessMemorySnapshot processAfterOptions{};
    EditorProcessMemorySnapshot processAfterCatalog{};
    EditorProcessMemorySnapshot processAfterEngineCreate{};
    EditorProcessMemorySnapshot processFirstUiFrame{};
    EditorProcessMemorySnapshot processLastUiFrame{};
    EditorProcessMemorySnapshot processAfterRun{};
    EditorProcessMemorySnapshot processAfterEngineDestroy{};
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
    u32 rgbaCaptureWidth = 0;
    u32 rgbaCaptureHeight = 0;
    u64 rgbaCaptureBytes = 0;
    bool rgbaCaptureAttempted = false;
    bool rgbaCaptureOk = false;
    bool rgbaCaptureOutputWritten = false;
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
    // Animators built for the current play session, and how many times one
    // advanced onto a new frame. Both are zero while editing.
    Tina::Core::usize playAnimatorCount = 0;
    u64 playAnimatorFrameChanges = 0;
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
    bool testFixtureCatalog = false;
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
    u64 sourceImportCookedPayloadBytes = 0;
    u64 sourceImportTransientMemoryPeakBytes = 0;
    u64 sourceImportTransientMemoryBytesAfterRelease = 0;
    u64 sourceImportResidentCookedFileBytesBefore = 0;
    u64 sourceImportResidentCookedFileBytesAfterCommit = 0;
    EditorProcessMemorySnapshot sourceImportProcessBefore{};
    EditorProcessMemorySnapshot sourceImportProcessAfterWorker{};
    EditorProcessMemorySnapshot sourceImportProcessAfterCommit{};
    EditorProcessMemorySnapshot sourceImportProcessPeak{};
    EditorFrameTimingStatistics frameTimingOverall{};
    EditorFrameTimingStatistics frameTimingBeforeSourceImport{};
    EditorFrameTimingStatistics frameTimingDuringSourceImport{};
    EditorFrameTimingStatistics frameTimingAfterSourceImport{};
    bool sourceImportRunning = false;
    bool sourceImportReady = false;
    bool sourceImportStateCommitted = false;
    bool navigationBakeReady = false;
    bool navigationBakeDirty = true;
};

[[nodiscard]] inline EditorProcessMemorySnapshot queryEditorProcessMemory() noexcept
{
    EditorProcessMemorySnapshot snapshot{};
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX processMemory{};
    processMemory.cb = static_cast<DWORD>(sizeof(processMemory));
    if (::GetProcessMemoryInfo(::GetCurrentProcess(),
                               reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&processMemory),
                               static_cast<DWORD>(sizeof(processMemory))) != FALSE) {
        snapshot.workingSetBytes = static_cast<u64>(processMemory.WorkingSetSize);
        snapshot.peakWorkingSetBytes =
            static_cast<u64>(processMemory.PeakWorkingSetSize);
        snapshot.privateBytes = static_cast<u64>(processMemory.PrivateUsage);
        snapshot.sampled = true;
    }
#endif
    return snapshot;
}

inline void recordEditorProcessMemory(
    LifecycleCounters& counters,
    const EditorProcessMemorySnapshot& snapshot) noexcept
{
    if (!snapshot.sampled) {
        return;
    }
    counters.processWorkingSetBytes = snapshot.workingSetBytes;
    counters.processPrivateBytes = snapshot.privateBytes;
    counters.processPeakWorkingSetBytes =
        (std::max)(counters.processPeakWorkingSetBytes,
                   snapshot.peakWorkingSetBytes);
    counters.processPeakPrivateBytes =
        (std::max)(counters.processPeakPrivateBytes, snapshot.privateBytes);
}

inline void recordEditorProcessMemoryMaximum(
    EditorProcessMemorySnapshot& maximum,
    const EditorProcessMemorySnapshot& snapshot) noexcept
{
    if (!snapshot.sampled) {
        return;
    }
    maximum.sampled = true;
    maximum.workingSetBytes =
        (std::max)(maximum.workingSetBytes, snapshot.workingSetBytes);
    maximum.peakWorkingSetBytes =
        (std::max)(maximum.peakWorkingSetBytes,
                   snapshot.peakWorkingSetBytes);
    maximum.privateBytes =
        (std::max)(maximum.privateBytes, snapshot.privateBytes);
}

inline void recordEditorFrameTiming(
    EditorFrameTimingStatistics& statistics, double frameSeconds) noexcept
{
    if (!(frameSeconds > 0.0) || !std::isfinite(frameSeconds)) {
        return;
    }
    ++statistics.sampleCount;
    statistics.totalSeconds += frameSeconds;
    statistics.maximumSeconds =
        (std::max)(statistics.maximumSeconds, frameSeconds);
}
enum class EditorCommand : u32 {
    SwitchToWorld2D,
    SwitchToWorld3D,
    MoveSelectedPositiveX,
    ApplyTransform,
    ResetTransform,
    NodeApplySprite,
    NodeApplyCamera,
    NodeApplyPointLight,
    NodeApplyShadowOccluder,
    NodeApplyAnimationProperties,
    NodeApplyPhysicsBody,
    NodeApplyPhysicsShape,
    NodeToggleSpriteVisible,
    NodeToggleSpriteFlipX,
    NodeToggleSpriteFlipY,
    NodeToggleCameraActive,
    NodeTogglePointLightActive,
    NodeToggleShadowOccluderActive,
    NodeToggleSpriteAnimationAutoPlay,
    NodeTogglePhysicsBodyEnabled,
    NodeTogglePhysicsShapeEnabled,
    NodeTogglePhysicsShapeSensor,
    NodeTogglePhysicsShapeSensorEvents,
    NodeTogglePhysicsShapeContactEvents,
    NodeTogglePhysicsShapeHitEvents,
    NodeToggleMeshVisible,
    NodeAssignSprite,
    NodePickSpriteAsset,
    SpriteAssetPickerConfirm,
    SpriteAssetPickerCancel,
    NodeAssignResource,
    NodePickResourceAsset,
    NodeToggleResourceActive,
    Undo,
    Redo,
    Save,
    SaveAs,
    SceneAdd,
    SceneAddConfirm,
    SceneAddCancel,
    SceneDuplicate,
    SceneDelete,
    SceneDeleteConfirm,
    SceneDeleteCancel,
    SceneRenameContext,
    SceneMoveUpContext,
    SceneMoveDownContext,
    SceneMoveToRootContext,
    SceneDeleteContext,
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
    ProjectTypeFilterAll,
    ProjectTypeFilterImages,
    ProjectTypeFilterModels,
    ProjectTypeFilterScenes,
    ProjectTypeFilterAudio,
    ProjectTypeFilterAnimation,
    ProjectTypeFilterOther,
    ViewportCyclePreset,
    ViewportPresetTop,
    ViewportPresetFront,
    ViewportPresetRight,
    ViewportResetView,
    RefreshProjectCatalog,
    CreateProject,
    OpenProject,
    ImportSource,
    RemoveSelectedSourceImport,
    OpenSelectedProjectAsset,
    InspectSelectedProjectAsset,
    ReimportSelectedProjectAsset,
    LocateProjectAssetSource,
    CopyProjectAssetId,
    CopyProjectAssetSourcePath,
    RevealProjectAssetDependencies,
    RenameSelectedProjectAsset,
    NewProjectAssetFolder,
    ProjectAssetRenameConfirm,
    ProjectAssetRenameCancel,
    ProjectAssetFolderConfirm,
    ProjectAssetFolderCancel,
    RemoveSelectedProjectAsset,
    ProjectAssetRemoveConfirm,
    ProjectAssetRemoveCancel,
    CloseActiveDocument,
    DirtyCloseSave,
    DirtyCloseDiscard,
    DirtyCloseCancel,
    ShowAbout,
    HideAbout,
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
// Absolute path of the diagnostic report shared with the crash handler. A single
// file keeps "the window vanished" answerable regardless of which of the two
// paths ended the process.
[[nodiscard]] inline const std::string& editorDiagnosticReportPathUtf8()
{
    static const std::string path = [] {
        std::error_code error;
        const std::filesystem::path temp = std::filesystem::temp_directory_path(error);
        return pathToUtf8((error ? std::filesystem::path{"."} : temp) /
                          "tina_editor_crash.txt");
    }();
    return path;
}

inline void writeError(const Tina::Core::Error& error)
{
    {
        Tina::Core::JsonWriter writer(std::cerr);
        writer.beginObject();
        writer.member("status", "error");
        writer.member("application", "TinaEditor");
        writer.member("domain", static_cast<std::uint16_t>(error.code.domain));
        writer.member("code", error.code.value);
        writer.member("message", error.message);
        writer.endObject();
    }
    std::cerr << '\n';

    // TinaEditor is a GUI-subsystem binary, so the stderr line above is invisible
    // in normal use: a fatal error looked exactly like a crash or a clean exit.
    // Append the same failure to the report file, including the origin and the
    // full context chain, which is what actually locates the fault.
    std::ofstream report{editorDiagnosticReportPathUtf8(), std::ios::app | std::ios::binary};
    if (!report) {
        return;
    }
    report << "\n==== Tina fatal error ====\n"
           << "  application: TinaEditor\n"
           << "  reason: the Editor exited with a fatal error\n"
           << "  domain: " << static_cast<std::uint16_t>(error.code.domain)
           << "  code: " << error.code.value << '\n'
           << "  message: " << error.message << '\n'
           << "  origin: " << error.origin.file_name() << '(' << error.origin.line() << ") in "
           << error.origin.function_name() << '\n';
    if (error.nativeCode.has_value()) {
        report << "  nativeCode: " << *error.nativeCode << '\n';
    }
    for (std::size_t index = 0; index < error.context.size(); ++index) {
        const Tina::Core::ErrorContext& entry = error.context[index];
        report << "  context[" << index << "]: " << entry.operation;
        if (!entry.detail.empty()) {
            report << " — " << entry.detail;
        }
        report << "\n    at " << entry.location.file_name() << '(' << entry.location.line()
               << ") in " << entry.location.function_name() << '\n';
    }
    {
        Tina::Core::JsonWriter writer(report);
        writer.beginObject();
        writer.member("status", "fatal");
        writer.member("code", error.code.value);
        writer.endObject();
    }
    report << '\n' << "==== end Tina fatal error ====\n";
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
[[nodiscard]] constexpr bool inspectorTransformFieldRequires3D(
    InspectorTransformField field) noexcept
{
    return field == InspectorTransformField::PositionZ ||
           field == InspectorTransformField::RotationX ||
           field == InspectorTransformField::RotationY ||
           field == InspectorTransformField::ScaleZ;
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
    EditorLaunchOptions options{};
    bool hasFrames = false;
    bool hasDelay = false;
    bool hasWorld2DPath = false;
    bool hasWorld3DPath = false;
    bool hasCatalogRoot = false;
    bool hasRgbaOutput = false;
    bool hasRgbaStage = false;
    bool hasWorkspace = false;
    bool hasAutoDemo = false;
    bool hasProfileUi = false;
    bool hasProfileUiLayoutDrag = false;
    Tina::Core::ArgScanner scanner(argumentCount, arguments);
    while (scanner.next()) {
        auto sourceImportOption =
            Tina::EditorApp::Detail::parseEditorSourceImportLaunchOption(scanner,
                                                                        options.sourceImport);
        if (!sourceImportOption) {
            return Tina::Core::failure(std::move(sourceImportOption.error()));
        }
        if (*sourceImportOption) {
            continue;
        }
        if (const auto value = scanner.value("--frames")) {
            if (hasFrames) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --frames argument");
            }
            if (!Tina::Core::parseArgUnsigned(*value, options.targetFrameCount)
                || options.targetFrameCount == 0) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frames must be an unsigned integer greater than zero");
            }
            hasFrames = true;
            continue;
        }
        if (const auto value = scanner.value("--frame-delay-ms")) {
            if (hasDelay) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --frame-delay-ms argument");
            }
            if (!Tina::Core::parseArgUnsigned(*value, options.frameDelayMilliseconds)) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--frame-delay-ms must be an unsigned integer");
            }
            hasDelay = true;
            continue;
        }
        if (const auto value = scanner.value("--world2d-path")) {
            if (hasWorld2DPath) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --world2d-path argument");
            }
            if (value->empty()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--world2d-path must not be empty");
            }
            try {
                options.world2DDocumentPathUtf8.assign(*value);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "Could not retain --world2d-path");
            }
            hasWorld2DPath = true;
            continue;
        }
        if (const auto value = scanner.value("--world3d-path")) {
            if (hasWorld3DPath) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --world3d-path argument");
            }
            if (value->empty()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--world3d-path must not be empty");
            }
            try {
                options.world3DDocumentPathUtf8.assign(*value);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "Could not retain --world3d-path");
            }
            hasWorld3DPath = true;
            continue;
        }
        if (const auto value = scanner.value("--catalog-root")) {
            if (hasCatalogRoot) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --catalog-root argument");
            }
            if (value->empty()) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--catalog-root must not be empty");
            }
            try {
                options.catalogRootUtf8.assign(*value);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "Could not retain --catalog-root");
            }
            hasCatalogRoot = true;
            continue;
        }
        if (const auto value = scanner.value("--rgba-output")) {
            if (hasRgbaOutput) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Duplicate --rgba-output argument");
            }
            if (value->empty() || !Tina::Core::isStrictUtf8WithoutNul(*value)) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "--rgba-output must be a non-empty strict UTF-8 path");
            }
            try {
                options.rgbaOutputUtf8.assign(*value);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Could not retain --rgba-output");
            }
            hasRgbaOutput = true;
            continue;
        }
        if (const auto value = scanner.value("--rgba-stage")) {
            if (hasRgbaStage) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Duplicate --rgba-stage argument");
            }
            if (*value == "workspace") {
                options.rgbaStage = RgbaCaptureStage::Workspace;
            } else if (*value == "color-picker") {
                options.rgbaStage = RgbaCaptureStage::ColorPicker;
            } else if (*value == "delete-dialog") {
                options.rgbaStage = RgbaCaptureStage::DeleteDialog;
            } else {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "--rgba-stage must be workspace, color-picker, or delete-dialog");
            }
            hasRgbaStage = true;
            continue;
        }
        if (const auto value = scanner.value("--workspace")) {
            if (hasWorkspace) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "Duplicate --workspace argument");
            }
            if (*value == "2d") {
                options.initialWorkspace = WorkspaceMode::World2D;
            } else if (*value == "3d") {
                options.initialWorkspace = WorkspaceMode::World3D;
            } else {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                           "--workspace must be 2d or 3d");
            }
            hasWorkspace = true;
            continue;
        }
        if (scanner.flag("--auto-demo")) {
            if (hasAutoDemo) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Duplicate --auto-demo argument");
            }
            options.autoDemo = true;
            hasAutoDemo = true;
            continue;
        }
        if (scanner.flag("--profile-ui")) {
            if (hasProfileUi) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Duplicate --profile-ui argument");
            }
            options.profileUi = true;
            hasProfileUi = true;
            continue;
        }
        if (scanner.flag("--profile-ui-layout-drag")) {
            if (hasProfileUiLayoutDrag) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::InvalidArgument,
                    "Duplicate --profile-ui-layout-drag argument");
            }
            options.profileUi = true;
            options.profileUiLayoutDrag = true;
            hasProfileUiLayoutDrag = true;
            continue;
        }
        // An option that appeared without its value also reaches here, so it has to be told apart
        // from a genuinely unknown one before the generic message is returned.
        if (scanner.failed()) {
            return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                       "Command-line option is missing its value");
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
    if (hasRgbaOutput && !options.autoDemo) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "--rgba-output requires --auto-demo");
    }
    if (hasRgbaStage && !hasRgbaOutput) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "--rgba-stage requires --rgba-output");
    }
    if (options.autoDemo &&
        options.targetFrameCount < AutomaticAuthoringMinimumFrameCount) {
        std::string message = "--auto-demo requires --frames of at least ";
        message += std::to_string(AutomaticAuthoringMinimumFrameCount);
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   std::move(message));
    }
    if (options.profileUiLayoutDrag && options.autoDemo) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "--profile-ui-layout-drag cannot be combined with --auto-demo");
    }
    if (options.profileUiLayoutDrag && options.sourceImport.importOnStart) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "--profile-ui-layout-drag cannot be combined with startup source import");
    }
    if (options.profileUiLayoutDrag &&
        (!hasFrames || options.targetFrameCount < LayoutDebugProfileMinimumFrameCount)) {
        std::string message =
            "--profile-ui-layout-drag requires --frames of at least ";
        message += std::to_string(LayoutDebugProfileMinimumFrameCount);
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument, std::move(message));
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
[[nodiscard]] inline UI::UILayoutStyle hierarchyRenameLayout(
    UI::UIVisibility visibility, float offsetX = 0.0F, float offsetY = 0.0F,
    float width = 0.0F, float height = 0.0F) noexcept
{
    UI::UILayoutStyle style = fixedSize(width, height);
    style.placement = UI::UILayoutPlacement::Overlay;
    style.visibility = visibility;
    style.overlay.horizontal = UI::UIAxisAlignment::Start;
    style.overlay.vertical = UI::UIAxisAlignment::Start;
    style.overlay.offset.x = UI::UILayoutLength::Px(offsetX);
    style.overlay.offset.y = UI::UILayoutLength::Px(offsetY);
    return style;
}
[[nodiscard]] inline UI::UILayoutStyle sceneAddTemplateRowLayout(
    UI::UIVisibility visibility, Tina::Core::usize visibleIndex = 0U) noexcept
{
    constexpr UI::UITheme theme =
        UI::makeModernDesktopTheme(UI::UIColorScheme::Dark, UI::UIDensity::Compact);
    UI::UILayoutStyle style = fillWidth(theme.controls.buttonHeight);
    style.visibility = visibility;
    style.gridItem.row = static_cast<u8>(visibleIndex / 2U);
    style.gridItem.column = static_cast<u8>(visibleIndex % 2U);
    style.gridItem.alignSelf = UI::UIAlignSelf::Stretch;
    return style;
}
// One row per node template. Sized for the widest registry (World2D).
inline constexpr Tina::Core::usize SceneAddTemplateSlotCount =
    Tina::Editor::World2DNodeTemplateCount;
[[nodiscard]] inline UI::UILayoutStyle editorDialogOverlayLayout() noexcept
{
    UI::UILayoutStyle style = percentSize(100.0F, 100.0F);
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.horizontal = UI::UIAxisAlignment::Stretch;
    style.overlay.vertical = UI::UIAxisAlignment::Stretch;
    return style;
}
inline constexpr u32 DirtyCloseSaveActionIndex = 0U;
inline constexpr u32 DirtyCloseDiscardActionIndex = 1U;
inline constexpr u32 DirtyCloseCancelActionIndex = 2U;
inline constexpr u32 SceneDeleteCancelActionIndex = 0U;
inline constexpr u32 SceneDeleteConfirmActionIndex = 1U;
inline constexpr u32 AboutCloseActionIndex = 0U;
inline constexpr u32 ProjectAssetDialogCancelActionIndex = 0U;
inline constexpr u32 ProjectAssetDialogConfirmActionIndex = 1U;
inline constexpr u32 HelpMainMenuIndex = 3U;
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
    // Node template display name, shared with the creation picker so the
    // hierarchy and the Inspector report the same kind vocabulary.
    std::string_view kindName{};
};
struct SceneDeleteConfirmation final {
    Tina::Editor::EditorDocumentKey documentKey{};
    WorkspaceMode workspace = WorkspaceMode::World2D;
    u32 stableId = 0;
    u64 documentRevision = 0;
    bool confirming = false;
};
// Pending "create node" choice. Captured when the picker opens so a document
// that changes underneath it is detected before anything is published.
struct SceneAddRequest final {
    Tina::Editor::EditorDocumentKey documentKey{};
    WorkspaceMode workspace = WorkspaceMode::World2D;
    u32 parentStableId = 0;
    u64 documentRevision = 0;
    bool creating = false;
};

enum class ProjectAssetViewMode : u8 {
    Grid = 0,
    List = 1,
};
enum class HierarchyDropIntent : u8 {
    Reparent,
    ReorderBefore,
    ReorderAfter,
};
struct HierarchyDropRequest final {
    u32 sourceStableId = 0;
    u32 targetStableId = 0;
    HierarchyDropIntent intent = HierarchyDropIntent::Reparent;
};
enum class ProjectAssetDropTarget : u8 {
    Invalid,
    HierarchyNode,
    InspectorResourceSlot,
    Viewport,
};
struct ProjectAssetDropRequest final {
    Tina::Core::AssetId assetId{};
    ProjectAssetDropTarget target = ProjectAssetDropTarget::Invalid;
    u32 targetStableId = 0;
    UI::UILogicalPoint viewportPosition{};
};

struct ProjectAssetRemoveConfirmation final {
    Tina::Core::AssetId assetId{};
    std::string sourcePathUtf8{};
    bool confirming = false;
};

enum class FileDropFeedbackState : u8 {
    Hidden,
    Accepted,
    Processing,
    Committed,
    Rejected,
    Failed,
};

// Per-AssetId import history kept by the active Editor project. The source
// import worker still owns all phase transitions; this is only the owner-thread
// projection used by Project Assets and the source-import grid.
enum class EditorAssetImportStatus : u8 {
    Ready = 0,
    Queued = 1,
    Preparing = 2,
    Copying = 3,
    Cooking = 4,
    ReadyToCommit = 5,
    Committed = 6,
    Error = 7,
};

struct EditorAssetImportHistoryEntry final {
    Tina::Core::AssetId assetId{};
    EditorAssetImportStatus status = EditorAssetImportStatus::Ready;
};

struct PendingFileDrop final {
    Tina::Platform::WindowId window{};
    double logicalX = 0.0;
    double logicalY = 0.0;
    std::vector<std::string> pathsUtf8{};
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
// Pose owner for one previewed SkinnedMesh3D node. A Prefab authors no clip, so the
// Editor holds the skeleton's own bind pose and composes it once at build time: the
// palette is then a constant this outlives every frame that borrows it.
struct World3DPreviewSkinnedPose final {
    Tina::Scene::EntityId entity{};
    std::vector<float> palette{};
};
struct PreviewAssetReference final {
    Tina::Core::AssetId assetId{};
    Tina::AssetFormat::AssetKind kind = Tina::AssetFormat::AssetKind::Invalid;

    friend bool operator==(const PreviewAssetReference&, const PreviewAssetReference&) = default;
};
[[nodiscard]] inline Tina::Core::Result<InitialAuthoringDocuments>
createAuthoringDocuments(const EditorLaunchOptions& options)
{
    const bool useAutoDemoFixtures = options.autoDemo;
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
            .nodeKind = Tina::AssetFormat::World2DNodeKind::Camera2D,
            .positionY = 4.0F,
            .camera = Tina::AssetFormat::World2DCameraDesc{
                .fixedWorldHeightMeters = PreviewWorldHeight,
            },
        },
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 3,
            .parentStableEntityId = 1,
            .nodeKind = Tina::AssetFormat::World2DNodeKind::Sprite2D,
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
            .nodeKind = Tina::AssetFormat::World2DNodeKind::PointLight2D,
            .positionX = 2.0F,
            .positionY = 2.0F,
            .pointLight = Tina::AssetFormat::World2DPointLightDesc{},
        },
        Tina::AssetFormat::World2DEntityDesc{
            .stableEntityId = 7,
            .parentStableEntityId = 1,
        },
    };
    if (!world2DSession.loadedFromPath && useAutoDemoFixtures) {
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
            .nodeKind = Tina::AssetFormat::PrefabNodeKind::Mesh3D,
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
            .nodeKind = Tina::AssetFormat::PrefabNodeKind::Mesh3D,
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
            .nodeKind = Tina::AssetFormat::PrefabNodeKind::Mesh3D,
            .positionX = 2.3F,
            .positionZ = -1.6F,
            .scaleX = 0.8F,
            .scaleY = 0.8F,
            .scaleZ = 0.8F,
            .meshId = meshId,
            .materialId = materialId,
        },
    };
    if (!world3DSession.loadedFromPath && useAutoDemoFixtures) {
        auto prefabBytes = Tina::AssetFormat::writePrefabPayloadBytes({.nodes = nodes});
        if (!prefabBytes) {
            return Tina::Core::failure(std::move(prefabBytes.error()));
        }
        if (auto status = world3D->loadPayload(*prefabBytes); !status) {
            return Tina::Core::failure(std::move(status.error()));
        }
    }
    std::vector<Tina::Editor::TileMapAuthoringLayer> initialTileMapLayers;
    if (useAutoDemoFixtures) {
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
        initialTileMapLayers = {
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
        };
    } else {
        initialTileMapLayers = {
            Tina::Editor::TileMapAuthoringLayer{
                .stableLayerId = InitialTileMapLayerId,
                .kind = Tina::AssetFormat::TileMapLayerKind::Tile,
                .name = "Ground",
            },
        };
    }
    auto tileMap = Tina::Editor::TileMapAuthoringDocument::Create(
        Tina::Editor::TileMapAuthoringDesc{
            .tileMapId = editorAssetId(0x42U),
            .tilesetId = editorAssetId(0x41U),
            .widthCells = InitialTileMapWidthCells,
            .heightCells = InitialTileMapHeightCells,
            .cellSizeMeters = 1.0F,
            .chunkSizeCells = 4,
            .layers = std::move(initialTileMapLayers),
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
    std::vector<Tina::AssetFormat::SpriteAnimationFrameDesc> initialAnimationFrames;
    if (useAutoDemoFixtures) {
        initialAnimationFrames = {
            {.spriteId = editorAssetId(0x22U), .durationSeconds = 0.12F},
            {.spriteId = editorAssetId(0x23U), .durationSeconds = 0.12F},
            {.spriteId = editorAssetId(0x24U), .durationSeconds = 0.12F},
            {.spriteId = editorAssetId(0x25U), .durationSeconds = 0.18F},
        };
    } else {
        initialAnimationFrames = {
            {.spriteId = editorAssetId(0x01U), .durationSeconds = 0.1F},
        };
    }
    auto spriteAnimation = Tina::Editor::SpriteAnimationAuthoringDocument::Create(
        Tina::Editor::SpriteAnimationAuthoringDesc{
            .clipId = editorAssetId(0x50U),
            .playbackMode = Tina::AssetFormat::SpriteAnimationPlaybackMode::Loop,
            .frames = std::move(initialAnimationFrames),
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
createProjectAssetBrowser(
    const Tina::Asset::CatalogSnapshot& catalog,
    std::span<const Tina::Asset::SourceImportPipelineUnitOutput> sourceMappings = {},
    std::span<const EditorAssetMetadataRecord> metadata = {},
    std::string_view sourceRootUtf8 = {})
{
    try {
        std::optional<std::filesystem::path> physicalSourceRoot;
        if (!sourceRootUtf8.empty()) {
            const auto sourceRoot = std::filesystem::u8path(
                sourceRootUtf8.begin(), sourceRootUtf8.end());
            std::error_code canonicalError;
            auto resolvedSourceRoot = std::filesystem::weakly_canonical(
                sourceRoot, canonicalError);
            if (canonicalError) {
                Tina::Core::Error failure{
                    Tina::Core::CoreErrorCode::Io,
                    "Tina Editor could not resolve the Project Source root"};
                failure.setNativeCode(canonicalError.value());
                return Tina::Core::failure(std::move(failure));
            }
            physicalSourceRoot.emplace(std::move(resolvedSourceRoot));
        }

        std::vector<Tina::Editor::ProjectAssetDescriptor> assets;
        assets.reserve(catalog.entryCount());
        for (u32 index = 0; index < catalog.entryCount(); ++index) {
            const auto entry = catalog.entry(index);
            if (!entry) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                           "Tina Editor Catalog entry disappeared");
            }
            std::string displayName{Tina::Editor::projectAssetKindLabel(entry->assetKind)};
            std::string sourcePathUtf8;
            std::string folderPathUtf8;
            for (const auto& mapping : sourceMappings) {
                const auto output = std::find_if(
                    mapping.outputs.begin(), mapping.outputs.end(),
                    [entry](const auto& candidate) {
                        return candidate.assetId == entry->assetId;
                    });
                if (output == mapping.outputs.end()) {
                    continue;
                }
                sourcePathUtf8 = mapping.sourceUtf8Path;
                if (physicalSourceRoot.has_value()) {
                    const auto absolute = std::filesystem::u8path(
                        sourcePathUtf8.begin(), sourcePathUtf8.end());
                    // Windows can expose the same directory through an 8.3 alias
                    // in the workspace and a long path in the import mapping.
                    // Resolve both sides before deriving the Source-relative folder
                    // so the browser never receives synthetic ".." components.
                    std::error_code canonicalError;
                    const auto physicalSource = std::filesystem::weakly_canonical(
                        absolute, canonicalError);
                    if (canonicalError) {
                        Tina::Core::Error failure{
                            Tina::Core::CoreErrorCode::Io,
                            "Tina Editor could not resolve an imported source path"};
                        failure.setNativeCode(canonicalError.value());
                        return Tina::Core::failure(std::move(failure));
                    }
                    const auto relative =
                        physicalSource.lexically_relative(*physicalSourceRoot);
                    if (!relative.empty() && relative != "." &&
                        !relative.has_root_path() && relative.parent_path() != ".") {
                        folderPathUtf8 = pathToUtf8(relative.parent_path());
                    }
                }
                const auto filename = std::filesystem::u8path(
                    sourcePathUtf8.begin(), sourcePathUtf8.end()).filename();
                if (!filename.empty()) {
                    displayName = pathToUtf8(filename);
                }
                break;
            }
            for (const auto& record : metadata) {
                if (record.assetId != entry->assetId) {
                    continue;
                }
                if (sourcePathUtf8.empty() && !record.displayName.empty()) {
                    displayName = record.displayName;
                }
                if (sourcePathUtf8.empty()) {
                    folderPathUtf8 = record.folderPathUtf8;
                }
                break;
            }
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
                .sourcePathUtf8 = std::move(sourcePathUtf8),
                .folderPathUtf8 = std::move(folderPathUtf8),
                .dependencies = std::move(dependencies),
            });
        }
        auto browser = Tina::Editor::ProjectAssetBrowserModel::Create(
            assets, Tina::Editor::ProjectAssetBrowserConfig{
                        .itemCapacity = (std::max)(Tina::Core::usize{4096},
                                                  assets.size()),
                        .dependencyCapacity = (std::max)(
                            Tina::Core::usize{16384},
                            static_cast<Tina::Core::usize>(catalog.dependencyCount())),
                    });
        if (!browser) {
            return Tina::Core::failure(std::move(browser.error()));
        }
        return std::move(*browser);
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
    return createProjectAssetBrowser(
        *resources.system->catalog(), resources.initialSourceImportUnitOutputs,
        resources.initialAssetMetadata,
        resources.initialProjectWorkspace.has_value()
            ? resources.initialProjectWorkspace->sourceRootUtf8()
            : std::string_view{});
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
              Tina::EditorApp::Detail::makeEditorSourceImportPipelineWorker())
    {
        hierarchyRows_.reserve(AuthoringEntityCapacity + 1U);
        collapsedHierarchyIds_.reserve(AuthoringEntityCapacity + 1U);
        assetImportHistory_.reserve(4096U);
        activeAssetImportIds_.reserve(4096U);
        activeProjectWorkspace_ = std::move(assetResources_.initialProjectWorkspace);
        assetMetadata_ = std::move(assetResources_.initialAssetMetadata);
        sourceImportUnitOutputs_ = std::move(assetResources_.initialSourceImportUnitOutputs);
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

        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createSurface(
            UI::UINodeId parent, UI::UILayoutStyle layout,
            UI::UISurfaceVariant variant);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createPanel(
            UI::UINodeId parent, UI::UILayoutStyle layout);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createLabel(
            UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
            const UI::UITextStyle& style);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createBadge(
            UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
            UI::UIBadgeTone tone = UI::UIBadgeTone::Neutral);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createButton(
            UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
            bool enabled = true,
            UI::UIStyleRoleId role = UI::UIStyleRoleId::ButtonTonal);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createSegmentedButton(
            UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
            bool enabled = true);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createIconButton(
            UI::UINodeId parent, EditorIcon icon, std::string_view accessibleName,
            UI::UILayoutStyle layout = {}, bool enabled = true,
            UI::UIButtonVariant variant = UI::UIButtonVariant::Text);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createIconToggleButton(
            UI::UINodeId parent, EditorIcon icon, std::string_view accessibleName,
            UI::UILayoutStyle layout = {}, bool enabled = true);
        [[nodiscard]] Tina::Core::Result<UI::UINodeId> createTextEdit(
            UI::UINodeId parent, std::string_view text, UI::UILayoutStyle layout,
            bool enabled);
    };

    [[nodiscard]] Tina::Core::Status buildCommandBarUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildDocumentTabsUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildMenuOverlaysUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildWorkspaceUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildLeftDockUi(
        UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& leftDock);
    [[nodiscard]] Tina::Core::Status buildViewportUi(
        UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& viewport);
    [[nodiscard]] Tina::Core::Status buildInspectorUi(
        UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& inspector);
    [[nodiscard]] Tina::Core::Status buildTimelineUi(
        UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& timeline);
    [[nodiscard]] Tina::Core::Status buildOutputPanelUi(
        UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& outputPanel);
    [[nodiscard]] Tina::Core::Status buildLayoutDebuggerUi(
        UiBuildContext& ui, UI::UINodeId parent, UI::UINodeId& layoutPanel);
    [[nodiscard]] Tina::Core::Status buildStatusBarUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildDirtyCloseModalUi(UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildSceneAddModalUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildSpriteAssetPickerUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildSceneDeleteDialogUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildProjectAssetRemoveDialogUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildProjectAssetRenameDialogUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildProjectAssetFolderDialogUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildAboutDialogUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildFileDropFeedbackUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status buildSnackbarUi(
        UiBuildContext& ui, UI::UINodeId parent);
    [[nodiscard]] Tina::Core::Status registerUiCallbacks(
        UiBuildContext& ui, UI::UINodeId rootNode);

    void onExit(Tina::GameStateExitContext&) noexcept override;
    [[nodiscard]] Tina::GameStatePolicy initialPolicy() const noexcept override;
    Tina::Core::Status updateFrame(Tina::FrameUpdateContext& context) override;
    Tina::Core::Status extractRenderScene(Tina::RenderSceneExtractionContext& context) const override;
    Tina::Core::Status updateUI(Tina::UIUpdateContext& context) override;
  private:
    void resetViewportInteractionState() noexcept;
    [[nodiscard]] Tina::Core::Status processEditorShortcuts(
        const Tina::FrameActionSnapshot& actions);
    [[nodiscard]] Tina::Core::Status processPendingInspectorSectionUpdates(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status updateHierarchySearch(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status updateProjectAssetSearch(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status updateSceneAddSearch(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status processPendingHierarchyRename(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status processPendingHierarchyDrop(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status processPendingProjectAssetDrop(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status showHierarchyRename(
        Tina::PrimaryWindowUITreeUpdater& tree, u32 stableId);
    [[nodiscard]] Tina::Core::Status refreshHierarchyRenameLayout(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshHierarchyDropIndicator(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status hideHierarchyRename(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status applyHierarchyRename(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] std::optional<u32> hierarchyStableIdAtPosition(
        UI::UILogicalPoint position) const noexcept;
    [[nodiscard]] std::optional<HierarchyDropRequest>
    hierarchyDropRequestAtPosition(UI::UILogicalPoint position) const noexcept;
    [[nodiscard]] std::optional<u64> projectAssetVisibleIndexAtPosition(
        UI::UILogicalPoint position) const noexcept;
    [[nodiscard]] std::optional<u32> viewportStableIdAtPosition(
        UI::UILogicalPoint position) const noexcept;
    void handleHierarchyPointerDown(UI::UIRoutedPointerEvent& event) noexcept;
    void handleHierarchyPointerMove(UI::UIRoutedPointerEvent& event) noexcept;
    void handleHierarchyPointerUp(UI::UIRoutedPointerEvent& event) noexcept;
    void handleHierarchyPointerCancel(UI::UIRoutedPointerEvent& event) noexcept;
    void handleProjectAssetPointerDown(UI::UIRoutedPointerEvent& event) noexcept;
    void handleProjectAssetPointerMove(UI::UIRoutedPointerEvent& event) noexcept;
    void handleProjectAssetPointerUp(UI::UIRoutedPointerEvent& event) noexcept;
    void handleProjectAssetPointerCancel(UI::UIRoutedPointerEvent& event) noexcept;
    void setFileDropFeedback(
        FileDropFeedbackState state, std::string_view message) noexcept;
    [[nodiscard]] Tina::Core::Status refreshFileDropFeedbackUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status updateSnackbarUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status processPendingMenuToggle(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshMainMenuUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshOutputAndStatusUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshLayoutDebuggerUi(
        Tina::UIUpdateContext& context,
        Tina::PrimaryWindowUITreeUpdater& tree);
    bool queueEditorCommand(EditorCommand command) noexcept;
    void queueViewportToolMode(ViewportToolMode mode) noexcept;
    [[nodiscard]] bool tileMapEditingContext() const noexcept;
    [[nodiscard]] bool playSessionActive() const noexcept;
    [[nodiscard]] bool authoringEnabled() const noexcept;
    [[nodiscard]] bool playStartReady() const noexcept;
    [[nodiscard]] UI::UIVirtualGridViewDataSource tilePaletteDataSource() const noexcept;
    static u64 tilePaletteItemCount(const void* state) noexcept;
    static bool resolveTilePaletteItem(const void* state, u64 logicalIndex,
                                       UI::UIVirtualGridViewItemDescriptor& output) noexcept;
    [[nodiscard]] bool sceneDocumentActive() const noexcept;
    [[nodiscard]] static std::string_view
    animationModeLabel(Tina::AssetFormat::SpriteAnimationPlaybackMode mode) noexcept;
    [[nodiscard]] Tina::Core::Status applyAnimationPreviewFrame(u32 frameIndex);
    [[nodiscard]] Tina::Core::Status rebuildAnimationAnimator();
    [[nodiscard]] Tina::Core::Result<Tina::AssetFormat::SpriteAnimationEventDesc>
    readAnimationEventInput(Tina::PrimaryWindowUITreeUpdater& tree) const;
    void handleAnimationTimelinePointerMove(
        UI::UIRoutedPointerEvent& event) noexcept;
    [[nodiscard]] Tina::Core::Status processPendingAnimationFrameSelection(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshAnimationTimelineUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status
    registerViewportPointerListeners(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] static Tina::Editor::EditorViewportNavigationConfig
    viewportNavigationConfig() noexcept;
    [[nodiscard]] Tina::Core::Status ensureViewportNavigation();
    [[nodiscard]] static Tina::Math::Quaternion viewportOrbitRotation(
        float yawRadians, float pitchRadians) noexcept;
    void syncViewportZoomFromNavigation() noexcept;
    void persistViewportNavigationState() noexcept;
    [[nodiscard]] Tina::Core::Status applyViewportNavigationToPreview();
    [[nodiscard]] Tina::Core::Status initializeOrApplyViewportNavigation();
    [[nodiscard]] Tina::Core::Status focusViewportOnSelection();
    [[nodiscard]] bool queueViewportNavigationInput(
        Tina::Editor::EditorViewportNavigationInput input) noexcept;
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
    [[nodiscard]] Tina::Math::Vec3 viewportSelectionPivot() const noexcept;
    [[nodiscard]] Tina::Core::Status captureViewportTransformTargets(
        ViewportTransformTransaction& transaction);
    [[nodiscard]] static Tina::Core::Result<Tina::Scene::WorldTransform>
    applyViewportWorldTransformDelta(
        const Tina::Scene::WorldTransform& baseline,
        Tina::Math::Vec3 pivot,
        const Tina::Editor::EditorTransformGizmoDelta& delta,
        Tina::Math::Quaternion localBasis) noexcept;
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
    resolvePreviewSkinnedMesh(void* userData, Tina::Asset::AssetHandle asset,
                               Tina::Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] static Tina::Core::Result<Tina::Render::FrameResourceRef>
    resolvePreviewMaterial(void* userData, Tina::Asset::AssetHandle asset,
                           Tina::Render::FrameResourceSink& sink) noexcept;
    [[nodiscard]] static std::span<const float> resolvePreviewSkinnedPose(
        void* userData, Tina::Scene::EntityId entity) noexcept;
    [[nodiscard]] ViewportProjectedPoint projectViewportWorldPoint(
        Tina::Math::Vec3 worldPoint) const noexcept;
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
    [[nodiscard]] Tina::Core::Status updateViewportPreselectionVisual(
        Tina::PrimaryWindowUITreeUpdater& tree);
    // Draws every CollisionShape2D outline in the active World2D document. The
    // shape payload is not a transform, so the gizmo cannot show it; without an
    // outline a user editing half extents or radius has no feedback at all and
    // reasonably assumes dragging the node already resizes the shape.
    [[nodiscard]] Tina::Core::Status updateViewportCollisionShapeVisuals(
        Tina::PrimaryWindowUITreeUpdater& tree);
    // Play drives every AnimatedSprite2D in the isolated preview world from the
    // session's fixed-step clock. Before this, Play only advanced a tick counter,
    // so pressing it changed nothing on screen.
    [[nodiscard]] Tina::Core::Status rebuildPlayAnimators();
    [[nodiscard]] Tina::Core::Status advancePlayAnimators(u32 steps);
    void releasePlayAnimators() noexcept;
    [[nodiscard]] Tina::Core::Status updateHierarchyPreselectionVisual(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] std::optional<u64> hierarchyIndexForStableId(
        u64 stableId) const noexcept;
    [[nodiscard]] std::optional<u32> automaticHierarchyStableId(
        bool preferLast) const noexcept;
    void synchronizeViewportSelectionFromHierarchy() noexcept;
    // Screen-space bounds, used by marquee drag and the preselection highlight
    // rectangle. Marquee stays a 2D operation: the user drags a rectangle on screen
    // and expects to catch what looks enclosed by it.
    [[nodiscard]] Tina::Core::usize collectViewportMarqueeCandidates(
        std::span<Tina::Editor::EditorMarqueeCandidate> output) const noexcept;
    // World-space bounding spheres, used by single-click picking in 3D. Separate
    // from the marquee collector because the two answer different questions:
    // "what is under this pixel, nearest first" versus "what overlaps this
    // rectangle on screen".
    [[nodiscard]] Tina::Core::usize collectViewportPickCandidates(
        std::span<Tina::Editor::EditorViewportPickCandidate> output) const noexcept;
    [[nodiscard]] std::optional<Tina::Math::Ray> viewportPickRay(
        UI::UILogicalPoint position) const noexcept;
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
    [[nodiscard]] Tina::Core::Status
    updateViewportOrientationCompass(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status updateGpuViewport(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status editTileMapBrushCell(bool erase);
    [[nodiscard]] Tina::Core::Status toggleActiveTileMapLayer();
    [[nodiscard]] Tina::Core::Status
    addTileMapLayer(Tina::AssetFormat::TileMapLayerKind kind);
    [[nodiscard]] Tina::Core::Status refreshProjectAssetUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshProjectAssetCollection(
        Tina::PrimaryWindowUITreeUpdater& tree, std::string_view feedback);
    [[nodiscard]] Tina::Core::Status refreshRecentProjectsUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status processPendingRecentProject();
    void rememberRecentProject(std::string_view projectRootUtf8) noexcept;
    [[nodiscard]] Tina::Core::Status reportAuthoringFailure(
        std::string_view prefix, const Tina::Core::Error& error);
    [[nodiscard]] Tina::Core::Result<Tina::Editor::ProjectAssetBrowserModel>
    prepareProjectBrowserForSnapshot(
        const Tina::Asset::CatalogSnapshot& catalog,
        Tina::Editor::ProjectAssetTypeFilter filter,
        std::optional<Tina::Core::AssetId> selectedAsset,
        std::string_view searchQuery = {},
        std::span<const Tina::Asset::SourceImportPipelineUnitOutput> sourceMappings = {},
        std::span<const EditorAssetMetadataRecord> metadata = {},
        std::string_view sourceRootUtf8 = {});
    [[nodiscard]] Tina::Core::Result<Tina::Editor::EditorDocumentTabs>
    prepareProjectSwitchDocumentTabs();
    [[nodiscard]] Tina::Core::Status switchCatalogAuthoringOwnersToPinnedTabs();
    [[nodiscard]] Tina::Core::Status refreshPinnedCatalogAuthoringDocuments(
        const Tina::Editor::ProjectAssetBrowserModel& browser);
    void commitProjectSwitchDocumentTabs(
        Tina::Editor::EditorDocumentTabs candidateTabs) noexcept;
    [[nodiscard]] Tina::Core::Status rebuildLiveCatalogPreview(
        std::string successFeedback);
    [[nodiscard]] Tina::Core::Result<bool> switchLiveProjectCatalog(
        Tina::Editor::EditorProjectWorkspace& workspace);
    [[nodiscard]] Tina::Core::Result<Tina::Editor::EditorProjectWorkspace>
    initializeNewProjectAt(std::string_view projectRootUtf8);
    [[nodiscard]] Tina::Core::Status scheduleNewProjectAt(
        std::string_view projectRootUtf8,
        std::vector<std::string> pendingSourceImportPathsUtf8 = {});
    [[nodiscard]] Tina::Core::Status createTemporaryProjectForImport(
        std::vector<std::string> pendingSourceImportPathsUtf8);
    [[nodiscard]] Tina::Core::Status saveTemporaryProjectFromDialog();
    [[nodiscard]] bool temporaryProjectActive() const noexcept;
    void cleanupOwnedTemporaryProject(std::string& projectRootUtf8) noexcept;
    [[nodiscard]] Tina::Core::Status createNewProjectFromDialog();
    [[nodiscard]] Tina::Core::Status openProjectFromDialog();
    [[nodiscard]] Tina::Core::Status startSourceImport(
        std::span<const Tina::EditorApp::Detail::EditorSourceImportUnit> intendedUnits,
        std::vector<std::string> selectedPathsUtf8);
    [[nodiscard]] static bool isFatalSourceImportError(
        const Tina::Core::Error& error) noexcept;
    [[nodiscard]] Tina::Core::Status importSelectedSourceFiles(
        std::vector<std::string> selectedPathsUtf8);
    [[nodiscard]] Tina::Core::Status importSourceFromDialog();
    [[nodiscard]] Tina::Core::Status removeSelectedSourceImport();
    [[nodiscard]] static EditorAssetImportStatus
    assetImportStatusForPhase(
        Tina::EditorApp::Detail::EditorSourceImportPhase phase) noexcept;
    [[nodiscard]] EditorAssetImportStatus
    projectAssetImportStatus(Tina::Core::AssetId assetId) const noexcept;
    [[nodiscard]] std::string_view
    projectAssetImportStatusLabel(Tina::Core::AssetId assetId) const noexcept;
    void setAssetImportHistory(
        std::span<const Tina::Asset::SourceImportPipelineUnitOutput> outputs,
        EditorAssetImportStatus status) noexcept;
    void setCurrentImportAssetHistory(EditorAssetImportStatus status) noexcept;
    void pruneAssetImportHistoryToCatalog() noexcept;
    void clearAssetImportHistory() noexcept;
    void cleanupOwnedSourceImportStage(std::string_view catalogRootUtf8) noexcept;
    void cleanupOwnedAuthoringStage(std::string_view catalogRootUtf8) noexcept;
    void cleanupFailedSourceImportStage() noexcept;
    [[nodiscard]] Tina::Core::Status publishCommittedSourceImportState(
        const Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready);
    [[nodiscard]] Tina::Core::Status commitSourceImportCatalog(
        Tina::EditorApp::Detail::EditorSourceImportReadyStage& ready);
    [[nodiscard]] Tina::Core::Status updateSourceImport();
    [[nodiscard]] Tina::Core::Status processPendingFileDrops(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshProjectCatalog();
    [[nodiscard]] Tina::Core::Status bakeAndPublishNavigation2D();
    [[nodiscard]] Tina::Core::Status refreshDocumentTabsUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status applyProjectAssetTypeFilter(
        Tina::PrimaryWindowUITreeUpdater& tree,
        Tina::Editor::ProjectAssetTypeFilter filter);
    [[nodiscard]] Tina::Core::Status applyProjectAssetViewMode(
        Tina::PrimaryWindowUITreeUpdater& tree, ProjectAssetViewMode mode);
    [[nodiscard]] Tina::Core::Status synchronizePendingProjectAssetSelection(
        Tina::PrimaryWindowUITreeUpdater& tree);
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
    // Node creation picker. showSceneAddModal captures the target parent and
    // document revision; createNodeFromSceneAddRequest re-validates both before
    // publishing so a stale request is rejected instead of writing the wrong
    // parent.
    [[nodiscard]] Tina::Core::Status showSceneAddModal(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status hideSceneAddModal(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshSceneAddModalUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status processPendingSceneAddTemplate(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::usize sceneAddTemplateCount() const noexcept;
    // Sprite resource picker. Only one Dialog may be open per window, so this
    // refuses to open while another modal owns the window.
    [[nodiscard]] Tina::Core::Status showSpriteAssetPicker(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status hideSpriteAssetPicker(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshSpriteAssetPickerUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status updateSpriteAssetPickerSearch(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status synchronizeSpriteAssetPickerSelection(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status confirmSpriteAssetPicker(
        Tina::PrimaryWindowUITreeUpdater& tree);
    void rebuildSpriteAssetPickerRows() noexcept;
    [[nodiscard]] UI::UIVirtualGridViewDataSource
    spriteAssetPickerDataSource() const noexcept;
    [[nodiscard]] static u64 spriteAssetPickerItemCount(
        const void* state) noexcept;
    [[nodiscard]] static bool spriteAssetPickerResolveItem(
        const void* state, u64 logicalIndex,
        UI::UIVirtualGridViewItemDescriptor& output) noexcept;
    [[nodiscard]] Tina::Core::Status createNodeFromSceneAddRequest(
        Tina::PrimaryWindowUITreeUpdater& tree,
        std::optional<u32>& hierarchyRefreshStableId,
        bool& requiresPreviewValidation);
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
    [[nodiscard]] Tina::Core::Status showSceneDeleteConfirmation(
        Tina::PrimaryWindowUITreeUpdater& tree,
        std::optional<u32> requestedStableId = std::nullopt);
    [[nodiscard]] Tina::Core::Status hideSceneDeleteConfirmation(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status showProjectAssetRemoveConfirmation(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status hideProjectAssetRemoveConfirmation(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status showProjectAssetRenameDialog(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status hideProjectAssetRenameDialog(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status confirmProjectAssetRename(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status showProjectAssetFolderDialog(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status hideProjectAssetFolderDialog(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status confirmProjectAssetFolder(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status captureRequestedRgbaFrame(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status processPendingPointLightColorUpdates(
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
    [[nodiscard]] Tina::Core::Status
    refreshInspectorGridLayout(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status
    refreshWorkspacePanelsUi(Tina::PrimaryWindowUITreeUpdater& tree);
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
    nodePropertySelectionStableIds() const;
    [[nodiscard]] Tina::Core::AssetId
    selectedProjectAssetIdOfKind(Tina::AssetFormat::AssetKind kind) const noexcept;
    [[nodiscard]] Tina::Core::AssetId
    selectedProjectSpriteAssetId() const noexcept;
    // A new node's required asset must exist in the active Catalog, otherwise the
    // node is authored against a dangling AssetId: the preview demotes it to a
    // plain Node2D and the viewport silently draws nothing. Resolution prefers the
    // current Project Assets selection and falls back to a built-in preview asset
    // only when that asset is really present, so a non-empty missingAssetKindName
    // means creation must be refused rather than bound to a missing asset.
    struct NodeTemplateAssetResolution final {
        Tina::Editor::World2DNodeTemplateAssets world2D{};
        Tina::Editor::World3DNodeTemplateAssets world3D{};
        std::string_view missingAssetKindName{};
    };
    [[nodiscard]] bool projectAssetExists(Tina::Core::AssetId assetId) const noexcept;
    [[nodiscard]] Tina::Core::AssetId
    resolvedNodeTemplateSpriteAssetId(u8 fallbackMarker) const noexcept;
    [[nodiscard]] Tina::Core::AssetId
    resolvedNodeTemplateAssetId(Tina::AssetFormat::AssetKind kind,
                                u8 fallbackMarker) const noexcept;
    [[nodiscard]] Tina::Core::Result<NodeTemplateAssetResolution>
    resolveNodeTemplateAssets(Tina::Core::usize slot, bool world2D) const noexcept;
    // Inspector text fields commit the way the viewport gizmo does: one finished
    // user intent publishes at most one revision. The gizmo's intent boundary is
    // pointer-up; a text field's is losing focus or pressing Enter, so no
    // separate Apply button is needed. Each field maps to the group command that
    // owns it, and a group command still batches all of its fields into a single
    // document `replace()`, preserving undo granularity.
    struct InspectorFieldCommitState final {
        UI::UINodeId field{};
        std::string baselineTextUtf8{};
        EditorCommand command = EditorCommand::ApplyTransform;
        // The selection the text was typed against. Clicking another node moves
        // focus and changes the selection in the same frame, so a commit without
        // this guard would write one node's text onto a different node.
        u32 stableId = 0;
        u64 viewportSelectionRevision = 0;
    };
    [[nodiscard]] std::optional<EditorCommand>
    inspectorFieldCommitCommand(UI::UINodeId field) const noexcept;
    // AssetKind a resource node template requires, or Invalid for node kinds that
    // own no resource payload.
    [[nodiscard]] static Tina::AssetFormat::AssetKind requiredResourceAssetKind(
        Tina::Editor::World2DNodeTemplate nodeTemplate) noexcept;
    // True while the user is editing this field. Canonical refreshes must not
    // overwrite it, or a gizmo drag/selection sync would silently discard typing.
    [[nodiscard]] bool inspectorFieldIsBeingEdited(UI::UINodeId field) const noexcept;
    [[nodiscard]] Tina::Core::Status processInspectorFieldCommit(
        Tina::PrimaryWindowUITreeUpdater& tree);
    // Runs one Inspector property edit against nodes of the matching kind.
    // Rejections (invalid selection, kind mismatch, invalid values) are
    // reported through authoringFeedback_ and return success; `published` is
    // true only when exactly one document revision was published.
    [[nodiscard]] Tina::Core::Status runNodePropertyCommand(
        Tina::PrimaryWindowUITreeUpdater& tree, EditorCommand command,
        bool& published);
    [[nodiscard]] Tina::Core::Status refreshNodePropertySectionsUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Asset::AssetHandle
    loadedAsset(Tina::Core::AssetId assetId, Tina::AssetFormat::AssetKind expectedKind) const noexcept;
    [[nodiscard]] static bool containsHandle(std::span<const Tina::Asset::AssetHandle> handles,
                                             Tina::Asset::AssetHandle handle) noexcept;
    [[nodiscard]] Tina::Core::Status preparePreviewAssetBindings();
    [[nodiscard]] bool previewAssetBindingsHaveActiveFrameBorrows() const noexcept;
    [[nodiscard]] Tina::Core::Status releasePreviewAssetBindings() noexcept;
    // Releases the preview registries, draining GPU retirements and retrying
    // once when the first attempt fails. Retirement is asynchronous, so a
    // single failed release does not mean the bindings are unrecoverable.
    // Returns the first failure only if the registries are still engaged, in
    // which case the next rebuild must not emplace over them.
    [[nodiscard]] Tina::Core::Status releasePreviewAssetBindingsDraining() noexcept;
    [[nodiscard]] Tina::Core::Status validateRuntimePreview();
    [[nodiscard]] Tina::Core::Status validateWorld3DRuntimePreview();
    [[nodiscard]] Tina::Core::Status publishRuntimePreviewStatus(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshAuthoringUi(Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] Tina::Core::Status refreshPlaySessionUi(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] const Tina::Editor::ProjectAssetDescriptor*
    inspectedProjectAsset() const noexcept;
    [[nodiscard]] const Tina::EditorApp::Detail::EditorSourceImportUnit*
    sourceImportUnitForAsset(Tina::Core::AssetId assetId) const noexcept;
    [[nodiscard]] const Tina::Asset::SourceImportPipelineUnitOutput*
    sourceImportMappingForAsset(Tina::Core::AssetId assetId) const noexcept;
    [[nodiscard]] bool projectAssetSupportsSourceRename(
        Tina::Core::AssetId assetId) const noexcept;
    [[nodiscard]] Tina::Core::Status rollbackProjectAssetSourceRename() noexcept;
    [[nodiscard]] Tina::Core::Result<InspectorMixedTransformFlags>
    inspectorMixedTransformFlags(u32 primaryStableId) const;
    [[nodiscard]] Tina::Core::Status publishInspector(Tina::PrimaryWindowUITreeUpdater& tree,
                                                      UI::UITreeViewItemKey key);
    [[nodiscard]] UI::UIListViewDataSource inspectorDependencyDataSource() const noexcept;
    [[nodiscard]] static u64 inspectorDependencyItemCount(const void* state) noexcept;
    static bool resolveInspectorDependencyItem(
        const void* state, u64 logicalIndex,
        UI::UIListViewItemDescriptor& output) noexcept;
    [[nodiscard]] UI::UIDataGridDataSource outputGridDataSource() const noexcept;
    [[nodiscard]] static u64 outputRowCount(const void* state) noexcept;
    [[nodiscard]] static u32 outputColumnCount(const void* state) noexcept;
    static bool resolveOutputRow(
        const void* state, u64 logicalRow,
        UI::UIDataGridRowDescriptor& output) noexcept;
    static bool resolveOutputColumn(
        const void* state, u32 logicalColumn,
        UI::UIDataGridColumnDescriptor& output) noexcept;
    static bool resolveOutputCell(
        const void* state, u64 logicalRow, u32 logicalColumn,
        UI::UIDataGridCellDescriptor& output) noexcept;
    [[nodiscard]] UI::UITreeViewDataSource layoutDebugTreeDataSource() noexcept;
    [[nodiscard]] static u64 layoutDebugTreeItemCount(const void* state) noexcept;
    static bool resolveLayoutDebugTreeItem(
        const void* state, u64 logicalIndex,
        UI::UITreeViewItemDescriptor& output) noexcept;
    static bool setLayoutDebugTreeItemExpanded(
        void* state, UI::UITreeViewItemKey key, bool expanded) noexcept;
    [[nodiscard]] bool rebuildLayoutDebugTreeProjection() noexcept;
    void handleLayoutDebugPickPointerDown(
        UI::UIRoutedPointerEvent& event) noexcept;
    void handleLayoutDebugWindowPointerDown(
        UI::UIRoutedPointerEvent& event) noexcept;
    void handleLayoutDebugWindowPointerMove(
        UI::UIRoutedPointerEvent& event) noexcept;
    void handleLayoutDebugWindowPointerUp(
        UI::UIRoutedPointerEvent& event) noexcept;
    void handleLayoutDebugWindowPointerCancel(
        UI::UIRoutedPointerEvent& event) noexcept;
    [[nodiscard]] const EditorOutputHistoryEntry*
    selectedOutputEntry() const noexcept;
    [[nodiscard]] bool outputTargetAvailable(
        const EditorOutputHistoryEntry& entry) const noexcept;
    void handleOutputPointerDown(UI::UIRoutedPointerEvent& event) noexcept;
    [[nodiscard]] Tina::Core::Status processPendingOutputLocate(
        Tina::PrimaryWindowUITreeUpdater& tree);
    [[nodiscard]] UI::UIDataGridDataSource sourceImportGridDataSource() const noexcept;
    [[nodiscard]] static u64 sourceImportRowCount(const void* state) noexcept;
    [[nodiscard]] static u32 sourceImportColumnCount(const void* state) noexcept;
    static bool resolveSourceImportRow(
        const void* state, u64 logicalRow,
        UI::UIDataGridRowDescriptor& output) noexcept;
    static bool resolveSourceImportColumn(
        const void* state, u32 logicalColumn,
        UI::UIDataGridColumnDescriptor& output) noexcept;
    static bool resolveSourceImportCell(
        const void* state, u64 logicalRow, u32 logicalColumn,
        UI::UIDataGridCellDescriptor& output) noexcept;
    [[nodiscard]] UI::UIVirtualGridViewDataSource projectAssetDataSource() const noexcept;
    [[nodiscard]] static u64 projectAssetItemCount(const void* state) noexcept;
    static bool resolveProjectAssetItem(const void* state, u64 logicalIndex,
                                        UI::UIVirtualGridViewItemDescriptor& output) noexcept;
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
        std::string_view kindName, u32 stableId);
    [[nodiscard]] std::string hierarchyLabelForStableId(
        std::string_view kindName, u32 stableId,
        std::string_view authoredName) const;
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
    std::vector<EditorAssetMetadataRecord> assetMetadata_{};
    Tina::Editor::EditorDocumentTabs documentTabs_;
    std::optional<Tina::Editor::EditorProjectWorkspace> pendingProjectSwitch_{};
    bool projectSwitchBlockedByDirty_ = false;
    std::optional<Tina::Editor::EditorProjectWorkspace> activeProjectWorkspace_{};
    EditorSettings editorSettings_{};
    std::optional<u32> pendingRecentProjectIndex_{};
    std::string temporaryProjectRootUtf8_{};
    std::string temporaryProjectSaveTargetRootUtf8_{};
    std::string pendingTemporaryProjectCleanupRootUtf8_{};
    EditorAssetResources& assetResources_;
    EditorRenderDeviceAccess& renderDeviceAccess_;
    Tina::EditorApp::Detail::EditorSourceImportService sourceImportService_;
    std::optional<Tina::PlatformEventSubscription> platformEventSubscription_{};
    std::vector<PendingFileDrop> pendingFileDrops_{};
    bool fileDropQueueOverflowed_ = false;
    FileDropFeedbackState fileDropFeedbackState_ =
        FileDropFeedbackState::Hidden;
    std::string fileDropFeedbackDetail_{};
    Tina::Core::SteadyMonotonicClock fileDropFeedbackClock_{};
    Tina::Core::MonotonicTimePoint fileDropFeedbackStarted_{};
    bool hierarchyRefreshPending_ = false;
    std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit> sourceImportUnits_{};
    std::vector<Tina::Asset::SourceImportPipelineUnitOutput>
        sourceImportUnitOutputs_{};
    std::vector<EditorAssetImportHistoryEntry> assetImportHistory_{};
    std::vector<Tina::Core::AssetId> activeAssetImportIds_{};
    std::vector<Tina::EditorApp::Detail::EditorSourceImportUnit>
        sourceImportRetryUnits_{};
    std::vector<std::string> sourceImportRetryPathsUtf8_{};
    std::vector<std::string> pendingSourceImportPathsUtf8_{};
    std::string sourceImportPointerPathUtf8_{};
    std::string sourceImportPendingStageRootUtf8_{};
    std::string sourceImportSupersededCatalogRootUtf8_{};
    std::string sourceImportSupersededAuthoringCatalogRootUtf8_{};
    bool sourceImportStartPending_ = false;
    bool retrySourceImportPending_ = false;
    bool cancelSourceImportPending_ = false;
    bool sourceImportCatalogCommitted_ = false;
    std::optional<Tina::EditorApp::Detail::EditorSourceFileRename>
        pendingProjectAssetSourceRename_{};
    Tina::EditorApp::Detail::EditorSourceImportPhase sourceImportObservedPhase_ =
        Tina::EditorApp::Detail::EditorSourceImportPhase::Idle;
    Tina::EditorApp::Detail::EditorFileDialog fileDialog_{};
    EditorIconResources iconResources_{};
    EditorCompositeImageResolver imageResolver_{};
    Tina::PrimaryWindowUIImageResolverRegistration imageResolverRegistration_{};
    Tina::UI::UIRootOwner uiRoot_{};
    UI::UINodeId hierarchyTree_{};
    UI::UINodeId hierarchyCount_{};
    UI::UINodeId hierarchySearchInput_{};
    UI::UINodeId hierarchyRenameRoot_{};
    UI::UINodeId hierarchyRenameInput_{};
    UI::UILayoutStyle hierarchyRenameRootLayout_{};
    UI::UINodeId hierarchyDropIndicator_{};
    UI::UINodeId hierarchyPreselectionNode_{};
    UI::UILayoutStyle hierarchyDropIndicatorLayout_{};
    UI::UINodeId projectAssetList_{};
    UI::UINodeId tilePaletteGrid_{};
    UI::UILayoutStyle tilePaletteGridLayout_{};
    UI::UINodeId projectAssetCount_{};
    UI::UINodeId projectAssetBreadcrumb_{};
    UI::UINodeId projectAssetSummary_{};
    UI::UINodeId projectAssetSource_{};
    UI::UINodeId projectAssetSearchInput_{};
    UI::UINodeId projectAssetTypeDropdown_{};
    UI::UINodeId projectAssetTypeDropdownPopup_{};
    UI::UINodeId projectAssetEmptyState_{};
    UI::UILayoutStyle projectAssetEmptyStateLayout_{};
    UI::UINodeId projectAssetEmptyStateTitle_{};
    UI::UINodeId projectAssetEmptyStateText_{};
    UI::UINodeId projectAssetEmptyStateImportButton_{};
    UI::UINodeId projectAssetStartCenter_{};
    UI::UILayoutStyle projectAssetStartCenterLayout_{};
    UI::UINodeId projectAssetStartCenterTitle_{};
    UI::UINodeId projectAssetStartCenterSubtitle_{};
    UI::UINodeId projectAssetStartCenterActions_{};
    UI::UINodeId projectAssetStartCenterNewButton_{};
    UI::UINodeId projectAssetStartCenterOpenButton_{};
    UI::UINodeId projectAssetStartCenterImportButton_{};
    UI::UINodeId projectAssetStartCenterRecent_{};
    std::array<UI::UINodeId, RecentProjectCapacity> recentProjectButtons_{};
    UI::UINodeId fileDropFeedbackRoot_{};
    UI::UILayoutStyle fileDropFeedbackRootLayout_{};
    UI::UINodeId fileDropFeedbackSurface_{};
    UI::UILayoutStyle fileDropFeedbackSurfaceLayout_{};
    UI::UINodeId fileDropFeedbackStateText_{};
    UI::UINodeId fileDropFeedbackMessage_{};
    std::array<UI::UIStraightSrgba8Color, 6> fileDropFeedbackToneColors_{};
    UI::UILayoutStyle fileDropFeedbackProgressLayout_{};
    UI::UINodeId fileDropFeedbackProgress_{};
    UI::UINodeId projectAssetActivity_{};
    UI::UILayoutStyle projectAssetActivityLayout_{};
    UI::UINodeId projectAssetActivityText_{};
    UI::UINodeId projectAssetActivityProgress_{};
    UI::UINodeId cancelSourceImportButton_{};
    UI::UINodeId projectAssetImportCallout_{};
    UI::UILayoutStyle projectAssetImportCalloutLayout_{};
    UI::UINodeId projectAssetImportFailureText_{};
    UI::UINodeId retrySourceImportButton_{};
    UI::UINodeId openImportOutputButton_{};
    UI::UINodeId sourceImportSection_{};
    UI::UILayoutStyle sourceImportSectionLayout_{};
    UI::UINodeId sourceImportDetails_{};
    UI::UILayoutStyle sourceImportDetailsLayout_{};
    UI::UINodeId sourceImportExpandButtonRoot_{};
    UI::UILayoutStyle sourceImportExpandButtonRootLayout_{};
    UI::UINodeId sourceImportExpandButton_{};
    UI::UINodeId sourceImportCollapseButtonRoot_{};
    UI::UILayoutStyle sourceImportCollapseButtonRootLayout_{};
    UI::UINodeId sourceImportCollapseButton_{};
    UI::UINodeId sourceImportGrid_{};
    UI::UINodeId sourceImportCount_{};
    UI::UINodeId inspectorScroll_{};
    UI::UINodeId inspectorContent_{};
    UI::UILayoutStyle inspectorContentLayout_{};
    UI::UINodeId inspectorEmptyState_{};
    UI::UILayoutStyle inspectorEmptyStateLayout_{};
    UI::UINodeId inspectorEmptyStateTitle_{};
    UI::UINodeId inspectorEmptyStateText_{};
    UI::UINodeId inspectorMode_{};
    UI::UINodeId inspectorDirtyBadge_{};
    UI::UILayoutStyle inspectorDirtyBadgeLayout_{};
    UI::UINodeId inspectorName_{};
    UI::UINodeId inspectorKind_{};
    UI::UINodeId inspectorNote_{};
    UI::UINodeId inspectorAssetRow_{};
    UI::UILayoutStyle inspectorAssetRowLayout_{};
    UI::UINodeId inspectorAssetPath_{};
    UI::UINodeId inspectorDependencySummary_{};
    UI::UINodeId inspectorDependencyList_{};
    UI::UINodeId viewportPreviewLayer_{};
    UI::UINodeId viewportStatusOverlay_{};
    UI::UILayoutStyle viewportStatusOverlayLayout_{};
    UI::UINodeId viewportStatusText_{};
    std::array<UI::UINodeId, ViewportGridVisualNodeCapacity>
        viewportGridNodes_{};
    std::array<UI::UINodeId, ViewportGizmoVisualNodeCapacity>
        viewportGizmoVisualNodes_{};
    std::array<UI::UINodeId, ViewportPreselectionVisualNodeCapacity>
        viewportPreselectionVisualNodes_{};
    std::array<UI::UINodeId, ViewportCollisionShapeVisualNodeCapacity>
        viewportCollisionShapeVisualNodes_{};
    UI::UINodeId viewportOrientationCompass_{};
    std::array<UI::UINodeId, ViewportOrientationOrbLayerCount>
        viewportOrientationOrbLayers_{};
    std::array<UI::UILayoutStyle, ViewportOrientationOrbLayerCount>
        viewportOrientationOrbLayerLayouts_{};
    std::array<UI::UINodeId, ViewportOrientationAxisCount>
        viewportOrientationAxisLines_{};
    std::array<UI::UINodeId, ViewportOrientationAxisCount>
        viewportOrientationAxisEndpoints_{};
    std::array<UI::UINodeId, ViewportOrientationAxisCount>
        viewportOrientationAxisLabels_{};
    UI::UINodeId viewportMarqueeNode_{};
    UI::UINodeId inspectorTransformHeader_{};
    UI::UILayoutStyle inspectorTransformHeaderLayout_{};
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
    struct InspectorLayoutNodeUi final {
        UI::UINodeId root{};
        UI::UILayoutStyle layout{};
    };
    UI::UINodeId inspectorTransformFields_{};
    UI::UILayoutStyle inspectorTransformFieldsLayout_{};
    UI::UINodeId inspectorTransformError_{};
    UI::UILayoutStyle inspectorTransformErrorLayout_{};
    std::string inspectorTransformErrorUtf8_{};
    u32 inspectorTransformErrorStableId_ = 0U;
    std::array<InspectorLayoutNodeUi, 3> inspectorTransformValueGrids_{};
    WorkspaceMode inspectorTransformGridWorkspace_ = WorkspaceMode::World2D;
    std::array<InspectorLayoutNodeUi, 9> inspectorTransformAxisFields_{};
    InspectorLayoutNodeUi inspectorHierarchyHeaderUi_{};
    InspectorLayoutNodeUi inspectorHierarchyParentRowUi_{};
    InspectorLayoutNodeUi inspectorHierarchyApplyParentUi_{};
    InspectorLayoutNodeUi inspectorTileMapHeaderUi_{};
    InspectorLayoutNodeUi inspectorTileMapStatusUi_{};
    std::array<InspectorLayoutNodeUi, 4> inspectorTileMapActionRows_{};
    UI::UINodeId tileMapStatus_{};
    UI::UINodeId leftDockSplitView_{};
    UI::UINodeId leftDock_{};
    UI::UINodeId leftDockSplitter_{};
    UI::UINodeId inspectorSplitView_{};
    UI::UINodeId inspectorDock_{};
    UI::UINodeId inspectorSplitter_{};
    UI::UINodeId bottomPanelSplitView_{};
    UI::UILayoutStyle leftDockLayout_{};
    UI::UILayoutStyle leftDockSplitterLayout_{};
    UI::UILayoutStyle inspectorDockLayout_{};
    UI::UILayoutStyle inspectorSplitterLayout_{};
    UI::UINodeId bottomPanelSplitter_{};
    UI::UINodeId bottomPanelHost_{};
    UI::UINodeId animationPanel_{};
    UI::UINodeId outputPanel_{};
    UI::UINodeId layoutDebugPanel_{};
    UI::UINodeId layoutDebugHeader_{};
    UI::UINodeId layoutDebugResizeHandle_{};
    UI::UINodeId outputGrid_{};
    std::array<UI::UINodeId, 4> outputFilterButtons_{};
    UI::UINodeId outputClearButton_{};
    UI::UINodeId outputDetailsToggleButton_{};
    UI::UINodeId outputLocateButton_{};
    UI::UINodeId outputSummary_{};
    UI::UINodeId outputDetails_{};
    UI::UILayoutStyle outputDetailsLayout_{};
    UI::UILogicalRect outputGridRect_{};
    UI::UIDataGridMetrics outputGridMetrics_{};
    UI::UIDataGridStyle outputGridStyle_{};
    UI::UIRoutedPointerListenerToken outputPointerListener_{};
    UI::UILayoutStyle bottomPanelSplitterLayout_{};
    UI::UILayoutStyle bottomPanelHostLayout_{};
    UI::UILayoutStyle animationPanelLayout_{};
    UI::UILayoutStyle outputPanelLayout_{};
    UI::UILayoutStyle layoutDebugPanelLayout_{};
    UI::UILayoutStyle layoutDebugResizeHandleLayout_{};
    UI::UINodeId leftDockCollapseButton_{};
    UI::UINodeId inspectorCollapseButton_{};
    UI::UINodeId animationCollapseButton_{};
    UI::UINodeId outputCollapseButton_{};
    UI::UINodeId layoutDebugCollapseButton_{};
    std::array<UI::UINodeId, 2> bottomPanelButtons_{};
    UI::UINodeId layoutDebugStatusButton_{};
    UI::UINodeId layoutDebugShowAllButton_{};
    UI::UINodeId layoutDebugPickButton_{};
    UI::UINodeId layoutDebugSummary_{};
    UI::UINodeId layoutDebugTree_{};
    std::array<UI::UINodeId, LayoutDebugDetailRowCount>
        layoutDebugDetailLabels_{};
    UI::UIRoutedPointerListenerToken layoutDebugPickPointerListener_{};
    std::array<UI::UIRoutedPointerListenerToken, 4>
        layoutDebugWindowPointerListeners_{};
    UI::UILogicalRect layoutDebugPanelRect_{};
    UI::UILogicalRect layoutDebugHeaderRect_{};
    UI::UILogicalRect layoutDebugResizeHandleRect_{};
    UI::UILogicalRect layoutDebugViewportRect_{};
    std::array<UI::UILogicalRect, 3> layoutDebugActionRects_{};
    UI::UILogicalPoint layoutDebugWindowPointerStart_{};
    UI::UILogicalRect layoutDebugWindowInitialRect_{};
    UI::UILogicalPoint layoutDebugTransientOffset_{};
    Tina::Platform::PointerId layoutDebugWindowPointer_{};
    bool layoutDebugWindowDragActive_ = false;
    bool layoutDebugWindowResizeActive_ = false;
    // Moving/resizing the debugger only changes the excluded overlay subtree.
    // Keep the expensive tree projection stable until the interaction ends.
    bool layoutDebugWindowMoveProjectionSkip_ = false;
    bool layoutDebugProfileMutationPendingCommit_ = false;
    bool layoutDebugProfileCommittedStatisticsPending_ = false;
    u64 layoutDebugProfileMutationIndex_ = 0;
    bool layoutDebugWindowStyleDirty_ = false;
    UI::UINodeId animationStatus_{};
    UI::UINodeId animationSelection_{};
    UI::UINodeId animationTimelineScale_{};
    UI::UINodeId animationHoverTime_{};
    UI::UILayoutStyle animationHoverTimeLayout_{};
    UI::UINodeId animationPlayhead_{};
    UI::UINodeId animationEventPosition_{};
    UI::UINodeId statusDocument_{};
    UI::UINodeId statusPreview_{};
    UI::UINodeId statusSelection_{};
    UI::UINodeId statusTask_{};
    UI::UINodeId statusCatalog_{};
    UI::UINodeId statusActivity_{};
    // The frame-rate readout is quantized and rate limited. A full-precision
    // per-frame string differs every frame, and setText would then dirty layout
    // and paint on every idle frame just to publish it.
    double statusFrameMetricElapsedSeconds_ = StatusFrameMetricIntervalSeconds;
    double statusFrameMetricFps_ = 0.0;
    double statusFrameMetricMilliseconds_ = 0.0;
    bool statusFrameMetricValid_ = false;
    UI::UIDialogParts sceneAddDialog_{};
    UI::UINodeId sceneAddParentLabel_{};
    // Sprite tint and flip live outside the numeric field table: colour needs the
    // hex text edit + swatch, and flips are booleans. Both were already supported
    // by EditorNodePropertyOperations before the Inspector surfaced them.
    UI::UIColorFieldParts spriteColorField_{};
    UI::UIStraightSrgba8Color spriteColorValue_{UI::rgba8(255, 255, 255)};
    bool spriteColorMixed_ = false;
    UI::UINodeId spriteFlipXSwitch_{};
    UI::UINodeId spriteFlipYSwitch_{};
    UI::UINodeId sceneAddSearchInput_{};
    UI::UINodeId sceneAddDescription_{};
    std::array<UI::UINodeId, SceneAddTemplateSlotCount> sceneAddTemplateButtons_{};
    UI::UINodeId sceneAddCreateButton_{};
    UI::UINodeId sceneAddCancelButton_{};
    // Clicking the Inspector resource slot opens this picker. Drag-and-drop from
    // Project Assets stays supported; the picker exists so the binding is
    // reachable without discovering the drag gesture. It reuses the same
    // ProjectAssetBrowserModel rows filtered to Sprite/Texture2D.
    UI::UIDialogParts spriteAssetPickerDialog_{};
    UI::UINodeId spriteAssetPickerSearchInput_{};
    UI::UINodeId spriteAssetPickerList_{};
    UI::UINodeId spriteAssetPickerStatus_{};
    UI::UINodeId spriteAssetPickerConfirmButton_{};
    UI::UINodeId spriteAssetPickerCancelButton_{};
    UI::UIVirtualGridViewStyle spriteAssetPickerListStyle_{};
    std::string spriteAssetPickerFilterUtf8_{};
    std::string spriteAssetPickerStatusUtf8_{};
    // Visible rows for the picker, rebuilt whenever it opens or its filter
    // changes. Indices address projectAssets_.items(), which owns the strings the
    // data source hands to the UI.
    std::vector<Tina::Core::usize> spriteAssetPickerRows_{};
    std::optional<u64> spriteAssetPickerObservedSelection_{};
    Tina::Core::AssetId spriteAssetPickerSelectedAssetId_{};
    u32 spriteAssetPickerTargetStableId_ = 0;
    // Invalid means the Sprite2D case, which uniquely accepts two kinds
    // (Sprite or a directly bound Texture2D). Any other value is a resource node
    // that accepts exactly its declared kind.
    Tina::AssetFormat::AssetKind spriteAssetPickerKind_ =
        Tina::AssetFormat::AssetKind::Invalid;
    bool spriteAssetPickerVisible_ = false;
    bool spriteAssetPickerFocusPending_ = false;
    // One animator per AnimatedSprite2D entity in the play world. Built when Play
    // starts and released on stop, so editing never pays for them.
    struct PlayAnimatorBinding final {
        Tina::Scene::EntityId entity{};
        std::optional<Tina::Scene::SpriteAnimator2D> animator{};
    };
    std::vector<PlayAnimatorBinding> playAnimators_{};
    UI::UIDialogParts dirtyCloseDialog_{};
    UI::UINodeId dirtyCloseTitle_{};
    UI::UINodeId dirtyCloseMessage_{};
    UI::UINodeId dirtyClosePathInput_{};
    UI::UIDialogParts sceneDeleteDialog_{};
    UI::UIDialogParts projectAssetRemoveDialog_{};
    UI::UIDialogParts projectAssetRenameDialog_{};
    UI::UINodeId projectAssetRenameInput_{};
    UI::UIDialogParts projectAssetFolderDialog_{};
    UI::UINodeId projectAssetFolderInput_{};
    UI::UIDialogParts aboutDialog_{};
    UI::UISnackbarHostParts snackbarParts_{};
    UI::UILayoutStyle snackbarRootLayout_{};
    UI::UILayoutStyle snackbarActionLayout_{};
    std::array<UI::UIStraightSrgba8Color, 4> snackbarToneColors_{};
    UI::UINodeId viewportMode_{};
    UI::UILayoutStyle viewportModeLayout_{};
    UI::UINodeId frameAllButton_{};
    struct NodePropertySectionUi final {
        UI::UICollapsibleSectionParts collapsible{};
        UI::UILayoutStyle rootLayout{};
        UI::UILayoutStyle indicatorLayout{};
        UI::UILayoutStyle contentLayout{};
        bool expanded = true;
        bool collapseUpdatePending = false;
        UI::UINodeId activeSwitch{};
        // Resource slot used by Sprite2D and AnimatedSprite2D rendering.
        // It is a real drop target, not a second selection/state model.
        UI::UINodeId resourceSlot{};
        UI::UINodeId resourceLabel{};
        UI::UINodeId resourceAssignButton{};
        // Collision Shape is the widest group at 10 values (kind, half extent XY,
        // radius, center XY, angle, density, friction, restitution) plus four
        // event-related toggles.
        std::array<UI::UINodeId, 12> fields{};
        Tina::Core::usize fieldCount = 0;
        std::array<UI::UINodeId, 4> toggles{};
        Tina::Core::usize toggleCount = 0;
    };
    // Rendering, Camera, Light, Occlusion, Animation, Physics body, Physics
    // shape, Resource, and 3D Rendering.
    static constexpr Tina::Core::usize MeshPropertiesSectionIndex = 8;
    std::array<NodePropertySectionUi, 9> nodePropertySections_{};
    // Tracks the focused Inspector field and the text it held when focus arrived.
    // A commit is published when focus leaves it or Enter is pressed, so the
    // Inspector needs no per-section Apply button.
    InspectorFieldCommitState inspectorFieldEdit_{};
    bool pendingInspectorFieldCommit_ = false;
    struct InspectorPointLightColorChannelRequest final {
        UI::UIColorPickerChannel channel = UI::UIColorPickerChannel::Red;
        float value = 0.0F;
    };
    UI::UIColorFieldParts pointLightColorField_{};    UI::UIColorPickerParts pointLightColorPicker_{};
    UI::UILayoutStyle pointLightColorPickerLayout_{};
    UI::UIStraightSrgba8Color pointLightColorValue_{UI::rgba8(255, 255, 255)};
    std::optional<InspectorPointLightColorChannelRequest>
        pendingPointLightColorChannel_{};
    bool pointLightColorPickerVisible_ = false;
    bool pendingPointLightColorPickerToggle_ = false;
    bool pointLightColorMixed_ = false;
    UI::UINodeId playButton_{};
    UI::UINodeId pauseButton_{};
    UI::UINodeId stepButton_{};
    UI::UINodeId stopButton_{};
    std::array<UI::UINodeId, MainMenuCount> mainMenuAnchors_{};
    std::array<UI::UINodeId, MainMenuCount> mainMenus_{};
    UI::UINodeId hierarchyContextMenu_{};
    UI::UINodeId hierarchyContextRenameItem_{};
    UI::UINodeId hierarchyContextMoveUpItem_{};
    UI::UINodeId hierarchyContextMoveDownItem_{};
    UI::UINodeId hierarchyContextMoveToRootItem_{};
    UI::UINodeId hierarchyContextDeleteItem_{};
    UI::UINodeId projectAssetContextMenu_{};
    UI::UINodeId projectAssetContextOpenItem_{};
    UI::UINodeId projectAssetContextInspectItem_{};
    UI::UINodeId projectAssetContextRenameItem_{};
    UI::UINodeId projectAssetContextNewFolderItem_{};
    UI::UINodeId projectAssetContextReimportItem_{};
    UI::UINodeId projectAssetContextLocateSourceItem_{};
    UI::UINodeId projectAssetContextCopyAssetIdItem_{};
    UI::UINodeId projectAssetContextCopySourcePathItem_{};
    UI::UINodeId projectAssetContextRevealDependenciesItem_{};
    UI::UINodeId projectAssetContextRemoveItem_{};
    std::array<UI::UINodeId, 2> workspaceModeButtons_{};
    std::array<UI::UINodeId, 2> viewportContextButtons_{};
    std::array<UI::UILayoutStyle, 2> viewportContextButtonLayouts_{};
    UI::UINodeId fileCreateProjectMenuItem_{};
    UI::UINodeId fileOpenProjectMenuItem_{};
    UI::UINodeId fileOpenRecentSubmenu_{};
    std::array<UI::UINodeId, RecentProjectCapacity> recentProjectMenuItems_{};
    UI::UINodeId fileImportSourceMenuItem_{};
    UI::UINodeId fileSaveMenuItem_{};
    UI::UINodeId fileSaveAsMenuItem_{};
    UI::UINodeId fileCloseDocumentMenuItem_{};
    UI::UINodeId editUndoMenuItem_{};
    UI::UINodeId editRedoMenuItem_{};
    UI::UINodeId editDuplicateMenuItem_{};
    UI::UINodeId editDeleteMenuItem_{};
    UI::UINodeId viewWorkspaceSubmenu_{};
    std::array<UI::UINodeId, 2> viewWorkspaceMenuItems_{};
    std::array<UI::UINodeId, 2> viewPanelMenuItems_{};
    UI::UINodeId viewLayoutDebuggerMenuItem_{};
    UI::UINodeId viewFrameAllMenuItem_{};
    UI::UINodeId viewFocusSelectionMenuItem_{};
    UI::UINodeId helpAboutMenuItem_{};
    std::array<UI::UINodeId, 1> selectToolButtons_{};
    std::array<UI::UINodeId, 1> translateToolButtons_{};
    std::array<UI::UINodeId, 1> rotateToolButtons_{};
    std::array<UI::UINodeId, 1> scaleToolButtons_{};
    UI::UINodeId orientationButton_{};
    UI::UINodeId snapButton_{};
    std::array<UI::UINodeId, 3> marqueeModeButtons_{};
    UI::UINodeId addEntityButton_{};
    UI::UINodeId duplicateEntityButton_{};
    UI::UINodeId deleteEntityButton_{};
    UI::UINodeId reparentEntityButton_{};
    UI::UINodeId focusEntityButton_{};
    UI::UINodeId tilePaintToolButton_{};
    UI::UINodeId tileEraseToolButton_{};
    UI::UINodeId resetTransformButton_{};
    UI::UINodeId undoButton_{};
    UI::UINodeId redoButton_{};
    UI::UINodeId saveButton_{};
    UI::UINodeId saveAsButton_{};
    UI::UINodeId openProjectAssetButton_{};
    UI::UINodeId refreshProjectCatalogButton_{};
    UI::UINodeId importSourceButton_{};
    UI::UINodeId removeSourceImportButton_{};
    UI::UINodeId closeDocumentButtonRoot_{};
    UI::UINodeId closeDocumentButton_{};
    UI::UILayoutStyle closeDocumentButtonLayout_{};
    UI::UINodeId documentTabsBar_{};
    UI::UILayoutStyle documentTabsBarLayout_{};
    std::array<UI::UINodeId, ProjectAssetTypeFilterCount>
        projectAssetTypeDropdownItems_{};
    std::array<UI::UINodeId, 2> projectAssetViewButtons_{};
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
    struct AnimationPlaybackButtonsUi final {
        UI::UIIconButtonParts play{};
        UI::UIIconButtonParts pause{};
        UI::UILayoutStyle layout{};
    } animationPlaybackButtons_{};
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
    UI::UINodeId animationEventMarkerLabel_{};
    std::array<UI::UINodeId, AnimationVisibleFrameSlots> animationFrameButtons_{};
    std::array<UI::UINodeId, AnimationVisibleFrameSlots> animationEventMarkerButtons_{};
    std::optional<u32> animationHoveredFrameSlot_{};
    UI::UIRoutedPointerListenerToken animationTimelinePointerListener_{};
    UI::UITreeViewItemKey selectionKey_ = UI::InvalidUITreeViewItemKey;
    std::vector<EditorHierarchyRow> hierarchyRows_{};
    std::vector<u32> collapsedHierarchyIds_{};
    std::string hierarchyFilterUtf8_{};
    ProjectAssetViewMode projectAssetViewMode_ = ProjectAssetViewMode::Grid;
    std::optional<ProjectAssetViewMode> pendingProjectAssetViewMode_{};
    std::string sceneAddFilterUtf8_{};
    // Owns the Create Node description text whenever it is composed rather than
    // borrowed from the template registry; setText copies, but the buffer must
    // still outlive the call.
    std::string sceneAddDescriptionUtf8_{};
    UI::UILogicalRect hierarchyTreeRect_{};
    UI::UITreeViewMetrics hierarchyTreeMetrics_{};
    float hierarchyTreeRowHeight_ = 28.0F;
    UI::UILogicalRect projectAssetListRect_{};
    UI::UIVirtualGridViewMetrics projectAssetListMetrics_{};
    UI::UILayoutStyle projectAssetListLayout_{};
    UI::UIVirtualGridViewStyle projectAssetListStyle_{};
    u64 lastHierarchyPointerDownFrame_ = 0;
    u32 lastHierarchyPointerDownStableId_ = 0;
    Tina::Platform::PointerId hierarchyDragPointer_ = Tina::Platform::PrimaryPointerId;
    u32 hierarchyDragStableId_ = 0;
    bool hierarchyDragActive_ = false;
    UI::UILogicalPoint hierarchyDragStartPosition_{};
    UI::UILogicalPoint hierarchyDragCurrentPosition_{};
    u32 hierarchyDropIndicatorStableId_ = 0U;
    HierarchyDropIntent hierarchyDropIndicatorIntent_ = HierarchyDropIntent::Reparent;
    // Last intent published from the committed hierarchy snapshot. Pointer-up
    // consumes this value instead of re-deriving insertion semantics from a
    // potentially different layout/position.
    std::optional<HierarchyDropRequest> hierarchyPublishedDrop_{};
    std::optional<u32> pendingHierarchyRenameStableId_{};
    u32 hierarchyContextStableId_ = 0;
    bool pendingHierarchyRenameCancel_ = false;
    bool pendingHierarchyRenameCommit_ = false;
    bool pendingHierarchyRenameFocus_ = false;
    u8 hierarchyRenameFocusDeferralFrames_ = 0;
    std::optional<HierarchyDropRequest> pendingHierarchyDrop_{};
    u32 hierarchyRenameStableId_ = 0;
    u64 hierarchyRenameDocumentRevision_ = 0;
    bool hierarchyRenameVisible_ = false;
    std::array<UI::UIRoutedPointerListenerToken, 4>
        hierarchyPointerListeners_{};
    Tina::Platform::PointerId projectAssetDragPointer_ =
        Tina::Platform::PrimaryPointerId;
    u64 lastProjectAssetPointerDownFrame_ = 0;
    Tina::Core::AssetId lastProjectAssetPointerDownAssetId_{};
    Tina::Core::AssetId projectAssetDragAssetId_{};
    Tina::Core::AssetId projectAssetContextAssetId_{};
    bool projectAssetDragActive_ = false;
    bool projectAssetPointerDownPreservedNodeInspector_ = false;
    UI::UILogicalPoint projectAssetDragStartPosition_{};
    std::optional<Tina::Core::AssetId> pendingProjectAssetOpen_{};
    std::optional<ProjectAssetDropRequest> pendingProjectAssetDrop_{};
    bool preserveNodeInspectorOnProjectAssetSelection_ = false;
    std::optional<ProjectAssetRemoveConfirmation>
        pendingProjectAssetRemoveConfirmation_{};
    bool pendingProjectAssetRemoveDialogFocus_ = false;
    std::optional<Tina::Core::AssetId> pendingProjectAssetRenameAssetId_{};
    bool pendingProjectAssetRenameDialogFocus_ = false;
    bool pendingProjectAssetFolderDialogFocus_ = false;
    std::array<UI::UIRoutedPointerListenerToken, 4>
        projectAssetPointerListeners_{};
    UI::UILogicalRect inspectorSpriteResourceRect_{};
    bool projectAssetDragOverSpriteResource_ = false;
    bool projectAssetDragOverViewport_ = false;
    ViewportToolMode viewportToolMode_ = ViewportToolMode::Select;
    u32 hierarchyPreselectionStableId_ = 0U;
    u32 viewportPreselectionStableId_ = 0U;
    Tina::Editor::EditorTransformGizmo viewportTransformGizmo_{};
    ViewportTransformTransaction viewportGizmo_{};
    Tina::Core::usize viewportGizmoVisibleNodeCount_ = 0;
    std::optional<ViewportOrientationCompassVisualState>
        viewportOrientationCompassVisualState_{};
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
    std::array<UI::UIRoutedPointerListenerToken, 4>
        viewportOrientationCompassPointerBarrierListeners_{};
    bool queuedFirstSelection_ = false;
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
    std::array<Tina::AssetFormat::TilesetTileView, Tina::AssetFormat::TilesetWire::MaxTiles>
        tilePaletteTiles_{};
    std::array<std::string, Tina::AssetFormat::TilesetWire::MaxTiles> tilePaletteLabels_{};
    u32 tilePaletteTileCount_ = 0;
    // The palette projection is rebuilt only when its inputs change. The
    // VirtualGridView invalidate path dirties Style unconditionally, which
    // would otherwise re-resolve every materialized row on every idle frame.
    Tina::Asset::AssetHandle observedTilePaletteTilesetAsset_{};
    Tina::Core::ContentHash observedTilePaletteTilesetHash_{};
    bool observedTilePaletteEditingContext_ = false;
    bool tilePaletteProjectionInitialized_ = false;
    std::optional<u64> observedTilePaletteSelection_{};
    std::optional<Tina::Editor::TileMapAuthoringCellEdit> lastPaintedTile_{};
    std::optional<Tina::Editor::TileMapAuthoringCellEdit> pendingTileCellEdit_{};
    Tina::AssetFormat::TileMapLayerId pendingTileLayerId_ = 0;
    std::vector<Tina::Scene::World2DEntityBinding> previewBindings_{};
    std::vector<World3DPreviewBinding> preview3DBindings_{};
    std::vector<World3DPreviewSkinnedPose> preview3DSkinnedPoses_{};
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
    // A source import can publish its Catalog before the runtime preview is
    // rebuilt. Retain this separately from pipeline failure state so a later
    // successful frame-boundary retry can restore the user-facing feedback.
    bool previewAssetBindingsFailureFeedbackPending_ = false;
    bool catalogRefreshPending_ = false;
    bool projectBrowserUiRefreshPending_ = false;
    bool projectAssetSelectionSyncPending_ = false;
    bool sourceImportUiRefreshPending_ = false;
    bool sourceImportVisibilityRefreshPending_ = false;
    bool sourceImportExpanded_ = false;
    bool sourceImportLastFailed_ = false;
    // Retains the bounded worker error for the persistent Project Assets
    // import callout. The previous Catalog remains the active data source.
    std::string sourceImportFailureMessageUtf8_{};
    bool sourceImportProfileSeen_ = false;
    bool sourceImportProfileActive_ = false;
    bool sourceImportProfileWorkerSampled_ = false;
    bool sourceImportProfileDeactivatePending_ = false;
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
    UI::UINodeId viewportAssetDropIndicator_{};
    std::optional<Tina::Render::RenderNormalizedViewport> viewportNormalized_{};
    u32 surfacePixelWidth_ = WindowLogicalWidth;
    u32 surfacePixelHeight_ = WindowLogicalHeight;
    std::string authoringFeedback_ = "One validated revision per command";
    std::array<EditorOutputHistoryEntry, EditorOutputHistoryCapacity>
        outputHistory_{};
    Tina::Core::usize outputHistoryCount_ = 0U;
    std::array<u32, EditorOutputHistoryCapacity> outputVisibleHistoryIndices_{};
    Tina::Core::usize outputVisibleHistoryCount_ = 0U;
    u64 outputNextSequence_ = 1U;
    std::optional<u64> outputSelectedSequence_{};
    std::optional<u64> pendingOutputLocateSequence_{};
    u64 lastOutputPointerDownSequence_ = 0U;
    u64 lastOutputPointerDownFrame_ = 0U;
    std::string outputHistoryObservedFeedback_{};
    bool outputGridRefreshPending_ = true;
    bool outputDetailsExpanded_ = false;
    std::optional<UI::UISnackbarHost> snackbarHost_{};
    Tina::Core::SteadyMonotonicClock snackbarClock_{};
    std::string lastSnackbarFeedback_{};
    u64 observedSnackbarRevision_ = 0;
    std::optional<u32> pendingSelectionStableId_{};
    std::optional<u64> observedProjectAssetSelectionIndex_{};
    std::optional<u64> observedSourceImportSelectionIndex_{};
    std::vector<std::string> inspectorDependencyLabels_{};
    std::optional<u32> pendingDocumentTabActivation_{};
    std::optional<u32> pendingMainMenuToggle_{};
    std::optional<WorkspacePanelKind> pendingWorkspacePanelToggle_{};
    std::optional<BottomPanelKind> pendingBottomPanelToggle_{};
    std::optional<BottomPanelKind> pendingBottomPanelOpen_{};
    bool pendingLayoutDebuggerToggle_ = false;
    bool pendingLayoutDebuggerOpen_ = false;
    struct LayoutDebugProjectionRow final {
        UI::UINodeId node{};
        UI::UITreeViewItemKey key = UI::InvalidUITreeViewItemKey;
        UI::UITreeViewItemKey parentKey = UI::InvalidUITreeViewItemKey;
        u32 depth = 0U;
        u32 preorder = 0U;
        bool expandable = false;
        bool expanded = false;
        std::array<char, 80> label{};
        u32 labelLength = 0U;

        bool operator==(const LayoutDebugProjectionRow&) const = default;
    };
    std::array<LayoutDebugProjectionRow, LayoutDebugProjectionCapacity>
        layoutDebugProjection_{};
    std::array<LayoutDebugProjectionRow, LayoutDebugProjectionCapacity>
        layoutDebugTreeRows_{};
    std::array<UI::UINodeId, LayoutDebugProjectionCapacity>
        layoutDebugPanelSubtreeNodes_{};
    std::optional<UI::UILayoutDebugEntry> layoutDebugSelectedEntry_{};
    Tina::Core::usize layoutDebugSnapshotEntryCount_ = 0U;
    Tina::Core::usize layoutDebugProjectionCount_ = 0U;
    Tina::Core::usize layoutDebugTreeRowCount_ = 0U;
    Tina::Core::usize layoutDebugPanelSubtreeNodeCount_ = 0U;
    u64 layoutDebugStructureRevision_ = 0U;
    u64 layoutDebugLayoutRevision_ = 0U;
    u32 layoutDebugPanelPreorderBegin_ = 0U;
    u32 layoutDebugPanelPreorderEnd_ = 0U;
    UI::UITreeViewItemKey layoutDebugSelectionKey_ =
        UI::InvalidUITreeViewItemKey;
    std::array<UI::UITreeViewItemKey, LayoutDebugProjectionCapacity>
        layoutDebugCollapsedKeys_{};
    std::array<bool, LayoutDebugProjectionCapacity>
        layoutDebugAncestorVisible_{};
    std::array<UI::UITreeViewItemKey, LayoutDebugProjectionCapacity>
        layoutDebugAncestorKeys_{};
    Tina::Core::usize layoutDebugCollapsedKeyCount_ = 0U;
    UI::UINodeId layoutDebugSelectedNode_{};
    bool layoutDebugShowAllVisibleBounds_ = true;
    bool layoutDebuggerVisible_ = false;
    bool layoutDebugPickArmed_ = false;
    bool layoutDebugSnapshotInitialized_ = false;
    bool layoutDebugPanelProjectionRangeValid_ = false;
    bool layoutDebugProjectionChangedPending_ = false;
    bool layoutDebugRevealSelectionPending_ = false;
    bool layoutDebugDetailsRefreshPending_ = true;
    std::optional<UI::UILogicalPoint> pendingLayoutDebugPickPoint_{};
    std::string layoutDebugPickFeedback_{};
    OutputFilter outputFilter_ = OutputFilter::All;
    std::optional<EditorCommand> pendingEditorCommand_{};
    std::optional<Tina::Editor::EditorDocumentKey> pendingDirtyCloseKey_{};
    bool pendingDirtyCloseDialogFocus_ = false;
    std::optional<SceneDeleteConfirmation> pendingSceneDeleteConfirmation_{};
    bool pendingSceneDeleteDialogFocus_ = false;
    std::optional<SceneAddRequest> pendingSceneAddRequest_{};
    bool pendingSceneAddDialogFocus_ = false;
    // Highlighted template row; applied from a button callback next frame so
    // selection follows the same deferred pattern as the main menu toggle.
    u32 sceneAddTemplateIndex_ = 0;
    std::optional<u32> pendingSceneAddTemplateIndex_{};
    bool pendingHierarchyFocusRestore_ = false;
    bool pendingAboutDialogFocus_ = false;
    bool pendingAboutDialogFocusRestore_ = false;
    std::optional<ViewportToolMode> pendingViewportToolMode_{};
    std::optional<Tina::Editor::EditorMarqueeSelectionMode>
        pendingMarqueeSelectionMode_{};
    std::optional<u32> pendingAutoParentStableId_{};
    bool pendingGizmoOrientationToggle_ = false;
    bool pendingGizmoSnapToggle_ = false;
    u64 observedPlaySessionRevision_ = 0;
    float leftDockVisibleFraction_ = LeftDockInitialFraction;
    float inspectorVisibleFraction_ = MainCenterInitialFraction;
    float bottomPanelVisibleFraction_ = ViewportInitialFraction;
    bool leftDockVisible_ = true;
    bool inspectorVisible_ = true;
    BottomPanelKind bottomPanel_ = BottomPanelKind::None;
};

} // namespace Tina::EditorApp::WorkspaceInternal
