#include <tina/editor/EditorComponentOperations.hpp>

#include <tina/editor/EditorErrors.hpp>
#include <tina/editor/EditorSceneOperations.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace Tina::Editor {
namespace {

using AssetFormat::PrefabNodeView;
using AssetFormat::World2DEntityDesc;

[[nodiscard]] Core::AssetId testAssetId(char digit)
{
    std::array<char, 32> text{};
    text.fill(digit);
    const auto parsed =
        Core::AssetId::parseCanonical(std::string_view{text.data(), text.size()});
    EXPECT_TRUE(parsed.has_value());
    return *parsed;
}

[[nodiscard]] World2DAuthoringDocument createWorld2D(
    World2DAuthoringDocumentConfig config = {})
{
    auto document = World2DAuthoringDocument::Create(config);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

[[nodiscard]] World3DAuthoringDocument createWorld3D(
    World3DAuthoringDocumentConfig config = {})
{
    auto document = World3DAuthoringDocument::Create(config);
    EXPECT_TRUE(document) << (document ? "" : document.error().message);
    return std::move(*document);
}

[[nodiscard]] std::vector<World2DEntityDesc> world2DEntities(
    const World2DAuthoringDocument& document)
{
    std::vector<World2DEntityDesc> storage;
    auto snapshot = document.parseCurrentSnapshot(storage);
    EXPECT_TRUE(snapshot) << (snapshot ? "" : snapshot.error().message);
    return storage;
}

[[nodiscard]] const World2DEntityDesc* findEntity(
    const std::vector<World2DEntityDesc>& entities, Core::u32 stableId)
{
    const auto found = std::find_if(
        entities.begin(), entities.end(), [stableId](const auto& entity) {
            return entity.stableEntityId == stableId;
        });
    return found == entities.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<PrefabNodeView> world3DNodes(
    const World3DAuthoringDocument& document)
{
    std::vector<PrefabNodeView> storage;
    auto prefab = document.parseCurrentPrefab(storage);
    EXPECT_TRUE(prefab) << (prefab ? "" : prefab.error().message);
    return storage;
}

struct DocumentFingerprint final {
    std::vector<std::byte> bytes{};
    Core::u64 revision = 0;
    Core::usize historyEntries = 0;
    Core::usize historyBytes = 0;
    Core::usize undoDepth = 0;
    Core::usize redoDepth = 0;
};

[[nodiscard]] DocumentFingerprint fingerprint(const World2DAuthoringDocument& document)
{
    return {
        .bytes = std::vector(document.snapshotBytes().begin(),
                             document.snapshotBytes().end()),
        .revision = document.revision(),
        .historyEntries = document.historyEntryCount(),
        .historyBytes = document.historyByteCount(),
        .undoDepth = document.undoDepth(),
        .redoDepth = document.redoDepth(),
    };
}

void expectUnchanged(const World2DAuthoringDocument& document,
                     const DocumentFingerprint& before)
{
    const auto after = fingerprint(document);
    EXPECT_EQ(after.bytes, before.bytes);
    EXPECT_EQ(after.revision, before.revision);
    EXPECT_EQ(after.historyEntries, before.historyEntries);
    EXPECT_EQ(after.historyBytes, before.historyBytes);
    EXPECT_EQ(after.undoDepth, before.undoDepth);
    EXPECT_EQ(after.redoDepth, before.redoDepth);
}

TEST(EditorComponentOperationsTests, RegistryListsEveryWorld2DComponent)
{
    const auto registry = world2DComponentRegistry();
    ASSERT_EQ(registry.size(), World2DComponentKindCount);
    for (Core::usize index = 0; index < registry.size(); ++index) {
        EXPECT_EQ(static_cast<Core::usize>(registry[index].kind), index);
        EXPECT_FALSE(registry[index].displayName.empty());
        EXPECT_TRUE(registry[index].removable);
    }
}

TEST(EditorComponentOperationsTests, AddAndRemovePublishOneRevisionEach)
{
    auto document = createWorld2D();
    auto entity = addWorld2DEntity(document);
    ASSERT_TRUE(entity);
    const std::array ids{entity->primaryStableId};
    Core::u64 revision = document.revision();

    auto added = addWorld2DComponent(document, ids,
                                     World2DComponentKind::PointLight);
    ASSERT_TRUE(added) << added.error().message;
    EXPECT_EQ(added->affectedItemCount, 1U);
    EXPECT_EQ(document.revision(), ++revision);
    auto entities = world2DEntities(document);
    ASSERT_NE(findEntity(entities, ids[0]), nullptr);
    EXPECT_TRUE(hasWorld2DComponent(*findEntity(entities, ids[0]),
                                    World2DComponentKind::PointLight));

    auto removed = removeWorld2DComponent(document, ids,
                                          World2DComponentKind::PointLight);
    ASSERT_TRUE(removed) << removed.error().message;
    EXPECT_EQ(removed->affectedItemCount, 1U);
    EXPECT_EQ(document.revision(), ++revision);
    entities = world2DEntities(document);
    EXPECT_FALSE(hasWorld2DComponent(*findEntity(entities, ids[0]),
                                     World2DComponentKind::PointLight));

    ASSERT_TRUE(document.undo());
    entities = world2DEntities(document);
    EXPECT_TRUE(hasWorld2DComponent(*findEntity(entities, ids[0]),
                                    World2DComponentKind::PointLight));
    ASSERT_TRUE(document.redo());
    entities = world2DEntities(document);
    EXPECT_FALSE(hasWorld2DComponent(*findEntity(entities, ids[0]),
                                     World2DComponentKind::PointLight));
}

TEST(EditorComponentOperationsTests, AddSpriteRequiresAssetId)
{
    auto document = createWorld2D();
    auto entity = addWorld2DEntity(document);
    ASSERT_TRUE(entity);
    const std::array ids{entity->primaryStableId};
    const auto before = fingerprint(document);

    auto missing = addWorld2DComponent(document, ids,
                                       World2DComponentKind::Sprite);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, EditorErrorCode::InvalidAuthoringOperation);
    expectUnchanged(document, before);

    auto added = addWorld2DComponent(document, ids,
                                     World2DComponentKind::Sprite,
                                     testAssetId('a'));
    ASSERT_TRUE(added) << added.error().message;
    const auto entities = world2DEntities(document);
    ASSERT_TRUE(findEntity(entities, ids[0])->sprite.has_value());
    EXPECT_EQ(findEntity(entities, ids[0])->sprite->spriteId, testAssetId('a'));
}

TEST(EditorComponentOperationsTests, RedundantAddAndRemoveFailClosed)
{
    auto document = createWorld2D();
    auto entity = addWorld2DEntity(document);
    ASSERT_TRUE(entity);
    const std::array ids{entity->primaryStableId};
    ASSERT_TRUE(addWorld2DComponent(document, ids,
                                    World2DComponentKind::Camera));
    const auto before = fingerprint(document);

    auto redundantAdd = addWorld2DComponent(document, ids,
                                            World2DComponentKind::Camera);
    ASSERT_FALSE(redundantAdd);
    EXPECT_EQ(redundantAdd.error().code, EditorErrorCode::ComponentAlreadyPresent);
    expectUnchanged(document, before);

    auto missingRemove = removeWorld2DComponent(
        document, ids, World2DComponentKind::PointLight);
    ASSERT_FALSE(missingRemove);
    EXPECT_EQ(missingRemove.error().code, EditorErrorCode::ComponentNotFound);
    expectUnchanged(document, before);

    const std::array unknownIds{Core::u32{999}};
    auto unknown = addWorld2DComponent(document, unknownIds,
                                       World2DComponentKind::PointLight);
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code, EditorErrorCode::EntityNotFound);
    expectUnchanged(document, before);

    auto emptySelection = addWorld2DComponent(document, {},
                                              World2DComponentKind::PointLight);
    ASSERT_FALSE(emptySelection);
    EXPECT_EQ(emptySelection.error().code,
              EditorErrorCode::InvalidAuthoringOperation);
    expectUnchanged(document, before);
}

TEST(EditorComponentOperationsTests, MultiSelectAddFillsOnlyMissingComponents)
{
    auto document = createWorld2D();
    auto first = addWorld2DEntity(document);
    auto second = addWorld2DEntity(document);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const std::array firstOnly{first->primaryStableId};
    ASSERT_TRUE(addWorld2DComponent(document, firstOnly,
                                    World2DComponentKind::PointLight));
    Core::u64 revision = document.revision();

    const std::array both{first->primaryStableId, second->primaryStableId};
    auto added = addWorld2DComponent(document, both,
                                     World2DComponentKind::PointLight);
    ASSERT_TRUE(added) << added.error().message;
    EXPECT_EQ(added->affectedItemCount, 1U);
    EXPECT_EQ(added->primaryStableId, second->primaryStableId);
    EXPECT_EQ(document.revision(), ++revision);

    auto removed = removeWorld2DComponent(document, both,
                                          World2DComponentKind::PointLight);
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->affectedItemCount, 2U);
    EXPECT_EQ(document.revision(), ++revision);
}

