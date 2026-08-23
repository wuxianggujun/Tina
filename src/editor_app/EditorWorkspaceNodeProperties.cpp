#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

enum class World2DNodePropertyGroup : Tina::Core::u8 {
    Rendering = 0,
    Camera = 1,
    Light = 2,
    Occlusion = 3,
    Animation = 4,
};

} // namespace

auto EditorWorkspaceState::nodePropertySelectionStableIds() const
 -> Tina::Core::Result<std::vector<u32>>
try{
    if (viewportSelectedEntityCount_ == 0U) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::EntityNotFound,
            "Node property commands require a viewport scene selection");
    }
    std::vector<u32> ids;
    ids.reserve(viewportSelectedEntityCount_);
    for (Tina::Core::usize index = 0; index < viewportSelectedEntityCount_;
         ++index) {
        const u64 stableId = viewportSelectedEntityIds_[index];
        if (stableId == 0U || stableId > (std::numeric_limits<u32>::max)()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Node property selection contains an invalid stable ID");
        }
        ids.push_back(static_cast<u32>(stableId));
    }
    return ids;
}
catch (const std::bad_alloc&)
{
    return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                               "Node property selection staging allocation failed");
}

auto EditorWorkspaceState::selectedProjectAssetIdOfKind(Tina::AssetFormat::AssetKind kind) const noexcept -> Tina::Core::AssetId{
    const auto* asset = projectAssets_.selectedInspectorSnapshot();
    if (asset != nullptr && asset->assetKind == kind) {
        return asset->assetId;
    }
    return {};
}

