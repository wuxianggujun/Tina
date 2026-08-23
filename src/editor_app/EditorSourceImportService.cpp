#include "EditorSourceImportService.hpp"

#include <tina/core/memory/CountingMemoryResource.hpp>
#include <tina/core/memory/MemoryTracker.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <atomic>
#include <exception>
#include <memory_resource>
#include <new>
#include <optional>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace Tina::EditorApp::Detail {
namespace {

[[nodiscard]] Core::Status invalidState(std::string_view message)
{
    return Core::failure(Core::CoreErrorCode::InvalidArgument, message);
}

[[nodiscard]] Core::Status validateText(std::string_view text, Core::usize maxBytes,
                                        bool allowEmpty, std::string_view field)
{
    if ((!allowEmpty && text.empty()) || text.size() > maxBytes ||
        !Core::isStrictUtf8WithoutNul(text))
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument, field);
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateConfig(const EditorSourceImportServiceConfig& config)
{
    if (config.maxUnits == 0U || config.maxPathBytes == 0U ||
        config.maxAggregatePathBytes < config.maxPathBytes ||
        config.maxErrorMessageBytes == 0U || config.maxErrorContexts == 0U ||
        config.maxErrorContextBytes == 0U)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import service limits must be non-zero and coherent");
    }
    return Core::success();
}

[[nodiscard]] Core::Status validateRequest(const EditorSourceImportRequest& request,
                                           const EditorSourceImportServiceConfig& config)
{
    if (request.targetPlatform == AssetFormat::TargetPlatform::Invalid)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import target platform is invalid");
    }
    if (request.units.size() > config.maxUnits)
    {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "Editor source import intended unit capacity exceeded");
    }

    Core::usize aggregatePathBytes = 0;
    const auto acceptPath = [&](std::string_view path, bool allowEmpty,
                                std::string_view field) -> Core::Status {
        if (const auto status = validateText(path, config.maxPathBytes, allowEmpty, field);
            !status)
        {
            return status;
        }
        if (path.size() > config.maxAggregatePathBytes - aggregatePathBytes)
        {
            return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                                 "Editor source import aggregate path capacity exceeded");
        }
        aggregatePathBytes += path.size();
        return Core::success();
    };

    if (const auto status = acceptPath(request.sourceRootUtf8, false,
                                       "Editor source import source root is invalid");
        !status)
    {
        return status;
    }
    if (const auto status = acceptPath(request.baselineCatalogRootUtf8, true,
                                       "Editor source import baseline Catalog root is invalid");
        !status)
    {
        return status;
    }
    if (const auto status = acceptPath(request.baselineStatePathUtf8, true,
                                       "Editor source import baseline state path is invalid");
        !status)
    {
        return status;
    }
    if (const auto status = acceptPath(request.freshStageRootUtf8, false,
                                       "Editor source import fresh stage root is invalid");
        !status)
    {
        return status;
    }
    if (const auto status = acceptPath(request.freshStageStatePathUtf8, false,
                                       "Editor source import fresh stage state path is invalid");
        !status)
    {
        return status;
    }
    if (!request.baselineCatalogRootUtf8.empty() &&
        request.baselineCatalogRootUtf8 == request.freshStageRootUtf8)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import stage root must differ from the baseline Catalog root");
    }

    for (const auto& unit : request.units)
    {
        if (unit.kind != EditorSourceImportUnitKind::CatalogRecipe &&
            unit.kind != EditorSourceImportUnitKind::Gltf &&
            unit.kind != EditorSourceImportUnitKind::Texture &&
            unit.kind != EditorSourceImportUnitKind::Audio)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Editor source import unit kind is invalid");
        }
        if (const auto status = acceptPath(unit.sourcePathUtf8, false,
                                           "Editor source import unit path is invalid");
            !status)
        {
            return status;
        }
    }
    return Core::success();
}

