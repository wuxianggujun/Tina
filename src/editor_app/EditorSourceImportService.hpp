#pragma once

#include "EditorSourceImportIngress.hpp"
#include "EditorSourceImportLimits.hpp"

#include <tina/asset/SourceImportPipeline.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace Tina::EditorApp::Detail {

enum class EditorSourceImportUnitKind : Core::u8 {
    CatalogRecipe = 0,
    Gltf = 1,
    Texture = 2,
    Audio = 3,
};

struct EditorSourceImportUnit final {
    EditorSourceImportUnitKind kind = EditorSourceImportUnitKind::CatalogRecipe;
    std::string sourcePathUtf8{};
    Asset::GltfCookIds gltfIds{};
};

// units is the currently intended set or an explicit replacement set. selectedPathsUtf8 is an
// optional user selection which the worker ingresses and merges before cooking. Both collections
// are owned so no UI or file-dialog storage crosses the worker boundary.
struct EditorSourceImportRequest final {
    std::string sourceRootUtf8{};
    std::string baselineCatalogRootUtf8{};
    std::string baselineStatePathUtf8{};
    std::string freshStageRootUtf8{};
    std::string freshStageStatePathUtf8{};
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::Invalid;
    std::vector<EditorSourceImportUnit> units{};
    std::vector<std::string> selectedPathsUtf8{};
};

enum class EditorSourceImportMode : Core::u8 {
    CleanReuse = 0,
    FullRecook = 1,
    IncrementalRecook = 2,
};

struct EditorSourceImportStatistics final {
    EditorSourceImportMode mode = EditorSourceImportMode::FullRecook;
    Core::u32 unitsTotal = 0;
    Core::u32 unitsRecooked = 0;
    Core::u32 unitsRemoved = 0;
    Core::u32 objectsReused = 0;
    Core::u32 objectsCooked = 0;
    Core::u64 cookedPayloadBytes = 0;
    Core::u64 transientMemoryPeakBytes = 0;
    Core::u64 transientMemoryBytesAfterRelease = 0;
};

// A successful worker result means the requested fresh stage and state file have completed
// current-schema package validation. The service publishes their request-owned paths only after
// the worker has stopped.
struct EditorSourceImportWorkResult final {
    Asset::CatalogSnapshot catalog{};
    EditorSourceImportStatistics statistics{};
    bool stageCreated = true;
    std::vector<EditorSourceImportUnit> intendedUnits{};
    std::optional<EditorSourceImportIngress> ingress{};
    Core::u32 selectedPathCount = 0;
    Core::u32 addedUnitCount = 0;
    Core::u32 copiedFileCount = 0;
    Core::u32 reusedFileCount = 0;
    std::vector<Asset::SourceImportPipelineUnitOutput> unitOutputs{};
};

struct EditorSourceImportReadyStage final {
    Asset::CatalogSnapshot catalog{};
    std::string stageRootUtf8{};
    std::string statePathUtf8{};
    EditorSourceImportStatistics statistics{};
    // False means the validated baseline was already current; no Catalog reload is required.
    bool stageCreated = true;
    std::vector<EditorSourceImportUnit> intendedUnits{};
    std::optional<EditorSourceImportIngress> ingress{};
    Core::u32 selectedPathCount = 0;
    Core::u32 addedUnitCount = 0;
    Core::u32 copiedFileCount = 0;
    Core::u32 reusedFileCount = 0;
    std::vector<Asset::SourceImportPipelineUnitOutput> unitOutputs{};
};

struct EditorSourceImportServiceConfig final {
    Core::u32 maxUnits = EditorSourceImportUnitCapacity;
    Core::usize maxPathBytes = 4096;
    Core::usize maxAggregatePathBytes = 4U * 1024U * 1024U;
    Core::usize maxErrorMessageBytes = 1024;
    Core::u32 maxErrorContexts = 8;
    Core::usize maxErrorContextBytes = 512;
};

using EditorSourceImportWorker = std::function<Core::Result<EditorSourceImportWorkResult>(
    const EditorSourceImportRequest&, std::stop_token)>;

[[nodiscard]] Asset::CatalogPackageStageConfig
makeEditorSourceImportStageConfig(std::pmr::memory_resource& transientMemory) noexcept;

// Each request owns a worker-local validation pool. No request-sized block is
// retained by the service after the worker publishes its value-only result.
[[nodiscard]] EditorSourceImportWorker makeEditorSourceImportPipelineWorker();

enum class EditorSourceImportServiceState : Core::u8 {
    Idle = 0,
    Running = 1,
    Ready = 2,
    Failed = 3,
};

enum class EditorSourceImportPhase : Core::u8 {
    Idle = 0,
    Preparing = 1,
    Copying = 2,
    Cooking = 3,
    ReadyToCommit = 4,
    Failed = 5,
};

// Owner-thread state machine. The worker receives one immutable owned request and must not touch
// AssetSystem, Render, or UI. Ready retains the ingress rollback transaction until commitReady(),
// so a CatalogReloadBusy result can be retried against the same stage on a later safe frame.
class EditorSourceImportService final {
public:
    EditorSourceImportService(EditorSourceImportWorker worker,
                              EditorSourceImportServiceConfig config = {});
    ~EditorSourceImportService() noexcept;

    EditorSourceImportService(const EditorSourceImportService&) = delete;
    EditorSourceImportService& operator=(const EditorSourceImportService&) = delete;
    EditorSourceImportService(EditorSourceImportService&&) = delete;
    EditorSourceImportService& operator=(EditorSourceImportService&&) = delete;

    [[nodiscard]] Core::Status start(EditorSourceImportRequest request);
    [[nodiscard]] Core::Status poll();
    [[nodiscard]] Core::Status cancel();
    [[nodiscard]] Core::Result<std::vector<EditorSourceImportUnit>> commitReady();
    [[nodiscard]] Core::Status discardReady();
    [[nodiscard]] Core::Status dismissFailure();

    [[nodiscard]] EditorSourceImportServiceState state() const noexcept;
    [[nodiscard]] EditorSourceImportPhase phase() const noexcept;
    [[nodiscard]] EditorSourceImportReadyStage* readyStage() noexcept;
    [[nodiscard]] const EditorSourceImportReadyStage* readyStage() const noexcept;
    [[nodiscard]] const Core::Error* failure() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tina::EditorApp::Detail