auto EditorWorkspaceState::runNodePropertyCommand(
    Tina::PrimaryWindowUITreeUpdater& tree, EditorCommand command,
    bool& published) -> Tina::Core::Status{
    published = false;
    const auto reject = [&](const Tina::Core::Error& error) -> Tina::Core::Status {
        try {
            authoringFeedback_ = "Node property edit rejected: ";
            authoringFeedback_ += error.message;
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Node property rejection feedback allocation failed");
        }
        ++counters_.inspectorRejectedTransactions;
        return Tina::Core::success();
    };
    const auto isRejectable = [](const Tina::Core::Error& error) noexcept {
        return error.code == Tina::Editor::EditorErrorCode::InvalidAuthoringOperation ||
               error.code == Tina::Editor::EditorErrorCode::EntityNotFound ||
               error.code == Tina::Editor::EditorErrorCode::NodePropertyUnavailable ||
               error.code.domain == Tina::Core::ErrorDomain::Asset;
    };

    const bool meshCommand = command == EditorCommand::NodeToggleMeshVisible;
    if (!authoringEnabled() || assetInspectorActive_ || !sceneDocumentActive() ||
        tileMapEditingContext()) {
        return reject(Tina::Core::Error{
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Node property edits require an editable scene document"});
    }
    if (meshCommand != (workspaceMode_ == WorkspaceMode::World3D)) {
        return reject(Tina::Core::Error{
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Node property edit does not match the active workspace"});
    }

    auto idsResult = nodePropertySelectionStableIds();
    if (!idsResult) {
        if (isRejectable(idsResult.error())) {
            return reject(idsResult.error());
        }
        return Tina::Core::failure(std::move(idsResult.error()));
    }
    const std::span<const u32> ids{*idsResult};

    const auto parseFloatField = [&](UI::UINodeId field, std::string_view fieldName)
        -> Tina::Core::Result<std::optional<float>> {
        auto text = tree.text(field);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        if (*text == "n/a") {
            return std::optional<float>{};
        }
        return parseInspectorTransformValue(*text, fieldName);
    };
    const auto parseIntField = [&](UI::UINodeId field, std::string_view fieldName,
                                   long minimum, long maximum)
        -> Tina::Core::Result<std::optional<long>> {
        auto text = tree.text(field);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        if (*text == "Mixed" || *text == "n/a") {
            return std::optional<long>{};
        }
        errno = 0;
        char* end = nullptr;
        const std::string buffer{*text};
        const long value = std::strtol(buffer.c_str(), &end, 10);
        if (errno != 0 || end == buffer.c_str() || *end != '\0' ||
            value < minimum || value > maximum) {
            try {
                std::string message{fieldName};
                message += " must be an integer or Mixed";
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    message);
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Node property validation message allocation failed");
            }
        }
        return std::optional<long>{value};
    };

    // The primary (first selected) entity anchors toggle semantics: the
    // toggled value is the primary's negation applied to every selection.
    const auto primaryWorld2DEntity = [&]()
        -> Tina::Core::Result<Tina::AssetFormat::World2DEntityDesc> {
        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto entity = std::find_if(
            storage.begin(), storage.end(), [&](const auto& candidate) {
                return candidate.stableEntityId == ids.front();
            });
        if (entity == storage.end()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::EntityNotFound,
                "Node property selection is absent from the World2D document");
        }
        return *entity;
    };

    Tina::Core::Result<Tina::Editor::EditorSceneOperationResult> result =
        Tina::Editor::EditorSceneOperationResult{};
    std::string_view successVerb{};
    std::string_view propertyGroupName{};

    switch (command) {
    case EditorCommand::NodeToggleSpriteVisible: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->sprite) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Rendering properties require a Sprite2D selection");
            break;
        }
        result = Tina::Editor::applyWorld2DSpriteNodeProperties(
            document_, ids, {.visible = !primary->sprite->visible});
        successVerb = "toggled";
        propertyGroupName = "Rendering visibility";
        break;
    }
    case EditorCommand::NodeToggleCameraActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->camera) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a Camera2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DCameraNodeProperties(
            document_, ids, {.active = !primary->camera->active});
        successVerb = "toggled";
        propertyGroupName = "Camera active";
        break;
    }
    case EditorCommand::NodeTogglePointLightActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->pointLight) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a PointLight2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DPointLightNodeProperties(
            document_, ids, {.active = !primary->pointLight->active});
        successVerb = "toggled";
        propertyGroupName = "Light active";
        break;
    }
    case EditorCommand::NodeToggleShadowOccluderActive: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->shadowOccluder) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a ShadowOccluder2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DShadowOccluderNodeProperties(
            document_, ids, {.active = !primary->shadowOccluder->active});
        successVerb = "toggled";
        propertyGroupName = "Occlusion active";
        break;
    }
    case EditorCommand::NodeToggleSpriteAnimationAutoPlay: {
        auto primary = primaryWorld2DEntity();
        if (!primary) {
            result = Tina::Core::failure(std::move(primary.error()));
            break;
        }
        if (!primary->spriteAnimation) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Toggle requires a SpriteAnimation2D on the primary selection");
            break;
        }
        result = Tina::Editor::applyWorld2DAnimatedSpriteNodeProperties(
            document_, ids, {.autoPlay = !primary->spriteAnimation->autoPlay});
        successVerb = "toggled";
        propertyGroupName = "Animation auto play";
        break;
    }
    case EditorCommand::NodeApplyAnimationProperties: {
        const auto& section = nodePropertySections_[4];
        auto clipText = tree.text(section.fields[0]);
        if (!clipText) {
            return Tina::Core::failure(std::move(clipText.error()));
        }
        Tina::Editor::World2DAnimatedSpriteNodeProperties input{};
        if (*clipText != "Mixed" && *clipText != "n/a") {
            const auto parsedClip =
                Tina::Core::AssetId::parseCanonical(*clipText);
            if (!parsedClip.has_value()) {
                return reject(Tina::Core::Error{
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Clip must be a 32-hex canonical AssetId or Mixed"});
            }
            input.clipId = *parsedClip;
        }
        auto speed = parseFloatField(section.fields[1], "Speed");
        if (!speed) {
            if (isRejectable(speed.error())) {
                return reject(speed.error());
            }
            return Tina::Core::failure(std::move(speed.error()));
        }
        input.playbackSpeed = *speed;
        result = Tina::Editor::applyWorld2DAnimatedSpriteNodeProperties(document_, ids,
                                                               input);
        successVerb = "applied";
        propertyGroupName = "Animation";
        break;
    }
    case EditorCommand::NodeToggleMeshVisible: {
        std::vector<Tina::AssetFormat::PrefabNodeView> storage;
        auto prefab = document3D_.parseCurrentPrefab(storage);
        if (!prefab) {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        const auto node = std::find_if(
            storage.begin(), storage.end(), [&](const auto& candidate) {
                return candidate.stableNodeId == ids.front();
            });
        if (node == storage.end() || !node->hasMesh || !node->hasMaterial) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::NodePropertyUnavailable,
                "Rendering properties require a Mesh3D selection");
            break;
        }
        result = Tina::Editor::applyWorld3DMeshNodeProperties(
            document3D_, ids, {.visible = !node->visible});
        successVerb = "toggled";
        propertyGroupName = "Rendering visibility";
        break;
    }
    case EditorCommand::NodeAssignSprite: {
        const Tina::Core::AssetId spriteId = selectedProjectAssetIdOfKind(
            Tina::AssetFormat::AssetKind::Sprite);
        if (!spriteId) {
            result = Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Assign requires a Sprite asset selected in Project Assets");
            break;
        }
        result = Tina::Editor::applyWorld2DSpriteNodeProperties(document_, ids,
                                                      {.spriteId = spriteId});
        successVerb = "assigned";
        propertyGroupName = "Rendering sprite";
        break;
    }
    case EditorCommand::NodeApplySprite: {
        const auto& section = nodePropertySections_[0];
        Tina::Editor::World2DSpriteNodeProperties input{};
        auto sizeX = parseFloatField(section.fields[0], "Size X");
        auto sizeY = parseFloatField(section.fields[1], "Size Y");
        auto pivotX = parseFloatField(section.fields[2], "Pivot X");
        auto pivotY = parseFloatField(section.fields[3], "Pivot Y");
        auto sortingLayer = sizeX && sizeY && pivotX && pivotY
            ? parseIntField(section.fields[4], "Sort Layer", -32768L, 32767L)
            : Tina::Core::Result<std::optional<long>>{std::optional<long>{}};
        auto orderInLayer = sortingLayer
            ? parseIntField(section.fields[5], "Order", -2147483647L, 2147483647L)
            : Tina::Core::Result<std::optional<long>>{std::optional<long>{}};
        if (!sizeX || !sizeY || !pivotX || !pivotY || !sortingLayer ||
            !orderInLayer) {
            const Tina::Core::Error error =
                !sizeX ? sizeX.error() : !sizeY ? sizeY.error()
                : !pivotX ? pivotX.error() : !pivotY ? pivotY.error()
                : !sortingLayer ? sortingLayer.error() : orderInLayer.error();
            if (isRejectable(error)) {
                return reject(error);
            }
            return Tina::Core::failure(error);
        }
        input.sizeX = *sizeX;
        input.sizeY = *sizeY;
        input.pivotX = *pivotX;
        input.pivotY = *pivotY;
        if (sortingLayer->has_value()) {
            input.sortingLayer = static_cast<Tina::Core::i16>(**sortingLayer);
        }
        if (orderInLayer->has_value()) {
            input.orderInLayer = static_cast<Tina::Core::i32>(**orderInLayer);
        }
        result = Tina::Editor::applyWorld2DSpriteNodeProperties(document_, ids, input);
        successVerb = "applied";
        propertyGroupName = "Rendering";
        break;
    }
    case EditorCommand::NodeApplyCamera: {
        const auto& section = nodePropertySections_[1];
        auto height = parseFloatField(section.fields[0], "Height m");
        auto pixelsPerMeter = parseFloatField(section.fields[1], "Ref Px/m");
        auto heightPixels = height && pixelsPerMeter
            ? parseIntField(section.fields[2], "Ref Px H", 1L, 16384L)
            : Tina::Core::Result<std::optional<long>>{std::optional<long>{}};
        if (!height || !pixelsPerMeter || !heightPixels) {
            const Tina::Core::Error error = !height ? height.error()
                : !pixelsPerMeter ? pixelsPerMeter.error()
                                  : heightPixels.error();
            if (isRejectable(error)) {
                return reject(error);
            }
            return Tina::Core::failure(error);
        }
        Tina::Editor::World2DCameraNodeProperties input{};
        input.fixedWorldHeightMeters = *height;
        input.referencePixelsPerMeter = *pixelsPerMeter;
        if (heightPixels->has_value()) {
            input.referenceHeightPixels = static_cast<u32>(**heightPixels);
        }
        result = Tina::Editor::applyWorld2DCameraNodeProperties(document_, ids, input);
        successVerb = "applied";
        propertyGroupName = "Camera";
        break;
    }
    case EditorCommand::NodeApplyPointLight: {
        const auto& section = nodePropertySections_[2];
        Tina::Editor::World2DPointLightNodeProperties input{};
        auto colorText = tree.text(pointLightColorField_.textEdit);
        if (!colorText) {
            return Tina::Core::failure(std::move(colorText.error()));
        }
        if (*colorText != "Mixed" && *colorText != "n/a") {
            auto color = UI::parseColorFieldValue(*colorText);
            if (!color) {
                return reject(Tina::Core::Error{
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    "Color must use #RRGGBBAA hexadecimal format"});
            }
            constexpr float ByteToUnit = 1.0F / 255.0F;
            input.colorRed = static_cast<float>(color->red) * ByteToUnit;
            input.colorGreen = static_cast<float>(color->green) * ByteToUnit;
            input.colorBlue = static_cast<float>(color->blue) * ByteToUnit;
        }
        const std::array<std::pair<UI::UINodeId, std::optional<float>*>, 3>
            fieldBindings{{
                {section.fields[0], &input.intensity},
                {section.fields[1], &input.radiusMeters},
                {section.fields[2], &input.sourceRadiusMeters},
            }};
        const std::array<std::string_view, 3> fieldNames{
            "Intensity", "Radius", "Src Radius"};
        Tina::Core::Status parseStatus = Tina::Core::success();
        for (Tina::Core::usize index = 0; index < fieldBindings.size();
             ++index) {
            auto parsed = parseFloatField(fieldBindings[index].first,
                                          fieldNames[index]);
            if (!parsed) {
                parseStatus = Tina::Core::failure(std::move(parsed.error()));
                break;
            }
            *fieldBindings[index].second = *parsed;
        }
        if (!parseStatus) {
            if (isRejectable(parseStatus.error())) {
                return reject(parseStatus.error());
            }
            return parseStatus;
        }
        result = Tina::Editor::applyWorld2DPointLightNodeProperties(document_, ids, input);
        successVerb = "applied";
        propertyGroupName = "Light";
        break;
    }
    case EditorCommand::NodeApplyShadowOccluder: {
        const auto& section = nodePropertySections_[3];
        auto startX = parseFloatField(section.fields[0], "Start X");
        auto startY = parseFloatField(section.fields[1], "Start Y");
        auto endX = parseFloatField(section.fields[2], "End X");
        auto endY = parseFloatField(section.fields[3], "End Y");
        if (!startX || !startY || !endX || !endY) {
            const Tina::Core::Error error = !startX ? startX.error()
                : !startY ? startY.error()
                : !endX ? endX.error() : endY.error();
            if (isRejectable(error)) {
                return reject(error);
            }
            return Tina::Core::failure(error);
        }
        result = Tina::Editor::applyWorld2DShadowOccluderNodeProperties(
            document_, ids,
            {.localStartX = *startX, .localStartY = *startY,
             .localEndX = *endX, .localEndY = *endY});
        successVerb = "applied";
        propertyGroupName = "Occlusion";
        break;
    }
    default:
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::InvalidArgument,
            "Unknown Inspector node property command");
    }

    if (!result) {
        if (isRejectable(result.error())) {
            return reject(result.error());
        }
        return Tina::Core::failure(std::move(result.error()));
    }
    try {
        authoringFeedback_ = "Node ";
        authoringFeedback_ += propertyGroupName;
        authoringFeedback_ += ' ';
        authoringFeedback_ += successVerb;
        if (result->affectedItemCount == 0U) {
            authoringFeedback_ =
                "Node properties unchanged; no document revision published";
        } else {
            authoringFeedback_ += " on ";
            authoringFeedback_ += std::to_string(result->affectedItemCount);
            authoringFeedback_ +=
                result->affectedItemCount == 1U
                    ? " entity as one document revision"
                    : " entities as one document revision";
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Node property feedback allocation failed");
    }
    if (result->affectedItemCount > 0U) {
        published = true;
        ++counters_.authoringEdits;
        ++counters_.inspectorTransactions;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshNodePropertySectionsUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const u32 primaryId = stableEntityIdForHierarchyItem(selectionKey_);
    const bool selectionEditable = authoringEnabled() && !assetInspectorActive_ &&
                                   primaryId != 0U && !tileMapEditingContext() &&
                                   sceneDocumentActive();
    const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
    const bool nodeContextVisible = !assetInspectorActive_ &&
                                    !tileMapEditingContext() && primaryId != 0U;
    for (NodePropertySectionUi& section : nodePropertySections_) {
        section.rootLayout.visibility = UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                section.collapsible.root, section.rootLayout);
            !status) {
            return status;
        }
    }

    const auto resetSection = [&](const NodePropertySectionUi& section)
        -> Tina::Core::Status {
        if (auto status = tree.setEnabled(section.activeSwitch, false); !status) {
            return status;
        }
        if (auto status = tree.setChecked(section.activeSwitch, false); !status) {
            return status;
        }
        for (Tina::Core::usize index = 0; index < section.fieldCount; ++index) {
            if (auto status = tree.setText(section.fields[index], "n/a"); !status) {
                return status;
            }
            if (auto status = tree.setEnabled(section.fields[index], false); !status) {
                return status;
            }
        }
        if (section.assignButton.hasValue()) {
            if (auto status = tree.setEnabled(section.assignButton, false); !status) {
                return status;
            }
        }
        if (section.applyButton.hasValue()) {
            if (auto status = tree.setEnabled(section.applyButton, false); !status) {
                return status;
            }
        }
        if (&section == &nodePropertySections_[2]) {
            pointLightColorMixed_ = false;
            if (auto status = tree.setText(pointLightColorField_.textEdit, "n/a");
                !status) {
                return status;
            }
            if (auto status = tree.setEnabled(
                    pointLightColorField_.swatchButton, false); !status) {
                return status;
            }
            if (auto status = tree.setEnabled(
                    pointLightColorField_.textEdit, false); !status) {
                return status;
            }
            for (const UI::UINodeId slider : pointLightColorPicker_.sliders()) {
                if (auto status = tree.setEnabled(slider, false); !status) {
                    return status;
                }
            }
        }
        return Tina::Core::success();
    };

    const auto setSectionVisible =
        [&](Tina::Core::usize sectionIndex, bool visible) -> Tina::Core::Status {
        auto& section = nodePropertySections_[sectionIndex];
        section.rootLayout.visibility = visible ? UI::UIVisibility::Visible
                                                : UI::UIVisibility::Collapsed;
        return tree.setLayoutStyle(section.collapsible.root, section.rootLayout);
    };

    if (!nodeContextVisible) {
        for (const NodePropertySectionUi& section : nodePropertySections_) {
            if (auto status = resetSection(section); !status) {
                return status;
            }
        }
        return Tina::Core::success();
    }

    if (world2D) {
        if (auto status = resetSection(
                nodePropertySections_[MeshPropertiesSectionIndex]);
            !status) {
            return status;
        }
        std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
        auto snapshot = document_.parseCurrentSnapshot(storage);
        if (!snapshot) {
            return Tina::Core::failure(std::move(snapshot.error()));
        }
        const auto findEntity = [&](u32 stableId)
            -> const Tina::AssetFormat::World2DEntityDesc* {
            const auto found = std::find_if(
                storage.begin(), storage.end(), [stableId](const auto& entity) {
                    return entity.stableEntityId == stableId;
                });
            return found == storage.end() ? nullptr : &*found;
        };
        const auto* primary = primaryId != 0U ? findEntity(primaryId) : nullptr;
        std::vector<const Tina::AssetFormat::World2DEntityDesc*> selected;
        try {
            if (viewportSelectedEntityCount_ > 0U) {
                selected.reserve(viewportSelectedEntityCount_);
                for (Tina::Core::usize index = 0;
                     index < viewportSelectedEntityCount_; ++index) {
                    const u64 stableId = viewportSelectedEntityIds_[index];
                    if (stableId == 0U ||
                        stableId > (std::numeric_limits<u32>::max)()) {
                        continue;
                    }
                    const auto* entity = findEntity(static_cast<u32>(stableId));
                    if (entity != nullptr) {
                        selected.push_back(entity);
                    }
                }
            } else if (primary != nullptr) {
                selected.push_back(primary);
            }
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Node property section staging allocation failed");
        }
        if (primary == nullptr && !selected.empty()) {
            primary = selected.front();
        }

        if (primary == nullptr || selected.empty()) {
            for (Tina::Core::usize sectionIndex = 0; sectionIndex < 5U;
                 ++sectionIndex) {
                if (auto status = resetSection(nodePropertySections_[sectionIndex]);
                    !status) {
                    return status;
                }
            }
            return Tina::Core::success();
        }
        auto primaryTemplate = Tina::Editor::classifyWorld2DNodeTemplate(*primary);
        if (!primaryTemplate) {
            return Tina::Core::failure(std::move(primaryTemplate.error()));
        }
        bool uniformNodeKinds = true;
        for (const auto* entity : selected) {
            auto nodeTemplate =
                Tina::Editor::classifyWorld2DNodeTemplate(*entity);
            if (!nodeTemplate) {
                return Tina::Core::failure(std::move(nodeTemplate.error()));
            }
            uniformNodeKinds = uniformNodeKinds &&
                               *nodeTemplate == *primaryTemplate;
        }
        const auto groupBelongsToNode =
            [nodeTemplate = *primaryTemplate](
                World2DNodePropertyGroup group) noexcept {
                switch (nodeTemplate) {
                case Tina::Editor::World2DNodeTemplate::Sprite2D:
                    return group == World2DNodePropertyGroup::Rendering;
                case Tina::Editor::World2DNodeTemplate::AnimatedSprite2D:
                    return group == World2DNodePropertyGroup::Rendering ||
                           group == World2DNodePropertyGroup::Animation;
                case Tina::Editor::World2DNodeTemplate::Camera2D:
                    return group == World2DNodePropertyGroup::Camera;
                case Tina::Editor::World2DNodeTemplate::PointLight2D:
                    return group == World2DNodePropertyGroup::Light;
                case Tina::Editor::World2DNodeTemplate::ShadowOccluder2D:
                    return group == World2DNodePropertyGroup::Occlusion;
                case Tina::Editor::World2DNodeTemplate::Node2D:
                case Tina::Editor::World2DNodeTemplate::Marker2D:
                case Tina::Editor::World2DNodeTemplate::TileMap2D:
                case Tina::Editor::World2DNodeTemplate::FxEmitter2D:
                case Tina::Editor::World2DNodeTemplate::StaticBody2D:
                case Tina::Editor::World2DNodeTemplate::RigidBody2D:
                case Tina::Editor::World2DNodeTemplate::CharacterBody2D:
                case Tina::Editor::World2DNodeTemplate::Area2D:
                case Tina::Editor::World2DNodeTemplate::CollisionShape2D:
                case Tina::Editor::World2DNodeTemplate::NavigationRegion2D:
                case Tina::Editor::World2DNodeTemplate::AudioPlayer2D:
                    return false;
                }
                return false;
            };
        for (Tina::Core::usize sectionIndex = 0; sectionIndex < 5U;
             ++sectionIndex) {
            const auto& section = nodePropertySections_[sectionIndex];
            const auto group = static_cast<World2DNodePropertyGroup>(
                sectionIndex);
            const bool sectionVisible = uniformNodeKinds &&
                                        groupBelongsToNode(group);
            if (auto status = setSectionVisible(sectionIndex, sectionVisible);
                !status) {
                return status;
            }
            if (!sectionVisible) {
                if (auto status = resetSection(section); !status) {
                    return status;
                }
                continue;
            }
            if (auto status = tree.setEnabled(section.activeSwitch,
                                              selectionEditable);
                !status) {
                return status;
            }
            const bool fieldsEditable = selectionEditable;
            for (Tina::Core::usize index = 0; index < section.fieldCount;
                 ++index) {
                if (auto status = tree.setEnabled(section.fields[index],
                                                  fieldsEditable);
                    !status) {
                    return status;
                }
            }
            if (section.assignButton.hasValue()) {
                if (auto status = tree.setEnabled(section.assignButton,
                                                  fieldsEditable);
                    !status) {
                    return status;
                }
            }
            if (section.applyButton.hasValue()) {
                if (auto status = tree.setEnabled(section.applyButton,
                                                  fieldsEditable);
                    !status) {
                    return status;
                }
            }
            if (sectionIndex == 2U) {
                if (auto status = tree.setEnabled(
                        pointLightColorField_.swatchButton, fieldsEditable);
                    !status) {
                    return status;
                }
                if (auto status = tree.setEnabled(
                        pointLightColorField_.textEdit, fieldsEditable);
                    !status) {
                    return status;
                }
                for (const UI::UINodeId slider : pointLightColorPicker_.sliders()) {
                    if (auto status = tree.setEnabled(slider, fieldsEditable);
                        !status) {
                        return status;
                    }
                }
            }
            const auto mixedFloat = [&](auto&& accessor) {
                const float primaryValue = accessor(*primary);
                for (const auto* entity : selected) {
                    if (std::abs(accessor(*entity) - primaryValue) > 1.0e-4F) {
                        return true;
                    }
                }
                return false;
            };
            const auto mixedBool = [&](auto&& accessor) {
                const bool primaryValue = accessor(*primary);
                for (const auto* entity : selected) {
                    if (accessor(*entity) != primaryValue) {
                        return true;
                    }
                }
                return false;
            };
            const auto setField = [&](Tina::Core::usize index, bool mixedValue,
                                      std::string_view value) -> Tina::Core::Status {
                return tree.setText(section.fields[index],
                                    mixedValue ? std::string_view{"Mixed"} : value);
            };
            const auto setNumberField = [&](Tina::Core::usize index,
                                            bool mixedValue,
                                            float value) -> Tina::Core::Status {
                if (mixedValue) {
                    return setField(index, true, {});
                }
                auto text = formatEditorNumber(value);
                if (!text) {
                    return Tina::Core::failure(std::move(text.error()));
                }
                return setField(index, false, text->view());
            };

            Tina::Core::Status status = Tina::Core::success();
            switch (group) {
            case World2DNodePropertyGroup::Rendering: {
                const auto& sprite = *primary->sprite;
                status = tree.setChecked(
                    section.activeSwitch,
                    sprite.visible &&
                        !mixedBool([](const auto& e) { return e.sprite->visible; }));
                if (status) {
                    status = setNumberField(
                        0,
                        mixedFloat([](const auto& e) { return e.sprite->sizeX; }),
                        sprite.sizeX);
                }
                if (status) {
                    status = setNumberField(
                        1,
                        mixedFloat([](const auto& e) { return e.sprite->sizeY; }),
                        sprite.sizeY);
                }
                if (status) {
                    status = setNumberField(
                        2,
                        mixedFloat([](const auto& e) { return e.sprite->pivotX; }),
                        sprite.pivotX);
                }
                if (status) {
                    status = setNumberField(
                        3,
                        mixedFloat([](const auto& e) { return e.sprite->pivotY; }),
                        sprite.pivotY);
                }
                if (status) {
                    status = setField(
                        4,
                        mixedFloat([](const auto& e) {
                            return static_cast<float>(e.sprite->sortingLayer);
                        }),
                        std::to_string(sprite.sortingLayer));
                }
                if (status) {
                    status = setField(
                        5,
                        mixedFloat([](const auto& e) {
                            return static_cast<float>(e.sprite->orderInLayer);
                        }),
                        std::to_string(sprite.orderInLayer));
                }
                break;
            }
            case World2DNodePropertyGroup::Camera: {
                const auto& camera = *primary->camera;
                status = tree.setChecked(
                    section.activeSwitch,
                    camera.active &&
                        !mixedBool([](const auto& e) { return e.camera->active; }));
                if (status) {
                    status = setNumberField(
                        0,
                        mixedFloat([](const auto& e) {
                            return e.camera->fixedWorldHeightMeters;
                        }),
                        camera.fixedWorldHeightMeters);
                }
                if (status) {
                    status = setNumberField(
                        1,
                        mixedFloat([](const auto& e) {
                            return e.camera->referencePixelsPerMeter;
                        }),
                        camera.referencePixelsPerMeter);
                }
                if (status) {
                    status = setField(
                        2,
                        mixedFloat([](const auto& e) {
                            return static_cast<float>(
                                e.camera->referenceHeightPixels);
                        }),
                        std::to_string(camera.referenceHeightPixels));
                }
                break;
            }
            case World2DNodePropertyGroup::Light: {
                const auto& light = *primary->pointLight;
                status = tree.setChecked(
                    section.activeSwitch,
                    light.active &&
                        !mixedBool([](const auto& e) {
                            return e.pointLight->active;
                        }));
                pointLightColorMixed_ =
                    mixedFloat([](const auto& e) {
                        return e.pointLight->colorRed;
                    }) ||
                    mixedFloat([](const auto& e) {
                        return e.pointLight->colorGreen;
                    }) ||
                    mixedFloat([](const auto& e) {
                        return e.pointLight->colorBlue;
                    });
                const auto toByte = [](float channel) noexcept {
                    return static_cast<u8>(std::lround(
                        std::clamp(channel, 0.0F, 1.0F) * 255.0F));
                };
                pointLightColorValue_ = UI::rgba8(
                    toByte(light.colorRed), toByte(light.colorGreen),
                    toByte(light.colorBlue));
                const UI::UIColorPickerState colorState =
                    UI::synchronizeColorPickerValue(pointLightColorValue_, false);
                if (status) {
                    status = tree.setText(
                        pointLightColorField_.textEdit,
                        pointLightColorMixed_ ? std::string_view{"Mixed"}
                                              : colorState.text.view());
                }
                for (Tina::Core::usize index = 0;
                     status && index < colorState.channelCount; ++index) {
                    status = tree.setSliderValue(
                        pointLightColorPicker_.channelSliders[index],
                        colorState.channelValues[index]);
                    if (status) {
                        status = tree.setText(
                            pointLightColorPicker_.channelValueLabels[index],
                            colorState.channelTexts[index].view());
                    }
                }
                if (!status) {
                    return status;
                }
                auto productTheme = tree.productTheme();
                if (!productTheme) {
                    return Tina::Core::failure(std::move(productTheme.error()));
                }
                status = tree.setBoxPaint(
                    pointLightColorField_.swatchButton,
                    UI::makePanelBoxPaint(*productTheme, pointLightColorValue_,
                                          UI::UIElevation::Flat));
                const bool colorEditable = fieldsEditable;
                if (status) {
                    status = tree.setEnabled(
                        pointLightColorField_.swatchButton, colorEditable);
                }
                if (status) {
                    status = tree.setEnabled(
                        pointLightColorField_.textEdit, colorEditable);
                }
                for (const UI::UINodeId slider : pointLightColorPicker_.sliders()) {
                    if (status) {
                        status = tree.setEnabled(slider, colorEditable);
                    }
                }
                const std::array<std::pair<float, bool>, 3> values{{
                    {light.intensity, mixedFloat([](const auto& e) {
                         return e.pointLight->intensity;
                     })},
                    {light.radiusMeters, mixedFloat([](const auto& e) {
                         return e.pointLight->radiusMeters;
                     })},
                    {light.sourceRadiusMeters, mixedFloat([](const auto& e) {
                         return e.pointLight->sourceRadiusMeters;
                     })},
                }};
                for (Tina::Core::usize index = 0;
                     status && index < values.size(); ++index) {
                    status = setNumberField(index, values[index].second,
                                            values[index].first);
                }
                break;
            }
            case World2DNodePropertyGroup::Occlusion: {
                const auto& occluder = *primary->shadowOccluder;
                status = tree.setChecked(
                    section.activeSwitch,
                    occluder.active &&
                        !mixedBool([](const auto& e) {
                            return e.shadowOccluder->active;
                        }));
                const std::array<std::pair<float, bool>, 4> values{{
                    {occluder.localStartX, mixedFloat([](const auto& e) {
                         return e.shadowOccluder->localStartX;
                     })},
                    {occluder.localStartY, mixedFloat([](const auto& e) {
                         return e.shadowOccluder->localStartY;
                     })},
                    {occluder.localEndX, mixedFloat([](const auto& e) {
                         return e.shadowOccluder->localEndX;
                     })},
                    {occluder.localEndY, mixedFloat([](const auto& e) {
                         return e.shadowOccluder->localEndY;
                     })},
                }};
                for (Tina::Core::usize index = 0;
                     status && index < values.size(); ++index) {
                    status = setNumberField(index, values[index].second,
                                            values[index].first);
                }
                break;
            }
            case World2DNodePropertyGroup::Animation: {
                const auto& animation = *primary->spriteAnimation;
                status = tree.setChecked(
                    section.activeSwitch,
                    animation.autoPlay &&
                        !mixedBool([](const auto& e) {
                            return e.spriteAnimation->autoPlay;
                        }));
                if (status) {
                    bool clipMixed = false;
                    for (const auto* entity : selected) {
                        if (entity->spriteAnimation->clipId != animation.clipId) {
                            clipMixed = true;
                            break;
                        }
                    }
                    const auto clipText = animation.clipId.canonicalText();
                    status = setField(
                        0, clipMixed,
                        std::string{clipText.data(), clipText.size()});
                }
                if (status) {
                    status = setNumberField(
                        1,
                        mixedFloat([](const auto& e) {
                            return e.spriteAnimation->playbackSpeed;
                        }),
                        animation.playbackSpeed);
                }
                break;
            }
            }
            if (!status) {
                return status;
            }
        }
        return Tina::Core::success();
    }

    for (Tina::Core::usize sectionIndex = 0; sectionIndex < 5U; ++sectionIndex) {
        if (auto status = resetSection(nodePropertySections_[sectionIndex]);
            !status) {
            return status;
        }
    }
    const auto& section = nodePropertySections_[MeshPropertiesSectionIndex];
    std::vector<Tina::AssetFormat::PrefabNodeView> storage;
    auto prefab = document3D_.parseCurrentPrefab(storage);
    if (!prefab) {
        return Tina::Core::failure(std::move(prefab.error()));
    }
    const auto primaryNode = std::find_if(
        storage.begin(), storage.end(), [primaryId](const auto& candidate) {
            return candidate.stableNodeId == primaryId;
        });
    if (primaryId == 0U || primaryNode == storage.end()) {
        if (auto status = resetSection(section); !status) {
            return status;
        }
        return Tina::Core::success();
    }
    auto primaryTemplate =
        Tina::Editor::classifyWorld3DNodeTemplate(*primaryNode);
    if (!primaryTemplate) {
        return Tina::Core::failure(std::move(primaryTemplate.error()));
    }
    bool uniformMeshNodes =
        *primaryTemplate == Tina::Editor::World3DNodeTemplate::Mesh3D;
    bool visibilityMixed = false;
    bool meshMixed = false;
    bool materialMixed = false;
    for (Tina::Core::usize index = 0;
         uniformMeshNodes && index < viewportSelectedEntityCount_; ++index) {
        const u64 selectedId = viewportSelectedEntityIds_[index];
        const auto selectedNode = std::find_if(
            storage.begin(), storage.end(), [&](const auto& candidate) {
                return candidate.stableNodeId == selectedId;
            });
        if (selectedNode == storage.end()) {
            uniformMeshNodes = false;
            continue;
        }
        auto nodeTemplate =
            Tina::Editor::classifyWorld3DNodeTemplate(*selectedNode);
        if (!nodeTemplate) {
            return Tina::Core::failure(std::move(nodeTemplate.error()));
        }
        uniformMeshNodes =
            *nodeTemplate == Tina::Editor::World3DNodeTemplate::Mesh3D;
        if (uniformMeshNodes) {
            visibilityMixed = visibilityMixed ||
                              selectedNode->visible != primaryNode->visible;
            meshMixed = meshMixed || selectedNode->meshId != primaryNode->meshId;
            materialMixed = materialMixed ||
                            selectedNode->materialId != primaryNode->materialId;
        }
    }
    if (auto status = setSectionVisible(MeshPropertiesSectionIndex,
                                        uniformMeshNodes);
        !status) {
        return status;
    }
    if (!uniformMeshNodes) {
        if (auto status = resetSection(section); !status) {
            return status;
        }
        return Tina::Core::success();
    }
    if (auto status = tree.setEnabled(section.activeSwitch,
                                      selectionEditable);
        !status) {
        return status;
    }
    if (auto status = tree.setChecked(
            section.activeSwitch, primaryNode->visible && !visibilityMixed);
        !status) {
        return status;
    }
    for (Tina::Core::usize index = 0; index < section.fieldCount; ++index) {
        if (auto status = tree.setEnabled(section.fields[index], false);
            !status) {
            return status;
        }
    }
    const auto assetIdPrefix = [](Tina::Core::AssetId assetId) -> std::string {
        if (!assetId) {
            return "n/a";
        }
        const auto text = assetId.canonicalText();
        return std::string{text.data(), 8U};
    };
    const std::string meshText = assetIdPrefix(primaryNode->meshId);
    if (auto status = tree.setText(
            section.fields[0],
            meshMixed ? std::string_view{"Mixed"} : std::string_view{meshText});
        !status) {
        return status;
    }
    const std::string materialText = assetIdPrefix(primaryNode->materialId);
    if (auto status = tree.setText(
            section.fields[1], materialMixed ? std::string_view{"Mixed"}
                                             : std::string_view{materialText});
        !status) {
        return status;
    }
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
