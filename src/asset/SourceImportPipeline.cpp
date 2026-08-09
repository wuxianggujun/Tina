#include <tina/asset/SourceImportPipeline.hpp>

#include "Utf8Path.hpp"

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogPackageChangeDetector.hpp>
#include <tina/asset/MediaCook.hpp>
#include <tina/asset/SourceImportExecutor.hpp>
#include <tina/asset/SourceImportPlan.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

struct LoadedBaseline final {
    std::pmr::vector<std::byte> stateBytes{};
    AssetFormat::SourceImportMetadataView metadata{};
    CatalogSnapshot catalog{};
    AssetFormat::SourceImportManifestRevision revision{};
};

struct BaselineLoadResult final {
    std::optional<LoadedBaseline> baseline{};
    SourceImportProbeState probeState = SourceImportProbeState::NoBaseline;
    SourceImportProbeReason probeReason = SourceImportProbeReason::CatalogNotFound;
    bool catalogPresent = false;
};

[[nodiscard]] Core::Status checkStopped(const std::stop_token stopToken)
{
    if (stopToken.stop_requested())
    {
        return Core::failure(AssetErrorCode::SourceImportCancelled,
                             "source import pipeline was cancelled");
    }
    return Core::success();
}

[[nodiscard]] bool pathComponentEquals(const std::filesystem::path& left,
                                       const std::filesystem::path& right) noexcept
{
#if defined(_WIN32)
    const auto& leftText = left.native();
    const auto& rightText = right.native();
    return leftText.size() == rightText.size() &&
           std::equal(leftText.begin(), leftText.end(), rightText.begin(),
                      [](const wchar_t leftCharacter, const wchar_t rightCharacter) {
                          return std::towlower(leftCharacter) == std::towlower(rightCharacter);
                      });
#else
    return left == right;
#endif
}