TEST(EditorComponentOperationsTests, BatchEditAppliesSetFieldsAndKeepsMixedFields)
{
    auto document = createWorld2D();
    auto first = addWorld2DEntity(document);
    auto second = addWorld2DEntity(document);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const std::array both{first->primaryStableId, second->primaryStableId};
    ASSERT_TRUE(addWorld2DComponent(document, both,
                                    World2DComponentKind::PointLight));
    const std::array firstOnly{first->primaryStableId};
    ASSERT_TRUE(applyWorld2DPointLightEdit(document, firstOnly,
                                           {.radiusMeters = 2.0F}));
    Core::u64 revision = document.revision();

    // intensity edited on both; radius left "Mixed" and must stay per-entity.
    auto edited = applyWorld2DPointLightEdit(document, both,
                                             {.intensity = 3.0F});
    ASSERT_TRUE(edited) << edited.error().message;
    EXPECT_EQ(edited->affectedItemCount, 2U);
    EXPECT_EQ(document.revision(), ++revision);
    auto entities = world2DEntities(document);
    EXPECT_FLOAT_EQ(findEntity(entities, both[0])->pointLight->intensity, 3.0F);
    EXPECT_FLOAT_EQ(findEntity(entities, both[1])->pointLight->intensity, 3.0F);
    EXPECT_FLOAT_EQ(findEntity(entities, both[0])->pointLight->radiusMeters, 2.0F);
    EXPECT_FLOAT_EQ(findEntity(entities, both[1])->pointLight->radiusMeters, 4.0F);

    // Re-applying identical values succeeds without publishing a revision.
    auto noop = applyWorld2DPointLightEdit(document, both, {.intensity = 3.0F});
    ASSERT_TRUE(noop);
    EXPECT_EQ(noop->affectedItemCount, 0U);
    EXPECT_EQ(document.revision(), revision);

    // A fully-Mixed edit is also a successful no-op.
    auto allMixed = applyWorld2DPointLightEdit(document, both, {});
    ASSERT_TRUE(allMixed);
    EXPECT_EQ(allMixed->affectedItemCount, 0U);
    EXPECT_EQ(document.revision(), revision);
}