[[nodiscard]] std::string boundedUtf8(std::string_view text, Core::usize maxBytes)
{
    if (!Core::isStrictUtf8WithoutNul(text))
    {
        return "invalid UTF-8 error text";
    }
    if (text.size() <= maxBytes)
    {
        return std::string(text);
    }

    Core::usize length = maxBytes;
    while (length > 0U &&
           (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U)
    {
        --length;
    }
    return std::string(text.substr(0, length));
}

[[nodiscard]] Core::Error boundedError(Core::Error source,
                                       const EditorSourceImportServiceConfig& config)
{
    Core::Error result{source.code, boundedUtf8(source.message, config.maxErrorMessageBytes),
                       source.origin};
    result.nativeCode = source.nativeCode;
    const auto contextCount = std::min<Core::usize>(source.context.size(), config.maxErrorContexts);
    result.context.reserve(contextCount);
    for (Core::usize index = 0; index < contextCount; ++index)
    {
        const auto& context = source.context[index];
        result.addContext(boundedUtf8(context.operation, config.maxErrorContextBytes),
                          boundedUtf8(context.detail, config.maxErrorContextBytes),
                          context.location);
    }
    return result;
}

struct EditorSourceImportCompletion final {
    std::optional<Core::Result<EditorSourceImportWorkResult>> result{};
    std::atomic<bool> finished = false;
};

[[nodiscard]] Core::Status validateWorkResult(const EditorSourceImportWorkResult& result,
                                              Core::u32 intendedUnitCount)
{
    const auto& statistics = result.statistics;
    if (statistics.mode != EditorSourceImportMode::CleanReuse &&
        statistics.mode != EditorSourceImportMode::FullRecook &&
        statistics.mode != EditorSourceImportMode::IncrementalRecook)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "Editor source import worker returned an invalid mode");
    }
    if (statistics.unitsTotal != intendedUnitCount ||
        statistics.unitsRecooked > statistics.unitsTotal)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "Editor source import worker returned inconsistent unit statistics");
    }
    if (statistics.mode == EditorSourceImportMode::CleanReuse &&
        (statistics.unitsRecooked != 0U || statistics.objectsCooked != 0U ||
         result.stageCreated))
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "Editor source import clean reuse reported cooked work");
    }
    if (statistics.mode != EditorSourceImportMode::CleanReuse && !result.stageCreated)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "Editor source import recook did not return a fresh stage");
    }
    return Core::success();
}

[[nodiscard]] Core::Result<EditorSourceImportMode>
editorImportMode(Asset::SourceImportPipelineMode mode)
{
    switch (mode)
    {
    case Asset::SourceImportPipelineMode::CleanReuse:
        return EditorSourceImportMode::CleanReuse;
    case Asset::SourceImportPipelineMode::FullRecook:
        return EditorSourceImportMode::FullRecook;
    case Asset::SourceImportPipelineMode::IncrementalRecook:
        return EditorSourceImportMode::IncrementalRecook;
    }
    return Core::failure(Core::CoreErrorCode::Internal,
                         "Asset source import pipeline returned an invalid mode");
}

} // namespace

Asset::CatalogPackageStageConfig
makeEditorSourceImportStageConfig(std::pmr::memory_resource& transientMemory) noexcept
{
    Asset::CatalogPackageStageConfig config{};
    config.validation.manifest.catalog.maxEntries = 4096;
    config.validation.manifest.catalog.maxDependencies = 16384;
    config.validation.manifest.catalog.maxDependenciesPerAsset = 4096;
    config.validation.manifest.catalog.memoryResource = &transientMemory;
    config.validation.validation.file.memoryResource = &transientMemory;
    config.validation.validation.verifyTypedPayload = true;
    return config;
}

