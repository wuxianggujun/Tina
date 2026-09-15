#include "EditorWorkspaceState.hpp"

#include <tina/asset/CatalogCook.hpp>
#include <tina/asset_format/Prefab2DPayload.hpp>
#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/EditorSceneOperations.hpp>

#include <array>
#include <filesystem>
#include <memory_resource>
#include <utility>

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::prefab2DEditingContext() const noexcept -> bool
{
    const auto* tab = documentTabs_.activeTab();
    return tab != nullptr &&
           tab->key.kind == Tina::Editor::EditorDocumentKind::World2D &&
           tab->key.assetId;
}

auto EditorWorkspaceState::allocateUnusedCatalogAssetId(u8 marker) const
    -> Tina::Core::Result<Tina::Core::AssetId>
{
    if (!assetResources_.system.has_value() ||
        assetResources_.system->catalog() == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Prefab2D allocation requires an open Editor Catalog");
    }
    const Tina::Asset::CatalogSnapshot& catalog = *assetResources_.system->catalog();
    for (u32 serial = 1U; serial < 0x10000U; ++serial) {
        Tina::Core::AssetId::Bytes bytes{};
        bytes[0] = static_cast<std::byte>(marker);
        bytes[1] = static_cast<std::byte>(serial & 0xFFU);
        bytes[2] = static_cast<std::byte>((serial >> 8U) & 0xFFU);
        const auto id = Tina::Core::AssetId::fromBytes(bytes);
        if (!id) {
            continue;
        }
        if (!catalog.find(*id).has_value()) {
            return *id;
        }
    }
    return Tina::Core::failure(
        Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
        "Editor Catalog has no free Prefab2D identity in the authoring range");
}