[[nodiscard]] bool pathIsSameOrDescendant(const std::filesystem::path& candidate,
                                          const std::filesystem::path& ancestor) noexcept
{
    auto candidatePart = candidate.begin();
    for (auto ancestorPart = ancestor.begin(); ancestorPart != ancestor.end();
         ++ancestorPart, ++candidatePart)
    {
        if (candidatePart == candidate.end() || !pathComponentEquals(*candidatePart, *ancestorPart))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Core::Result<std::filesystem::path> resolvePipelinePath(std::string_view utf8Path)
{
    if (utf8Path.empty() || !Core::isStrictUtf8WithoutNul(utf8Path))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import pipeline path is invalid");
    }
    std::error_code errorCode;
    auto resolved = std::filesystem::weakly_canonical(Detail::pathFromUtf8Bytes(utf8Path), errorCode);
    if (errorCode)
    {
        return Core::failure(Core::Error{Core::CoreErrorCode::Io,
                                         "failed to resolve source import pipeline path"}
                                 .setNativeCode(errorCode.value()));
    }
    return resolved.lexically_normal();
}

[[nodiscard]] Core::Status validateRequest(const SourceImportPipelineRequest& request)
{
    if (request.sourceRootUtf8.empty() ||
        request.baselineCatalogRootUtf8.empty() || request.baselineStateUtf8Path.empty() ||
        !Core::isStrictUtf8WithoutNul(request.sourceRootUtf8))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import pipeline requires source, baseline, and state paths");
    }
    if (request.targetPlatform == AssetFormat::TargetPlatform::Invalid)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import pipeline target platform must be explicit");
    }
    if (request.units.size() > static_cast<Core::usize>((std::numeric_limits<Core::u32>::max)()))
    {
        return Core::failure(AssetErrorCode::SourceImportCaptureCapacityExceeded,
                             "source import pipeline unit count exceeds u32");
    }
    if (request.stageCatalogRootUtf8.empty() != request.stageStateUtf8Path.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import stage Catalog and state paths are required together");
    }
    for (const auto& unit : request.units)
    {
        if (unit.sourceUtf8Path.empty() || !Core::isStrictUtf8WithoutNul(unit.sourceUtf8Path))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import unit path is invalid");
        }
        if (unit.kind != SourceImportPipelineUnitKind::CatalogRecipe &&
            unit.kind != SourceImportPipelineUnitKind::Gltf &&
            unit.kind != SourceImportPipelineUnitKind::Texture &&
            unit.kind != SourceImportPipelineUnitKind::Audio)
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import unit kind is invalid");
        }
    }

    auto baselineRoot = resolvePipelinePath(request.baselineCatalogRootUtf8);
    auto baselineState = resolvePipelinePath(request.baselineStateUtf8Path);
    if (!baselineRoot || !baselineState)
    {
        return Core::failure(!baselineRoot ? std::move(baselineRoot.error())
                                           : std::move(baselineState.error()));
    }
    if (pathIsSameOrDescendant(*baselineState, *baselineRoot))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import state must remain outside the Catalog root");
    }
    if (!request.stageCatalogRootUtf8.empty())
    {
        auto stageRoot = resolvePipelinePath(request.stageCatalogRootUtf8);
        auto stageState = resolvePipelinePath(request.stageStateUtf8Path);
        if (!stageRoot || !stageState)
        {
            return Core::failure(!stageRoot ? std::move(stageRoot.error())
                                            : std::move(stageState.error()));
        }
        if (pathIsSameOrDescendant(*stageRoot, *baselineRoot) ||
            pathIsSameOrDescendant(*baselineRoot, *stageRoot) ||
            pathIsSameOrDescendant(*stageState, *baselineRoot) ||
            pathIsSameOrDescendant(*stageState, *stageRoot) ||
            (pathIsSameOrDescendant(*stageState, *baselineState) &&
             pathIsSameOrDescendant(*baselineState, *stageState)))
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "source import fresh stage paths overlap existing roots or state");
        }
        std::error_code errorCode;
        const bool stageStateExists = std::filesystem::exists(*stageState, errorCode);
        if (errorCode)
        {
            return Core::failure(Core::Error{Core::CoreErrorCode::Io,
                                             "failed to inspect source import stage state"}
                                     .setNativeCode(errorCode.value()));
        }
        if (stageStateExists)
        {
            return Core::failure(Core::CoreErrorCode::AlreadyExists,
                                 "source import stage state already exists");
        }
    }
    return Core::success();
}

[[nodiscard]] std::pmr::memory_resource*
pipelineMemory(const SourceImportPipelineRequest& request) noexcept
{
    return request.stageConfig.validation.manifest.catalog.memoryResource;
}

[[nodiscard]] CatalogPackageOpenConfig
fullValidationConfig(const SourceImportPipelineRequest& request) noexcept
{
    auto config = request.stageConfig.validation;
    config.manifestRelativePath = DefaultCatalogManifestRelativePath;
    config.validateOnOpen = true;
    config.validation.verifyContent = true;
    return config;
}

[[nodiscard]] CatalogPackageChangeDetectorConfig
revisionConfig(const SourceImportPipelineRequest& request) noexcept
{
    return CatalogPackageChangeDetectorConfig{
        .scratchMemoryResource = pipelineMemory(request),
        .manifestRelativePath = DefaultCatalogManifestRelativePath,
    };
}