EditorSourceImportWorker makeEditorSourceImportPipelineWorker()
{
    return [](const EditorSourceImportRequest& request,
              std::stop_token stopToken) -> Core::Result<EditorSourceImportWorkResult> {
        Core::MemoryTracker transientTracker{};
        Core::CountingMemoryResource transientUpstream{
            transientTracker, Core::MemoryTag::Cooker,
            *std::pmr::new_delete_resource()};
        std::pmr::unsynchronized_pool_resource transientMemory{&transientUpstream};
        const Asset::CatalogPackageStageConfig stageConfig =
            makeEditorSourceImportStageConfig(transientMemory);
        std::vector<Asset::SourceImportPipelineUnit> units;
        units.reserve(request.units.size());
        for (const auto& unit : request.units)
        {
            Asset::SourceImportPipelineUnitKind pipelineKind =
                Asset::SourceImportPipelineUnitKind::CatalogRecipe;
            switch (unit.kind)
            {
            case EditorSourceImportUnitKind::CatalogRecipe:
                pipelineKind = Asset::SourceImportPipelineUnitKind::CatalogRecipe;
                break;
            case EditorSourceImportUnitKind::Gltf:
                pipelineKind = Asset::SourceImportPipelineUnitKind::Gltf;
                break;
            case EditorSourceImportUnitKind::Texture:
                pipelineKind = Asset::SourceImportPipelineUnitKind::Texture;
                break;
            case EditorSourceImportUnitKind::Audio:
                pipelineKind = Asset::SourceImportPipelineUnitKind::Audio;
                break;
            }
            units.push_back(Asset::SourceImportPipelineUnit{
                .kind = pipelineKind,
                .sourceUtf8Path = unit.sourcePathUtf8,
                .gltfIds = unit.gltfIds,
            });
        }

        auto pipeline = Asset::executeSourceImportPipeline(
            Asset::SourceImportPipelineRequest{
                .sourceRootUtf8 = request.sourceRootUtf8,
                .targetPlatform = request.targetPlatform,
                .units = units,
                .baselineCatalogRootUtf8 = request.baselineCatalogRootUtf8,
                .baselineStateUtf8Path = request.baselineStatePathUtf8,
                .stageCatalogRootUtf8 = request.freshStageRootUtf8,
                .stageStateUtf8Path = request.freshStageStatePathUtf8,
                .stageConfig = stageConfig,
            },
            stopToken);
        if (!pipeline)
        {
            return Core::failure(std::move(pipeline.error()).withContext(
                "EditorSourceImportService", "executeSourceImportPipeline"));
        }

        const std::string_view expectedCatalogRoot = pipeline->stageCreated
                                                         ? request.freshStageRootUtf8
                                                         : request.baselineCatalogRootUtf8;
        const std::string_view expectedStatePath = pipeline->stageCreated
                                                       ? request.freshStageStatePathUtf8
                                                       : request.baselineStatePathUtf8;
        if (pipeline->catalogRootUtf8 != expectedCatalogRoot ||
            pipeline->stateUtf8Path != expectedStatePath ||
            (pipeline->stageCreated && !pipeline->importStateCommitted))
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "Asset source import pipeline returned unexpected stage ownership");
        }

        auto mode = editorImportMode(pipeline->mode);
        if (!mode)
        {
            return Core::failure(std::move(mode.error()));
        }
        EditorSourceImportWorkResult result{
            .statistics = EditorSourceImportStatistics{
                .mode = *mode,
                .unitsTotal = pipeline->unitsTotal,
                .unitsRecooked = pipeline->unitsRecooked,
                .unitsRemoved = pipeline->unitsRemoved,
                .objectsReused = pipeline->objectsReused,
                .objectsCooked = pipeline->objectsCooked,
                .cookedPayloadBytes = pipeline->cookedPayloadBytes,
            },
            .stageCreated = pipeline->stageCreated,
        };
        const Core::MemoryStatistics transientPeakStatistics =
            transientTracker.snapshot(Core::MemoryTag::Cooker);
        transientMemory.release();
        const Core::MemoryStatistics transientReleasedStatistics =
            transientTracker.snapshot(Core::MemoryTag::Cooker);
        result.statistics.transientMemoryPeakBytes =
            static_cast<Core::u64>(transientPeakStatistics.peakBytes);
        result.statistics.transientMemoryBytesAfterRelease =
            static_cast<Core::u64>(transientReleasedStatistics.currentBytes);
        return result;
    };
}

class EditorSourceImportService::Impl final {
public:
    Impl(EditorSourceImportWorker sourceWorker, EditorSourceImportServiceConfig sourceConfig)
        : ownerThread(std::this_thread::get_id()), worker(std::move(sourceWorker)), config(sourceConfig)
    {
    }

    ~Impl() noexcept
    {
        if (workerThread.joinable())
        {
            workerThread.request_stop();
        }
    }

    [[nodiscard]] Core::Status ensureOwnerThread() const
    {
        if (std::this_thread::get_id() != ownerThread)
        {
            return Core::failure(Core::CoreErrorCode::InvalidArgument,
                                 "Editor source import service called from a non-owner thread");
        }
        return Core::success();
    }