TEST(EditorComponentOperationsTests, BatchEditFailsClosedOnInvalidInput)
{
    auto document = createWorld2D();
    auto entity = addWorld2DEntity(document);
    ASSERT_TRUE(entity);
    const std::array ids{entity->primaryStableId};
    ASSERT_TRUE(addWorld2DComponent(document, ids,
                                    World2DComponentKind::PointLight));
    ASSERT_TRUE(addWorld2DComponent(document, ids,
                                    World2DComponentKind::Camera));
    const auto before = fingerprint(document);

    const float infinity = std::numeric_limits<float>::infinity();
    auto nonFinite = applyWorld2DPointLightEdit(document, ids,
                                                {.intensity = infinity});
    ASSERT_FALSE(nonFinite);
    EXPECT_EQ(nonFinite.error().code, EditorErrorCode::InvalidAuthoringOperation);
    expectUnchanged(document, before);

    // Negative radius passes the finite gate and is rejected by AssetFormat
    // validation inside replace(); the document must stay untouched.
    auto invalidRadius = applyWorld2DPointLightEdit(document, ids,
                                                    {.radiusMeters = -1.0F});
    ASSERT_FALSE(invalidRadius);
    expectUnchanged(document, before);

    // Editing a component the entity does not have fails closed.
    auto missingComponent = applyWorld2DSpriteEdit(document, ids,
                                                   {.visible = false});
    ASSERT_FALSE(missingComponent);
    EXPECT_EQ(missingComponent.error().code, EditorErrorCode::ComponentNotFound);
    expectUnchanged(document, before);

    auto zeroSprite = applyWorld2DSpriteEdit(document, ids,
                                             {.spriteId = Core::AssetId{}});
    ASSERT_FALSE(zeroSprite);
    EXPECT_EQ(zeroSprite.error().code, EditorErrorCode::InvalidAuthoringOperation);
    expectUnchanged(document, before);
}