auto EditorWorkspaceState::publishPrefab2DToCatalog(
    Tina::Core::AssetId assetId,
    std::span<const Tina::AssetFormat::World2DEntityDesc> entities)
    -> Tina::Core::Status
{
    if (!assetResources_.system.has_value() ||
        assetResources_.system->catalog() == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "Prefab2D publication requires an open Editor Catalog");
    }
    auto payload = Tina::AssetFormat::writePrefab2DPayloadBytes(entities);
    if (!payload) {
        return Tina::Core::failure(std::move(payload.error()));
    }
    auto dependencies = Tina::AssetFormat::collectPrefab2DDependencies(entities);
    if (!dependencies) {
        return Tina::Core::failure(std::move(dependencies.error()));
    }

    const Tina::Asset::CatalogSnapshot& baseline = *assetResources_.system->catalog();
    std::vector<Tina::Core::AssetId> cleanAssetIds;
    try {
        cleanAssetIds.reserve(baseline.entryCount());
        for (u32 index = 0; index < baseline.entryCount(); ++index) {
            const auto entry = baseline.entry(index);
            if (!entry) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::Internal,
                    "Prefab2D Catalog baseline entry disappeared");
            }
            if (entry->assetId != assetId) {
                cleanAssetIds.push_back(entry->assetId);
            }
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Prefab2D Catalog baseline allocation failed");
    }

    Tina::Asset::CatalogCookRequest dirtyRequest{
        .targetPlatform = editorTargetPlatform(),
    };
    dirtyRequest.assets.push_back(Tina::Asset::CatalogCookAssetSpec{
        .assetKind = Tina::AssetFormat::AssetKind::Prefab2D,
        .assetId = assetId,
        .assetTypeVersion = Tina::AssetFormat::Prefab2DWire::SchemaVersion,
        .payload = std::move(*payload),
        .dependencies = std::move(*dependencies),
    });

    auto stageParent = activeProjectWorkspace_.has_value()
        ? createAuthoringStageRoot(*activeProjectWorkspace_)
        : createUniqueEditorTempDirectory();
    if (!stageParent) {
        return Tina::Core::failure(std::move(stageParent.error()));
    }
    auto cleanupStage = Tina::Core::makeScopeExit([&stageParent]() noexcept {
        std::error_code cleanupError;
        std::filesystem::remove_all(*stageParent, cleanupError);
    });
    const std::filesystem::path catalogPath = *stageParent / "catalog";
    const std::string catalogRootUtf8 = pathToUtf8(catalogPath);
    std::pmr::unsynchronized_pool_resource operationMemory{};
    Tina::Asset::CatalogPackageStageConfig stageConfig =
        Tina::EditorApp::Detail::makeEditorSourceImportStageConfig(operationMemory);
    auto staged = Tina::Asset::cookAndStageIncrementalCatalogPackage(
        catalogRootUtf8, assetResources_.catalogRootUtf8, baseline, cleanAssetIds,
        dirtyRequest, stageConfig);
    if (!staged) {
        return Tina::Core::failure(std::move(staged.error()));
    }

    const auto previousFilter = projectAssets_.typeFilter();
    std::optional<Tina::Core::AssetId> previousSelection = projectAssets_.selectedAssetId();
    const std::string_view previousSearchQuery = projectAssets_.searchQuery();
    auto browser = prepareProjectBrowserForSnapshot(
        *staged, previousFilter, previousSelection, previousSearchQuery,
        sourceImportUnitOutputs_, assetMetadata_,
        activeProjectWorkspace_.has_value()
            ? activeProjectWorkspace_->sourceRootUtf8()
            : std::string_view{});
    if (!browser) {
        return Tina::Core::failure(std::move(browser.error()));
    }

    Tina::Asset::Sprite2DBindingRegistry* spriteParticipant =
        spriteBindings_.has_value() ? &*spriteBindings_ : nullptr;
    Tina::Asset::Mesh3DBindingRegistry* meshParticipant =
        mesh3DBindings_.has_value() ? &*mesh3DBindings_ : nullptr;
    Tina::Asset::ShaderBindingRegistry* shaderParticipant =
        shaderBindings_.has_value() ? &*shaderBindings_ : nullptr;
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
    if (shaderParticipant != nullptr) {
        reloadConfig.bindings.shader =
            std::span<Tina::Asset::ShaderBindingRegistry*>{&shaderParticipant, 1U};
    }
    const bool persistentProjectStage = activeProjectWorkspace_.has_value();
    bool temporaryOwnerRegistered = false;
    if (!persistentProjectStage) {
        try {
            assetResources_.ownedCatalogStageRoots.reserve(
                assetResources_.ownedCatalogStageRoots.size() + 1U);
            assetResources_.ownedCatalogStageRoots.push_back(*stageParent);
            temporaryOwnerRegistered = true;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Prefab2D Catalog stage ownership allocation failed before reload");
        }
    }

    std::filesystem::path authoringPointerPath;
    std::string previousPointerBytes;
    bool pointerExisted = false;
    if (activeProjectWorkspace_.has_value()) {
        const auto cache = authoringCachePaths(*activeProjectWorkspace_);
        authoringPointerPath = cache.activeCatalogPointer;
        std::error_code pointerError;
        const auto pointerStatus = std::filesystem::symlink_status(authoringPointerPath, pointerError);
        if (!pointerError && std::filesystem::is_regular_file(pointerStatus)) {
            auto bytes = Tina::Core::readFile(pathToUtf8(authoringPointerPath),
                                              {.maxBytes = 4096, .memoryResource = &operationMemory});
            if (!bytes) {
                if (temporaryOwnerRegistered) {
                    assetResources_.ownedCatalogStageRoots.pop_back();
                }
                return Tina::Core::failure(std::move(bytes.error()));
            }
            previousPointerBytes.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            pointerExisted = true;
        } else if (pointerError && pointerError != std::errc::no_such_file_or_directory) {
            if (temporaryOwnerRegistered) {
                assetResources_.ownedCatalogStageRoots.pop_back();
            }
            return Tina::Core::failure(Tina::Core::CoreErrorCode::Io,
                                       "Prefab2D authoring Catalog pointer inspection failed");
        }
        const std::string stageRootText = catalogRootUtf8;
        const auto pointerText = std::as_bytes(
            std::span{stageRootText.data(), stageRootText.size()});
        if (auto status = Tina::Core::writeFile(pathToUtf8(authoringPointerPath), pointerText);
            !status) {
            if (temporaryOwnerRegistered) {
                assetResources_.ownedCatalogStageRoots.pop_back();
            }
            return status;
        }
    }

    auto reload = assetResources_.system->reloadCatalog(catalogRootUtf8, reloadConfig);
    if (!reload) {
        if (activeProjectWorkspace_.has_value()) {
            if (pointerExisted) {
                const auto bytes = std::as_bytes(std::span{previousPointerBytes.data(), previousPointerBytes.size()});
                (void)Tina::Core::writeFile(pathToUtf8(authoringPointerPath), bytes);
            } else {
                std::error_code pointerError;
                std::filesystem::remove(authoringPointerPath, pointerError);
            }
        }
        if (temporaryOwnerRegistered) {
            assetResources_.ownedCatalogStageRoots.pop_back();
        }
        return reportAuthoringFailure(
            "Prefab2D staged; previous Catalog preserved: ", reload.error());
    }

    std::string nextCatalogRoot = catalogRootUtf8;
    std::string supersededAuthoringCatalogRoot;
    if (persistentProjectStage) {
        supersededAuthoringCatalogRoot = assetResources_.authoringCatalogRootUtf8;
    }
    cleanupStage.release();
    assetResources_.catalogRootUtf8.swap(nextCatalogRoot);
    if (persistentProjectStage) {
        assetResources_.authoringCatalogRootUtf8 = assetResources_.catalogRootUtf8;
        cleanupOwnedAuthoringStage(supersededAuthoringCatalogRoot);
    }
    assetResources_.catalogEntryCount = static_cast<u32>(browser->itemCount());
    counters_.catalogEntryCount = assetResources_.catalogEntryCount;
    projectAssets_ = std::move(*browser);
    observedProjectAssetSelectionIndex_.reset();
    projectBrowserUiRefreshPending_ = true;
    previewAssetBindingsRefreshPending_ = true;
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