    std::thread::id ownerThread{};
    EditorSourceImportWorker worker{};
    EditorSourceImportServiceConfig config{};
    EditorSourceImportServiceState currentState = EditorSourceImportServiceState::Idle;
    std::jthread workerThread{};
    std::shared_ptr<EditorSourceImportCompletion> completion{};
    std::string pendingStageRootUtf8{};
    std::string pendingStatePathUtf8{};
    std::string pendingBaselineRootUtf8{};
    std::string pendingBaselineStatePathUtf8{};
    Core::u32 pendingUnitCount = 0;
    std::optional<EditorSourceImportReadyStage> ready{};
    std::optional<Core::Error> lastFailure{};
};

EditorSourceImportService::EditorSourceImportService(EditorSourceImportWorker worker,
                                                     EditorSourceImportServiceConfig config)
    : impl_(std::make_unique<Impl>(std::move(worker), config))
{
}

EditorSourceImportService::~EditorSourceImportService() noexcept = default;

Core::Status EditorSourceImportService::start(EditorSourceImportRequest request)
{
    if (const auto status = impl_->ensureOwnerThread(); !status)
    {
        return status;
    }
    if (impl_->currentState != EditorSourceImportServiceState::Idle)
    {
        return invalidState("Editor source import start requires Idle state");
    }
    if (!impl_->worker)
    {
        return Core::failure(Core::CoreErrorCode::InvalidArgument,
                             "Editor source import worker is not configured");
    }
    if (const auto status = validateConfig(impl_->config); !status)
    {
        return status;
    }
    if (const auto status = validateRequest(request, impl_->config); !status)
    {
        return status;
    }

    try
    {
        auto completion = std::make_shared<EditorSourceImportCompletion>();
        auto worker = impl_->worker;
        impl_->pendingStageRootUtf8 = request.freshStageRootUtf8;
        impl_->pendingStatePathUtf8 = request.freshStageStatePathUtf8;
        impl_->pendingBaselineRootUtf8 = request.baselineCatalogRootUtf8;
        impl_->pendingBaselineStatePathUtf8 = request.baselineStatePathUtf8;
        impl_->pendingUnitCount = static_cast<Core::u32>(request.units.size());
        const auto config = impl_->config;
        impl_->workerThread = std::jthread(
            [completion, worker = std::move(worker), request = std::move(request), config](
                std::stop_token stopToken) mutable {
                try
                {
                    auto result = worker(request, stopToken);
                    if (!result)
                    {
                        result = Core::failure(boundedError(std::move(result.error()), config));
                    }
                    completion->result.emplace(std::move(result));
                }
                catch (const std::bad_alloc&)
                {
                    completion->result.emplace(Core::failure(
                        Core::CoreErrorCode::OutOfMemory,
                        "Editor source import worker allocation failed"));
                }
                catch (const std::exception&)
                {
                    completion->result.emplace(Core::failure(
                        Core::CoreErrorCode::Internal,
                        "Editor source import worker threw an exception"));
                }
                catch (...)
                {
                    completion->result.emplace(Core::failure(
                        Core::CoreErrorCode::Internal,
                        "Editor source import worker failed with an unknown exception"));
                }
                completion->finished.store(true, std::memory_order_release);
            });
        impl_->completion = std::move(completion);
        impl_->currentState = EditorSourceImportServiceState::Running;
        return Core::success();
    }
    catch (const std::bad_alloc&)
    {
        impl_->pendingStageRootUtf8.clear();
        impl_->pendingStatePathUtf8.clear();
        impl_->pendingBaselineRootUtf8.clear();
        impl_->pendingBaselineStatePathUtf8.clear();
        impl_->pendingUnitCount = 0;
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor source import worker launch allocation failed");
    }
    catch (const std::system_error& error)
    {
        impl_->pendingStageRootUtf8.clear();
        impl_->pendingStatePathUtf8.clear();
        impl_->pendingBaselineRootUtf8.clear();
        impl_->pendingBaselineStatePathUtf8.clear();
        impl_->pendingUnitCount = 0;
        Core::Error result{Core::CoreErrorCode::Internal,
                           "Editor source import worker thread launch failed"};
        result.setNativeCode(static_cast<Core::i64>(error.code().value()));
        return Core::failure(std::move(result));
    }
}