TEST(EditorComponentOperationsTests, SpriteEditSetsOverrideFlags)
{
    auto document = createWorld2D();
    auto entity = addWorld2DEntity(document);
    ASSERT_TRUE(entity);
    const std::array ids{entity->primaryStableId};
    ASSERT_TRUE(addWorld2DComponent(document, ids, World2DComponentKind::Sprite,
                                    testAssetId('b')));

    auto edited = applyWorld2DSpriteEdit(
        document, ids,
        {.sizeX = 2.0F, .sizeY = 3.0F, .pivotX = 0.0F,
         .color = std::array<Core::u8, 4>{10, 20, 30, 255},
         .sortingLayer = Core::i16{5}, .flipX = true});
    ASSERT_TRUE(edited) << edited.error().message;
    const auto entities = world2DEntities(document);
    const auto& sprite = *findEntity(entities, ids[0])->sprite;
    EXPECT_FLOAT_EQ(sprite.sizeX, 2.0F);
    EXPECT_FLOAT_EQ(sprite.sizeY, 3.0F);
    EXPECT_FLOAT_EQ(sprite.pivotX, 0.0F);
    EXPECT_TRUE(hasFlag(sprite.overrides,
                        AssetFormat::World2DSpriteOverrideFlags::Size));
    EXPECT_TRUE(hasFlag(sprite.overrides,
                        AssetFormat::World2DSpriteOverrideFlags::Pivot));
    EXPECT_EQ(sprite.colorRed, 10U);
    EXPECT_EQ(sprite.sortingLayer, 5);
    EXPECT_TRUE(sprite.flipX);
}

TEST(EditorComponentOperationsTests, CameraAndOccluderEditsRoundTrip)
{
    auto document = createWorld2D();
    auto entity = addWorld2DEntity(document);
    ASSERT_TRUE(entity);
    const std::array ids{entity->primaryStableId};
    ASSERT_TRUE(addWorld2DComponent(document, ids, World2DComponentKind::Camera));
    ASSERT_TRUE(addWorld2DComponent(document, ids,
                                    World2DComponentKind::ShadowOccluder));

    auto camera = applyWorld2DCameraEdit(
        document, ids,
        {.projection = AssetFormat::World2DCameraProjectionKind::PixelPerfect,
         .pixelSnap = AssetFormat::World2DPixelSnapPolicy::CameraAndSprites,
         .fixedWorldHeightMeters = 12.0F,
         .referenceHeightPixels = Core::u32{192}, .active = false});
    ASSERT_TRUE(camera) << camera.error().message;
    auto occluder = applyWorld2DShadowOccluderEdit(
        document, ids, {.localStartX = -1.0F, .localEndX = 1.5F});
    ASSERT_TRUE(occluder) << occluder.error().message;

    const auto entities = world2DEntities(document);
    const auto* edited = findEntity(entities, ids[0]);
    EXPECT_EQ(edited->camera->projection,
              AssetFormat::World2DCameraProjectionKind::PixelPerfect);
    EXPECT_FLOAT_EQ(edited->camera->fixedWorldHeightMeters, 12.0F);
    EXPECT_FALSE(edited->camera->active);
    EXPECT_FLOAT_EQ(edited->shadowOccluder->localStartX, -1.0F);
    EXPECT_FLOAT_EQ(edited->shadowOccluder->localEndX, 1.5F);
}

TEST(EditorComponentOperationsTests, World3DMeshRendererAddEditRemove)
{
    auto document = createWorld3D();
    const auto nodes = world3DNodes(document);
    ASSERT_FALSE(nodes.empty());
    const std::array ids{nodes.front().stableNodeId};
    Core::u64 revision = document.revision();

    auto unpaired = addWorld3DMeshRenderer(document, ids, testAssetId('c'),
                                           Core::AssetId{});
    ASSERT_FALSE(unpaired);
    EXPECT_EQ(unpaired.error().code, EditorErrorCode::InvalidAuthoringOperation);
    EXPECT_EQ(document.revision(), revision);

    auto missingEdit = applyWorld3DMeshRendererEdit(document, ids,
                                                    {.visible = false});
    ASSERT_FALSE(missingEdit);
    EXPECT_EQ(missingEdit.error().code, EditorErrorCode::ComponentNotFound);

    auto added = addWorld3DMeshRenderer(document, ids, testAssetId('c'),
                                        testAssetId('d'));
    ASSERT_TRUE(added) << added.error().message;
    EXPECT_EQ(document.revision(), ++revision);
    EXPECT_TRUE(hasWorld3DMeshRenderer(world3DNodes(document).front()));

    auto redundant = addWorld3DMeshRenderer(document, ids, testAssetId('c'),
                                            testAssetId('d'));
    ASSERT_FALSE(redundant);
    EXPECT_EQ(redundant.error().code, EditorErrorCode::ComponentAlreadyPresent);
    EXPECT_EQ(document.revision(), revision);

    auto edited = applyWorld3DMeshRendererEdit(
        document, ids, {.materialId = testAssetId('e'), .visible = false});
    ASSERT_TRUE(edited) << edited.error().message;
    EXPECT_EQ(document.revision(), ++revision);
    auto after = world3DNodes(document);
    EXPECT_EQ(after.front().materialId, testAssetId('e'));
    EXPECT_FALSE(after.front().visible);

    auto removed = removeWorld3DMeshRenderer(document, ids);
    ASSERT_TRUE(removed) << removed.error().message;
    EXPECT_EQ(document.revision(), ++revision);
    EXPECT_FALSE(hasWorld3DMeshRenderer(world3DNodes(document).front()));

    auto missingRemove = removeWorld3DMeshRenderer(document, ids);
    ASSERT_FALSE(missingRemove);
    EXPECT_EQ(missingRemove.error().code, EditorErrorCode::ComponentNotFound);
    EXPECT_EQ(document.revision(), revision);

    ASSERT_TRUE(document.undo());
    EXPECT_TRUE(hasWorld3DMeshRenderer(world3DNodes(document).front()));
}