[[nodiscard]] Core::Result<BaselineLoadResult>
loadBaseline(const SourceImportPipelineRequest& request)
{
    auto revision = captureCatalogPackageRevision(request.baselineCatalogRootUtf8,
                                                  revisionConfig(request));
    if (!revision)
    {
        if (revision.error().code == Core::CoreErrorCode::NotFound)
        {
            return BaselineLoadResult{};
        }
        return Core::failure(std::move(revision.error()));
    }
    auto catalog = openCatalogPackage(request.baselineCatalogRootUtf8,
                                      fullValidationConfig(request));
    if (!catalog)
    {
        return Core::failure(std::move(catalog.error()));
    }

    auto stateBytes = Core::readFile(
        request.baselineStateUtf8Path,
        Core::ReadFileConfig{.maxBytes = Core::MaxReadFileBytes,
                             .memoryResource = pipelineMemory(request)});
    if (!stateBytes)
    {
        if (stateBytes.error().code == Core::CoreErrorCode::NotFound)
        {
            return BaselineLoadResult{
                .probeReason = SourceImportProbeReason::StateNotFound,
                .catalogPresent = true,
            };
        }
        return Core::failure(std::move(stateBytes.error()));
    }
    auto metadata = AssetFormat::parseSourceImportMetadataView(*stateBytes);
    if (!metadata)
    {
        if (metadata.error().code == AssetFormat::AssetFormatErrorCode::UnsupportedSchema)
        {
            return BaselineLoadResult{
                .probeState = SourceImportProbeState::Dirty,
                .probeReason = SourceImportProbeReason::StateSchemaChanged,
                .catalogPresent = true,
            };
        }
        return Core::failure(std::move(metadata.error()));
    }
    const AssetFormat::SourceImportManifestRevision importRevision{
        .manifestDigest = revision->manifestDigest,
        .manifestBytes = revision->manifestBytes,
    };
    if (const auto status = validateSourceImportCatalogBinding(*metadata, importRevision); !status)
    {
        return BaselineLoadResult{
            .probeState = SourceImportProbeState::Dirty,
            .probeReason = SourceImportProbeReason::CatalogRevisionChanged,
            .catalogPresent = true,
        };
    }
    if (const auto status = validateSourceImportCatalogOutputs(*metadata, *catalog); !status)
    {
        return BaselineLoadResult{
            .probeState = SourceImportProbeState::Dirty,
            .probeReason = SourceImportProbeReason::CatalogOutputChanged,
            .catalogPresent = true,
        };
    }
    return BaselineLoadResult{
        .baseline = LoadedBaseline{
            .stateBytes = std::move(*stateBytes),
            .metadata = *metadata,
            .catalog = std::move(*catalog),
            .revision = importRevision,
        },
        .probeState = SourceImportProbeState::Dirty,
        .probeReason = SourceImportProbeReason::None,
        .catalogPresent = true,
    };
}

[[nodiscard]] Core::Result<SourceImportUnitProbeDesc>
makeProbeDesc(const SourceImportPipelineUnit& unit, std::string_view sourceRoot,
              AssetFormat::TargetPlatform targetPlatform)
{
    switch (unit.kind)
    {
    case SourceImportPipelineUnitKind::CatalogRecipe:
        return makeCatalogRecipeSourceImportProbeDesc(sourceRoot, unit.sourceUtf8Path,
                                                      targetPlatform);
    case SourceImportPipelineUnitKind::Gltf:
        return makeGltfSourceImportProbeDesc(sourceRoot, unit.sourceUtf8Path, unit.gltfIds);
    case SourceImportPipelineUnitKind::Texture:
        return makeTextureSourceImportProbeDesc(sourceRoot, unit.sourceUtf8Path);
    case SourceImportPipelineUnitKind::Audio:
        return makeAudioSourceImportProbeDesc(sourceRoot, unit.sourceUtf8Path);
    }
    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                         "source import unit kind is unsupported");
}