Core::Status EditorSourceImportService::poll()
{
    if (const auto status = impl_->ensureOwnerThread(); !status)
    {
        return status;
    }
    if (impl_->currentState != EditorSourceImportServiceState::Running ||
        !impl_->completion->finished.load(std::memory_order_acquire))
    {
        return Core::success();
    }

    impl_->workerThread.join();
    auto result = std::move(*impl_->completion->result);
    impl_->completion.reset();
    if (!result)
    {
        impl_->lastFailure.emplace(std::move(result.error()));
        impl_->pendingStageRootUtf8.clear();
        impl_->pendingStatePathUtf8.clear();
        impl_->pendingBaselineRootUtf8.clear();
        impl_->pendingBaselineStatePathUtf8.clear();
        impl_->pendingUnitCount = 0;
        impl_->currentState = EditorSourceImportServiceState::Failed;
        return Core::success();
    }

    if (const auto status = validateWorkResult(*result, impl_->pendingUnitCount); !status)
    {
        impl_->lastFailure.emplace(status.error());
        impl_->pendingStageRootUtf8.clear();
        impl_->pendingStatePathUtf8.clear();
        impl_->pendingBaselineRootUtf8.clear();
        impl_->pendingBaselineStatePathUtf8.clear();
        impl_->pendingUnitCount = 0;
        impl_->currentState = EditorSourceImportServiceState::Failed;
        return Core::success();
    }

    impl_->ready.emplace(EditorSourceImportReadyStage{
        .stageRootUtf8 = result->stageCreated ? std::move(impl_->pendingStageRootUtf8)
                                              : std::move(impl_->pendingBaselineRootUtf8),
        .statePathUtf8 = result->stageCreated ? std::move(impl_->pendingStatePathUtf8)
                                              : std::move(impl_->pendingBaselineStatePathUtf8),
        .statistics = result->statistics,
        .stageCreated = result->stageCreated,
    });
    impl_->pendingStageRootUtf8.clear();
    impl_->pendingStatePathUtf8.clear();
    impl_->pendingBaselineRootUtf8.clear();
    impl_->pendingBaselineStatePathUtf8.clear();
    impl_->pendingUnitCount = 0;
    impl_->currentState = EditorSourceImportServiceState::Ready;
    return Core::success();
}

Core::Status EditorSourceImportService::cancel()
{
    if (const auto status = impl_->ensureOwnerThread(); !status)
    {
        return status;
    }
    if (impl_->currentState != EditorSourceImportServiceState::Running)
    {
        return invalidState("Editor source import cancel requires Running state");
    }

    impl_->workerThread.request_stop();
    impl_->workerThread.join();
    impl_->completion.reset();
    impl_->pendingStageRootUtf8.clear();
    impl_->pendingStatePathUtf8.clear();
    impl_->pendingBaselineRootUtf8.clear();
    impl_->pendingBaselineStatePathUtf8.clear();
    impl_->pendingUnitCount = 0;
    impl_->currentState = EditorSourceImportServiceState::Idle;
    return Core::success();
}

Core::Status EditorSourceImportService::acknowledgeReady()
{
    if (const auto status = impl_->ensureOwnerThread(); !status)
    {
        return status;
    }
    if (impl_->currentState != EditorSourceImportServiceState::Ready)
    {
        return invalidState("Editor source import acknowledge requires Ready state");
    }
    impl_->ready.reset();
    impl_->currentState = EditorSourceImportServiceState::Idle;
    return Core::success();
}

Core::Status EditorSourceImportService::dismissFailure()
{
    if (const auto status = impl_->ensureOwnerThread(); !status)
    {
        return status;
    }
    if (impl_->currentState != EditorSourceImportServiceState::Failed)
    {
        return invalidState("Editor source import failure dismissal requires Failed state");
    }
    impl_->lastFailure.reset();
    impl_->currentState = EditorSourceImportServiceState::Idle;
    return Core::success();
}

EditorSourceImportServiceState EditorSourceImportService::state() const noexcept
{
    return impl_->currentState;
}

const EditorSourceImportReadyStage* EditorSourceImportService::readyStage() const noexcept
{
    return impl_->ready ? &*impl_->ready : nullptr;
}

const Core::Error* EditorSourceImportService::failure() const noexcept
{
    return impl_->lastFailure ? &*impl_->lastFailure : nullptr;
}

} // namespace Tina::EditorApp::Detail