TEST(EditorComponentOperationsTests, SpriteAnimationRequiresSpriteAndCascadesOnSpriteRemove)
{
    auto document = createWorld2D();
    auto entity = addWorld2DEntity(document);
    ASSERT_TRUE(entity);
    const std::array ids{entity->primaryStableId};
    const auto before = fingerprint(document);

    // Requires a clip AssetId and a sprite on the entity.
    auto missingClip = addWorld2DComponent(document, ids,
                                           World2DComponentKind::SpriteAnimation);
    ASSERT_FALSE(missingClip);
    EXPECT_EQ(missingClip.error().code, EditorErrorCode::InvalidAuthoringOperation);
    expectUnchanged(document, before);

    auto missingSprite = addWorld2DComponent(document, ids,
                                             World2DComponentKind::SpriteAnimation,
                                             testAssetId('1'));
    ASSERT_FALSE(missingSprite);
    EXPECT_EQ(missingSprite.error().code,
              EditorErrorCode::InvalidAuthoringOperation);
    expectUnchanged(document, before);

    ASSERT_TRUE(addWorld2DComponent(document, ids, World2DComponentKind::Sprite,
                                    testAssetId('a')));
    auto added = addWorld2DComponent(document, ids,
                                     World2DComponentKind::SpriteAnimation,
                                     testAssetId('1'));
    ASSERT_TRUE(added) << added.error().message;
    auto entities = world2DEntities(document);
    ASSERT_TRUE(findEntity(entities, ids[0])->spriteAnimation.has_value());
    EXPECT_EQ(findEntity(entities, ids[0])->spriteAnimation->clipId,
              testAssetId('1'));
    EXPECT_TRUE(findEntity(entities, ids[0])->spriteAnimation->autoPlay);

    auto edited = applyWorld2DSpriteAnimationEdit(
        document, ids, {.playbackSpeed = 2.0F, .autoPlay = false});
    ASSERT_TRUE(edited) << edited.error().message;
    entities = world2DEntities(document);
    EXPECT_FLOAT_EQ(findEntity(entities, ids[0])->spriteAnimation->playbackSpeed,
                    2.0F);
    EXPECT_FALSE(findEntity(entities, ids[0])->spriteAnimation->autoPlay);

    auto invalidSpeed = applyWorld2DSpriteAnimationEdit(document, ids,
                                                        {.playbackSpeed = 0.0F});
    ASSERT_FALSE(invalidSpeed);
    EXPECT_EQ(invalidSpeed.error().code,
              EditorErrorCode::InvalidAuthoringOperation);

    // Removing the sprite cascades and drops the animation binding too.
    auto removedSprite = removeWorld2DComponent(document, ids,
                                                World2DComponentKind::Sprite);
    ASSERT_TRUE(removedSprite) << removedSprite.error().message;
    entities = world2DEntities(document);
    EXPECT_FALSE(findEntity(entities, ids[0])->sprite.has_value());
    EXPECT_FALSE(findEntity(entities, ids[0])->spriteAnimation.has_value());

    ASSERT_TRUE(document.undo());
    entities = world2DEntities(document);
    EXPECT_TRUE(findEntity(entities, ids[0])->spriteAnimation.has_value());
}

} // namespace
} // namespace Tina::Editor