[[nodiscard]] Core::Result<CatalogCookSourceResult>
cookUnit(const SourceImportPipelineUnit& unit, std::string_view sourceRoot,
         AssetFormat::TargetPlatform targetPlatform)
{
    const SourceImportCaptureConfig capture{.sourceRootUtf8 = sourceRoot};
    switch (unit.kind)
    {
    case SourceImportPipelineUnitKind::CatalogRecipe:
        return loadCatalogCookRecipeSourceFile(unit.sourceUtf8Path, capture);
    case SourceImportPipelineUnitKind::Gltf:
        return cookGltfFileToCatalogSourceResult(unit.sourceUtf8Path, targetPlatform,
                                                 capture, unit.gltfIds);
    case SourceImportPipelineUnitKind::Texture:
        return cookTextureFileToCatalogSourceResult(unit.sourceUtf8Path, targetPlatform,
                                                    capture);
    case SourceImportPipelineUnitKind::Audio:
        return cookAudioFileToCatalogSourceResult(unit.sourceUtf8Path, targetPlatform,
                                                  capture);
    }
    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                         "source import unit kind is unsupported");
}

[[nodiscard]] Core::Status appendCookRequest(CatalogCookRequest& combined,
                                             bool& hasTargetPlatform,
                                             CatalogCookRequest source)
{
    if (!hasTargetPlatform)
    {
        combined.targetPlatform = source.targetPlatform;
        hasTargetPlatform = true;
    } else if (combined.targetPlatform != source.targetPlatform)
    {
        return Core::failure(AssetErrorCode::SourceImportTargetPlatformMismatch,
                             "source import units target different platforms");
    }
    combined.assets.reserve(combined.assets.size() + source.assets.size());
    for (auto& asset : source.assets)
    {
        combined.assets.push_back(std::move(asset));
    }
    return Core::success();
}

[[nodiscard]] Core::Status commitState(const SourceImportPipelineRequest& request,
                                       std::string_view catalogRoot,
                                       std::string_view statePath,
                                       const SourceImportCandidate& candidate)
{
    auto revision = captureCatalogPackageRevision(catalogRoot, revisionConfig(request));
    if (!revision)
    {
        return Core::failure(std::move(revision.error()));
    }
    return commitSourceImportCandidate(
        statePath, candidate,
        AssetFormat::SourceImportManifestRevision{
            .manifestDigest = revision->manifestDigest,
            .manifestBytes = revision->manifestBytes,
        });
}

[[nodiscard]] SourceImportPipelineResult
makeResultBase(const SourceImportPipelineRequest& request,
               SourceImportPipelineMode mode,
               SourceImportProbeState probeState,
               SourceImportProbeReason probeReason,
               std::string_view catalogRoot,
               std::string_view statePath)
{
    return SourceImportPipelineResult{
        .mode = mode,
        .probeState = probeState,
        .probeReason = probeReason,
        .unitsTotal = static_cast<Core::u32>(request.units.size()),
        .catalogRootUtf8 = std::string(catalogRoot),
        .stateUtf8Path = std::string(statePath),
    };
}

