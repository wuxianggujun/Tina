#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::findPreviewEntity(u32 stableEntityId) const noexcept -> Tina::Scene::EntityId{
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

auto EditorWorkspaceState::stableIdForPreviewEntity(
    Tina::Scene::EntityId entity) const noexcept -> u32{
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

auto EditorWorkspaceState::findPreviewBinding(UI::UITreeViewItemKey hierarchyKey) const noexcept -> const Tina::Scene::World2DEntityBinding*{
    const u32 stableEntityId = stableEntityIdForHierarchyItem(hierarchyKey);
    const auto binding = std::find_if(
        previewBindings_.begin(), previewBindings_.end(),
        [stableEntityId](const Tina::Scene::World2DEntityBinding& candidate) {
            return candidate.stableEntityId == stableEntityId;
        });
    return binding == previewBindings_.end() ? nullptr : &*binding;
}

auto EditorWorkspaceState::resolvePreviewSprite(void* userData, Tina::Asset::AssetHandle asset,
                     Tina::Render::FrameResourceSink& sink) noexcept -> Tina::Core::Result<Tina::Render::FrameResourceRef>{
    auto& self = *static_cast<EditorWorkspaceState*>(userData);
    if (!self.spriteBindings_.has_value()) {
        return Tina::Render::FrameResourceRef{};
    }
    return self.spriteBindings_->internSpriteFrameResource(asset, sink);
}

auto EditorWorkspaceState::resolvePreviewTexture(void* userData, Tina::Asset::AssetHandle asset,
                      Tina::Render::FrameResourceSink& sink) noexcept -> Tina::Core::Result<Tina::Render::FrameResourceRef>{
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

auto EditorWorkspaceState::resolvePreviewTileset(void* userData, Tina::Asset::AssetHandle asset,
                      Tina::Render::FrameResourceSink& sink) noexcept -> Tina::Core::Result<Tina::Render::FrameResourceRef>{
    auto& self = *static_cast<EditorWorkspaceState*>(userData);
    if (!self.spriteBindings_.has_value()) {
        return Tina::Render::FrameResourceRef{};
    }
    return self.spriteBindings_->internTilesetFrameResource(asset, sink);
}

auto EditorWorkspaceState::resolvePreviewMesh(void* userData, Tina::Asset::AssetHandle asset,
                   Tina::Render::FrameResourceSink& sink) noexcept -> Tina::Core::Result<Tina::Render::FrameResourceRef>{
    auto& self = *static_cast<EditorWorkspaceState*>(userData);
    if (!self.mesh3DBindings_.has_value()) {
        return Tina::Render::FrameResourceRef{};
    }
    return self.mesh3DBindings_->internMeshFrameResource(asset, sink);
}

auto EditorWorkspaceState::resolvePreviewMaterial(void* userData, Tina::Asset::AssetHandle asset,
                       Tina::Render::FrameResourceSink& sink) noexcept -> Tina::Core::Result<Tina::Render::FrameResourceRef>{
    auto& self = *static_cast<EditorWorkspaceState*>(userData);
    if (!self.mesh3DBindings_.has_value()) {
        return Tina::Render::FrameResourceRef{};
    }
    return self.mesh3DBindings_->internMaterialFrameResource(asset, sink);
}

auto EditorWorkspaceState::loadedAsset(Tina::Core::AssetId assetId, Tina::AssetFormat::AssetKind expectedKind) const noexcept -> Tina::Asset::AssetHandle{
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

auto EditorWorkspaceState::containsHandle(std::span<const Tina::Asset::AssetHandle> handles,
                                         Tina::Asset::AssetHandle handle) noexcept -> bool{
    return std::find(handles.begin(), handles.end(), handle) != handles.end();
}

auto EditorWorkspaceState::preparePreviewAssetBindings() -> Tina::Core::Status{
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

auto EditorWorkspaceState::releasePreviewAssetBindings() noexcept -> void{
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

auto EditorWorkspaceState::validateRuntimePreview() -> Tina::Core::Status{
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
        animationPreview_.selectedFrameIndex());
    if (!animationFrame) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "Animation preview selected frame is invalid");
    }
    const Tina::Asset::AssetHandle animationSprite = loadedAsset(
        animationFrame->spriteId, Tina::AssetFormat::AssetKind::Sprite);
    animationPreview_.setPreviewAvailable(animationSprite &&
                                 containsHandle(boundSpriteAssets_, animationSprite));
    if (animationPreview_.previewAvailable()) {
        const u32 selectedStableId = stableEntityIdForHierarchyItem(selectionKey_);
        // Explicitly authored SpriteAnimation2D bindings win over the
        // selection/first-sprite preview heuristics.
        auto animationTarget = std::find_if(
            storage.begin(), storage.end(), [](const auto& entity) {
                return entity.spriteAnimation.has_value() &&
                       entity.spriteAnimation->autoPlay &&
                       entity.sprite.has_value();
            });
        if (animationTarget == storage.end()) {
            animationTarget = std::find_if(
                storage.begin(), storage.end(), [selectedStableId](const auto& entity) {
                    return entity.stableEntityId == selectedStableId &&
                           entity.sprite.has_value();
                });
        }
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
            // The animation binding targets the sprite; filtering the sprite
            // from the preview must also filter its binding.
            entity.spriteAnimation.reset();
            continue;
        }
        if (entity.spriteAnimation.has_value() &&
            !loadedAsset(entity.spriteAnimation->clipId,
                         Tina::AssetFormat::AssetKind::SpriteAnimationClip)) {
            entity.spriteAnimation.reset();
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
            .resolveAnimationClip = [this](Tina::Core::AssetId assetId) {
                return loadedAsset(assetId,
                                   Tina::AssetFormat::AssetKind::SpriteAnimationClip);
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
    counters_.animationPreviewFrameIndex = animationPreview_.selectedFrameIndex();

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

auto EditorWorkspaceState::validateWorld3DRuntimePreview() -> Tina::Core::Status{
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

auto EditorWorkspaceState::publishRuntimePreviewStatus(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
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

} // namespace Tina::EditorApp::WorkspaceInternal
