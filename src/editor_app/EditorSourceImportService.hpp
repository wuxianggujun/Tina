#pragma once

#include "EditorSourceImportLimits.hpp"

#include <tina/asset/SourceImportPipeline.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace Tina::EditorApp::Detail {

enum class EditorSourceImportUnitKind : Core::u8 {
    CatalogRecipe = 0,
    Gltf = 1,
};

struct EditorSourceImportUnit final {
    EditorSourceImportUnitKind kind = EditorSourceImportUnitKind::CatalogRecipe;
    std::string sourcePathUtf8{};
    Asset::GltfCookIds gltfIds{};
};

// This request owns the complete intended unit set. The service copies no units from an
// earlier request and never infers or rewrites any of these paths.
struct EditorSourceImportRequest final {
    std::string sourceRootUtf8{};
    std::string baselineCatalogRootUtf8{};
    std::string baselineStatePathUtf8{};
    std::string freshStageRootUtf8{};
    std::string freshStageStatePathUtf8{};
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::Invalid;
    std::vector<EditorSourceImportUnit> units{};
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
};

// A successful worker result means the requested fresh stage and state file have completed
// current-schema package validation. The service publishes their request-owned paths only after
// the worker has stopped.
struct EditorSourceImportWorkResult final {
    EditorSourceImportStatistics statistics{};
    bool stageCreated = true;
};

struct EditorSourceImportReadyStage final {
    std::string stageRootUtf8{};
    std::string statePathUtf8{};
    EditorSourceImportStatistics statistics{};
    // False means the validated baseline was already current; no Catalog reload is required.
    bool stageCreated = true;
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

// Adapts the shared Asset source-import pipeline without copying its cooking/probe orchestration
// into EditorApp. Any memory resources referenced by stageConfig must outlive the worker/service.
[[nodiscard]] EditorSourceImportWorker makeEditorSourceImportPipelineWorker(
    Asset::CatalogPackageStageConfig stageConfig);

enum class EditorSourceImportServiceState : Core::u8 {
    Idle = 0,
    Running = 1,
    Ready = 2,
    Failed = 3,
};

// Owner-thread state machine. The worker receives one immutable owned request and must not touch
// AssetSystem, Render, or UI. Ready remains stable until acknowledgeReady(), so a
// CatalogReloadBusy result can be retried against the same stage on a later safe frame.
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
    [[nodiscard]] Core::Status acknowledgeReady();
    [[nodiscard]] Core::Status dismissFailure();

    [[nodiscard]] EditorSourceImportServiceState state() const noexcept;
    [[nodiscard]] const EditorSourceImportReadyStage* readyStage() const noexcept;
    [[nodiscard]] const Core::Error* failure() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tina::EditorApp::Detail