[[nodiscard]] Core::Result<SourceImportPipelineResult>
executeSourceImportPipelineImpl(const SourceImportPipelineRequest& request,
                                const std::stop_token stopToken)
{
    if (const auto status = validateRequest(request); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (pipelineMemory(request) == nullptr ||
        request.stageConfig.validation.validation.file.memoryResource == nullptr)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "source import pipeline requires validation memory resources");
    }
    if (const auto status = checkStopped(stopToken); !status)
    {
        return Core::failure(std::move(status.error()));
    }

    auto loaded = loadBaseline(request);
    if (!loaded)
    {
        return Core::failure(std::move(loaded.error()).withContext("executeSourceImportPipeline",
                                                                   "loadBaseline"));
    }

    if (loaded->baseline &&
        loaded->baseline->metadata.header().targetPlatform != request.targetPlatform)
    {
        loaded->baseline.reset();
        loaded->probeState = SourceImportProbeState::Dirty;
        loaded->probeReason = SourceImportProbeReason::SettingsChanged;
    }
    if (request.units.empty() && !loaded->baseline)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "empty source import intended set requires a valid baseline");
    }

    if (loaded->baseline)
    {
        auto& baseline = *loaded->baseline;
        std::vector<SourceImportUnitProbeDesc> descriptions;
        descriptions.reserve(request.units.size());
        for (const auto& unit : request.units)
        {
            auto description = makeProbeDesc(unit, request.sourceRootUtf8,
                                             baseline.metadata.header().targetPlatform);
            if (!description)
            {
                return Core::failure(std::move(description.error()));
            }
            descriptions.push_back(std::move(*description));
        }
        auto batch = probeSourceImportUnits(baseline.metadata, baseline.revision, descriptions);
        if (!batch)
        {
            return Core::failure(std::move(batch.error()));
        }
        if (batch->removedUnitCount == 0U && batch->dirtyUnitCount == 0U)
        {
            auto result = makeResultBase(request, SourceImportPipelineMode::CleanReuse,
                                         SourceImportProbeState::Clean,
                                         SourceImportProbeReason::None,
                                         request.baselineCatalogRootUtf8,
                                         request.baselineStateUtf8Path);
            result.objectsReused = baseline.catalog.entryCount();
            result.catalogEntries = baseline.catalog.entryCount();
            result.catalogDependencies = baseline.catalog.dependencyCount();
            result.importStateCommitted = false;
            return result;
        }
        if (request.stageCatalogRootUtf8.empty())
        {
            return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                 "dirty source import requires a fresh stage Catalog and state");
        }

        SourceImportProbeReason probeReason = SourceImportProbeReason::UnitSetChanged;
        if (batch->removedUnitCount == 0U)
        {
            const auto dirty = std::find_if(batch->units.begin(), batch->units.end(),
                                            [](const SourceImportProbeResult& unit) {
                                                return unit.state != SourceImportProbeState::Clean;
                                            });
            if (dirty != batch->units.end())
            {
                probeReason = dirty->reason;
            }
        }
        CatalogCookRequest dirtyRequest{.targetPlatform = request.targetPlatform};
        bool hasTargetPlatform = true;
        std::vector<SourceImportCandidate> recookedCandidates;
        std::vector<AssetFormat::SourceImportUnitId> retainedUnitIds;
        recookedCandidates.reserve(batch->dirtyUnitCount);
        retainedUnitIds.reserve(batch->cleanUnitCount);
        for (Core::usize index = 0; index < request.units.size(); ++index)
        {
            if (batch->units[index].state == SourceImportProbeState::Clean)
            {
                retainedUnitIds.push_back(descriptions[index].expected.unitId);
                continue;
            }
            if (const auto status = checkStopped(stopToken); !status)
            {
                return Core::failure(std::move(status.error()));
            }
            auto cooked = cookUnit(request.units[index], request.sourceRootUtf8,
                                   request.targetPlatform);
            if (!cooked)
            {
                return Core::failure(std::move(cooked.error()));
            }
            if (const auto status = appendCookRequest(dirtyRequest, hasTargetPlatform,
                                                      std::move(cooked->request)); !status)
            {
                return Core::failure(std::move(status.error()));
            }
            recookedCandidates.push_back(std::move(cooked->sourceImports));
        }
        auto composed = composeSourceImportCandidate(SourceImportCandidateComposeDesc{
            .baseline = &baseline.metadata,
            .retainedUnitIds = retainedUnitIds,
            .recookedCandidates = recookedCandidates,
        });
        if (!composed)
        {
            return Core::failure(std::move(composed.error()));
        }
        if (const auto status = checkStopped(stopToken); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        auto result = makeResultBase(
            request,
            batch->cleanUnitCount == 0U && batch->removedUnitCount == 0U
                ? SourceImportPipelineMode::FullRecook
                : SourceImportPipelineMode::IncrementalRecook,
            SourceImportProbeState::Dirty, probeReason, request.stageCatalogRootUtf8,
            request.stageStateUtf8Path);
        result.unitsRecooked = batch->dirtyUnitCount;
        result.unitsRemoved = batch->removedUnitCount;
        result.objectsReused = static_cast<Core::u32>(composed->retainedAssetIds.size());
        result.objectsCooked = static_cast<Core::u32>(dirtyRequest.assets.size());
        result.stageCreated = true;
        auto staged = cookAndStageIncrementalCatalogPackage(
            request.stageCatalogRootUtf8, request.baselineCatalogRootUtf8, baseline.catalog,
            composed->retainedAssetIds, dirtyRequest, request.stageConfig);
        if (!staged)
        {
            return Core::failure(std::move(staged.error()));
        }
        if (const auto status = checkStopped(stopToken); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        if (const auto status = commitState(request, request.stageCatalogRootUtf8,
                                            request.stageStateUtf8Path, composed->candidate); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        result.catalogEntries = staged->entryCount();
        result.catalogDependencies = staged->dependencyCount();
        result.importStateCommitted = true;
        return result;
    }

    if (loaded->catalogPresent && request.stageCatalogRootUtf8.empty())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                             "full source recook of an existing Catalog requires a fresh stage");
    }
    const std::string_view targetRoot = request.stageCatalogRootUtf8.empty()
                                            ? request.baselineCatalogRootUtf8
                                            : request.stageCatalogRootUtf8;
    const std::string_view targetState = request.stageStateUtf8Path.empty()
                                             ? request.baselineStateUtf8Path
                                             : request.stageStateUtf8Path;
    CatalogCookRequest cookRequest{.targetPlatform = request.targetPlatform};
    bool hasTargetPlatform = true;
    std::vector<SourceImportCandidate> candidates;
    candidates.reserve(request.units.size());
    for (const auto& unit : request.units)
    {
        if (const auto status = checkStopped(stopToken); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        auto cooked = cookUnit(unit, request.sourceRootUtf8, request.targetPlatform);
        if (!cooked)
        {
            return Core::failure(std::move(cooked.error()));
        }
        if (const auto status = appendCookRequest(cookRequest, hasTargetPlatform,
                                                  std::move(cooked->request)); !status)
        {
            return Core::failure(std::move(status.error()));
        }
        candidates.push_back(std::move(cooked->sourceImports));
    }
    auto composed = composeSourceImportCandidate(
        SourceImportCandidateComposeDesc{.recookedCandidates = candidates});
    if (!composed)
    {
        return Core::failure(std::move(composed.error()));
    }
    if (const auto status = checkStopped(stopToken); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    auto result = makeResultBase(request, SourceImportPipelineMode::FullRecook,
                                 loaded->probeState, loaded->probeReason, targetRoot, targetState);
    result.unitsRecooked = static_cast<Core::u32>(request.units.size());
    result.stageCreated = true;
    auto staged = cookAndStageCatalogPackage(targetRoot, cookRequest, request.stageConfig);
    if (!staged)
    {
        return Core::failure(std::move(staged.error()));
    }
    if (const auto status = checkStopped(stopToken); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (const auto status = commitState(request, targetRoot, targetState, composed->candidate); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    result.objectsCooked = staged->entryCount();
    result.catalogEntries = staged->entryCount();
    result.catalogDependencies = staged->dependencyCount();
    result.importStateCommitted = true;
    return result;
}

} // namespace

Core::Result<SourceImportPipelineResult>
executeSourceImportPipeline(const SourceImportPipelineRequest& request,
                            const std::stop_token stopToken) noexcept
{
    try
    {
        return executeSourceImportPipelineImpl(request, stopToken);
    } catch (const std::bad_alloc&)
    {
        return Core::failure(AssetErrorCode::AllocationFailed,
                             "source import pipeline allocation failed");
    } catch (const std::filesystem::filesystem_error& exception)
    {
        return Core::failure(Core::Error{Core::CoreErrorCode::Io,
                                         "source import pipeline filesystem operation failed"}
                                 .setNativeCode(exception.code().value()));
    } catch (const std::exception&)
    {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "source import pipeline failed unexpectedly");
    }
}

} // namespace Tina::Asset
