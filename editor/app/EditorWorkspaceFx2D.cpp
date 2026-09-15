#include "EditorWorkspaceState.hpp"

#include <tina/core/text/ParseInteger.hpp>
#include <tina/math/Constants.hpp>
#include <tina/runtime/PhaseContexts.hpp>
#include <tina/scene/SceneErrors.hpp>

#include <charconv>
#include <cmath>
#include <limits>
#include <string>

namespace Tina::EditorApp::WorkspaceInternal {
namespace {

template <typename Value>
[[nodiscard]] Tina::Core::Result<EditorNumberText> formatEditorInteger(Value value)
{
    EditorNumberText text{};
    const auto result = std::to_chars(text.bytes.data(),
                                      text.bytes.data() + text.bytes.size(), value);
    if (result.ec != std::errc{}) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::InvalidArgument,
                                   "Fx2D integer formatting failed");
    }
    text.size = static_cast<Tina::Core::usize>(result.ptr - text.bytes.data());
    return text;
}

} // namespace

auto EditorWorkspaceState::fxEditingContext() const noexcept -> bool
{
    const auto* tab = documentTabs_.activeTab();
    return tab != nullptr &&
           tab->key.kind == Tina::Editor::EditorDocumentKind::Fx2D;
}

auto EditorWorkspaceState::applyFx2DPropertyCommand(
    Tina::PrimaryWindowUITreeUpdater& tree, EditorCommand command)
    -> Tina::Core::Status
{
    if (!authoringEnabled() || !fxEditingContext()) {
        authoringFeedback_ = "Fx2D edits require an open FX document";
        return Tina::Core::success();
    }
    if (command == EditorCommand::FxPreviewPlay) {
        fxPreviewPlaying_ = !fxPreviewPlaying_;
        if (fxPreviewPlaying_ && !fxPreview_.has_value()) {
            if (auto status = rebuildFx2DPreview(); !status) {
                return status;
            }
        }
        authoringFeedback_ = fxPreviewPlaying_ ? "Fx2D preview playing"
                                               : "Fx2D preview paused";
        return refreshAuthoringUi(tree);
    }
    if (command == EditorCommand::FxPreviewRestart) {
        fxPreviewPlaying_ = true;
        fxTrailPhase_ = 0.0F;
        if (auto status = rebuildFx2DPreview(); !status) {
            return status;
        }
        authoringFeedback_ = "Fx2D preview restarted";
        return refreshAuthoringUi(tree);
    }
    if (command == EditorCommand::FxPickSprite) {
        if (spriteAssetPickerVisible_) {
            return Tina::Core::success();
        }
        spriteAssetPickerFx2D_ = true;
        auto status = showSpriteAssetPicker(tree);
        if (!status || !spriteAssetPickerVisible_) {
            spriteAssetPickerFx2D_ = false;
        }
        return status;
    }
    if (command == EditorCommand::FxAssignSprite) {
        const Tina::Core::AssetId spriteId = selectedProjectSpriteAssetId();
        if (!spriteId) {
            authoringFeedback_ =
                "Assign requires a Sprite or imported Texture2D selected in Project Assets";
            return Tina::Core::success();
        }
        auto next = fx2DDocument_.value();
        next.spriteAssetId = spriteId;
        const u64 revisionBefore = fx2DDocument_.revision();
        if (auto status = fx2DDocument_.replace(next); !status) {
            authoringFeedback_ = "Fx2D sprite assignment rejected: ";
            authoringFeedback_ += status.error().message;
            ++counters_.inspectorRejectedTransactions;
            return Tina::Core::success();
        }
        if (fx2DDocument_.revision() == revisionBefore) {
            authoringFeedback_ =
                "Fx2D sprite unchanged; no document revision published";
            return refreshAuthoringUi(tree);
        }
        ++counters_.authoringEdits;
        ++counters_.inspectorTransactions;
        fxPreviewRevision_ = 0;
        authoringFeedback_ = "Fx2D sprite assigned as one document revision";
        return refreshAuthoringUi(tree);
    }

    auto next = fx2DDocument_.value();
    const auto rejectIfAuthoring = [&](Tina::Core::Status status) -> Tina::Core::Status {
        if (status.error().code ==
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation) {
            try {
                authoringFeedback_ = "Fx2D edit rejected: ";
                authoringFeedback_ += status.error().message;
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Fx2D rejection feedback allocation failed");
            }
            ++counters_.inspectorRejectedTransactions;
            return refreshAuthoringUi(tree);
        }
        return status;
    };
    const auto& emitter = nodePropertySections_[FxEmitterSectionIndex];
    const auto& particle = nodePropertySections_[FxParticleSectionIndex];
    const auto& trail = nodePropertySections_[FxTrailSectionIndex];
    const auto readFloat = [&](UI::UINodeId field, float& destination,
                               std::string_view name) -> Tina::Core::Status {
        auto text = tree.text(field);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        auto parsed = parseInspectorTransformValue(*text, name);
        if (!parsed) {
            return Tina::Core::failure(std::move(parsed.error()));
        }
        if (*parsed) {
            destination = **parsed;
        }
        return Tina::Core::success();
    };
    const auto readU32 = [&](UI::UINodeId field, Tina::Core::u32& destination,
                             std::string_view name) -> Tina::Core::Status {
        auto text = tree.text(field);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        Tina::Core::u32 value = 0;
        if (!Tina::Core::parseUnsigned(*text, value)) {
            try {
                std::string message{name};
                message += " must be a non-negative integer";
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    std::move(message));
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "Fx2D integer parse message allocation failed");
            }
        }
        destination = value;
        return Tina::Core::success();
    };
    const auto readI32 = [&](UI::UINodeId field, Tina::Core::i32& destination,
                             std::string_view name) -> Tina::Core::Status {
        auto text = tree.text(field);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        Tina::Core::i32 value = 0;
        if (!Tina::Core::parseSigned(*text, value)) {
            try {
                std::string message{name};
                message += " must be an integer";
                return Tina::Core::failure(
                    Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                    std::move(message));
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                           "Fx2D integer parse message allocation failed");
            }
        }
        destination = value;
        return Tina::Core::success();
    };

    if (auto status = readFloat(emitter.fields[0], next.particle.originX, "Origin X");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[1], next.particle.originY, "Origin Y");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[2], next.particle.positionOffsetMinX,
                                "Offset min X");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[3], next.particle.positionOffsetMinY,
                                "Offset min Y");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[4], next.particle.positionOffsetMaxX,
                                "Offset max X");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[5], next.particle.positionOffsetMaxY,
                                "Offset max Y");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[6], next.particle.velocityMinX,
                                "Velocity min X");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[7], next.particle.velocityMinY,
                                "Velocity min Y");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[8], next.particle.velocityMaxX,
                                "Velocity max X");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[9], next.particle.velocityMaxY,
                                "Velocity max Y");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[10], next.particle.lifetimeMinSeconds,
                                "Lifetime min");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(emitter.fields[11], next.particle.lifetimeMaxSeconds,
                                "Lifetime max");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readU32(emitter.fields[12], next.particle.capacity, "Capacity");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readU32(emitter.fields[13], next.particle.count, "Count");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    Tina::Core::u64 seed = next.particle.randomSeed;
    {
        auto text = tree.text(emitter.fields[14]);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        if (!Tina::Core::parseUnsigned(*text, seed)) {
            return rejectIfAuthoring(Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Seed must be a non-negative integer"));
        }
        next.particle.randomSeed = seed;
    }

    if (auto status = readFloat(particle.fields[0], next.particle.startWidthMeters,
                                "Start width");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(particle.fields[1], next.particle.startHeightMeters,
                                "Start height");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(particle.fields[2], next.particle.endWidthMeters,
                                "End width");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(particle.fields[3], next.particle.endHeightMeters,
                                "End height");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    float rotationDegrees = next.particle.rotationRadians / Tina::Math::DegreesToRadians;
    if (auto status = readFloat(particle.fields[4], rotationDegrees, "Rotation");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    next.particle.rotationRadians = rotationDegrees * Tina::Math::DegreesToRadians;
    Tina::Core::i32 sortingLayer = next.particle.sortingLayer;
    if (auto status = readI32(particle.fields[5], sortingLayer, "Sorting layer");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (sortingLayer < static_cast<Tina::Core::i32>(
                           (std::numeric_limits<Tina::Core::i16>::min)()) ||
        sortingLayer > static_cast<Tina::Core::i32>(
                           (std::numeric_limits<Tina::Core::i16>::max)())) {
        return rejectIfAuthoring(Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Sorting layer must fit a signed 16-bit integer"));
    }
    next.particle.sortingLayer = static_cast<Tina::Core::i16>(sortingLayer);
    if (auto status = readI32(particle.fields[6], next.particle.orderInLayer, "Order");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    Tina::Core::u32 blend = next.particle.blendMode == Tina::Core::BlendMode::Additive
                                ? 1U
                                : 0U;
    if (auto status = readU32(particle.fields[7], blend, "Blend"); !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (blend > 1U) {
        return rejectIfAuthoring(Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Blend must be 0 (premultiplied) or 1 (additive)"));
    }
    next.particle.blendMode = blend == 0U ? Tina::Core::BlendMode::PremultipliedAlpha
                                          : Tina::Core::BlendMode::Additive;

    if (auto status = readU32(trail.fields[0], next.trail.segmentCapacity, "Segments");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(trail.fields[1], next.trail.segmentLifetimeSeconds,
                                "Trail lifetime");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(trail.fields[2], next.trail.startWidthMeters,
                                "Trail start width");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(trail.fields[3], next.trail.endWidthMeters,
                                "Trail end width");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(trail.fields[4], next.trail.u0, "UV u0"); !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(trail.fields[5], next.trail.v0, "UV v0"); !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(trail.fields[6], next.trail.u1, "UV u1"); !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (auto status = readFloat(trail.fields[7], next.trail.v1, "UV v1"); !status) {
        return rejectIfAuthoring(std::move(status));
    }
    Tina::Core::i32 trailLayer = next.trail.sortingLayer;
    if (auto status = readI32(trail.fields[8], trailLayer, "Trail sorting layer");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (trailLayer < static_cast<Tina::Core::i32>(
                         (std::numeric_limits<Tina::Core::i16>::min)()) ||
        trailLayer > static_cast<Tina::Core::i32>(
                         (std::numeric_limits<Tina::Core::i16>::max)())) {
        return rejectIfAuthoring(Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Trail sorting layer must fit a signed 16-bit integer"));
    }
    next.trail.sortingLayer = static_cast<Tina::Core::i16>(trailLayer);
    if (auto status = readI32(trail.fields[9], next.trail.orderInLayer, "Trail order");
        !status) {
        return rejectIfAuthoring(std::move(status));
    }
    Tina::Core::u32 trailBlend = next.trail.blendMode == Tina::Core::BlendMode::Additive
                                     ? 1U
                                     : 0U;
    if (auto status = readU32(trail.fields[10], trailBlend, "Trail blend"); !status) {
        return rejectIfAuthoring(std::move(status));
    }
    if (trailBlend > 1U) {
        return rejectIfAuthoring(Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Trail blend must be 0 (premultiplied) or 1 (additive)"));
    }
    next.trail.blendMode = trailBlend == 0U ? Tina::Core::BlendMode::PremultipliedAlpha
                                            : Tina::Core::BlendMode::Additive;

    const u64 revisionBefore = fx2DDocument_.revision();
    if (auto status = fx2DDocument_.replace(next); !status) {
        authoringFeedback_ = "Fx2D edit rejected: ";
        authoringFeedback_ += status.error().message;
        ++counters_.inspectorRejectedTransactions;
        return Tina::Core::success();
    }
    if (fx2DDocument_.revision() == revisionBefore) {
        authoringFeedback_ =
            "Fx2D properties unchanged; no document revision published";
        return refreshAuthoringUi(tree);
    }
    ++counters_.authoringEdits;
    ++counters_.inspectorTransactions;
    fxPreviewRevision_ = 0;
    authoringFeedback_ = "Fx2D properties applied as one document revision";
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::refreshFx2DPropertiesUi(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    const auto setSectionVisible =
        [&](Tina::Core::usize sectionIndex, bool visible) -> Tina::Core::Status {
        auto& section = nodePropertySections_[sectionIndex];
        section.rootLayout.visibility = visible ? UI::UIVisibility::Visible
                                                : UI::UIVisibility::Collapsed;
        return tree.setLayoutStyle(section.collapsible.root, section.rootLayout);
    };
    for (Tina::Core::usize index = 0; index < nodePropertySections_.size(); ++index) {
        const bool fxSection = index >= FxEmitterSectionIndex;
        if (auto status = setSectionVisible(index, fxSection); !status) {
            return status;
        }
    }
    const auto& desc = fx2DDocument_.value();
    const bool editable = authoringEnabled();
    if (auto status = tree.setText(inspectorMode_, "Fx2D"); !status) {
        return status;
    }
    const auto* tab = documentTabs_.activeTab();
    if (auto status = tree.setText(inspectorName_,
                                   tab != nullptr ? std::string_view{tab->title}
                                                  : std::string_view{"Fx2D"});
        !status) {
        return status;
    }
    if (auto status = tree.setText(inspectorKind_, "Effect"); !status) {
        return status;
    }
    const auto writeFloat = [&](UI::UINodeId field, float value) -> Tina::Core::Status {
        if (inspectorFieldIsBeingEdited(field)) {
            return tree.setEnabled(field, editable);
        }
        auto text = formatEditorNumber(value);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        if (auto status = tree.setText(field, text->view()); !status) {
            return status;
        }
        return tree.setEnabled(field, editable);
    };
    const auto writeU64 = [&](UI::UINodeId field, Tina::Core::u64 value) -> Tina::Core::Status {
        if (inspectorFieldIsBeingEdited(field)) {
            return tree.setEnabled(field, editable);
        }
        auto text = formatEditorInteger(value);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        if (auto status = tree.setText(field, text->view()); !status) {
            return status;
        }
        return tree.setEnabled(field, editable);
    };
    const auto writeI32 = [&](UI::UINodeId field, Tina::Core::i32 value) -> Tina::Core::Status {
        if (inspectorFieldIsBeingEdited(field)) {
            return tree.setEnabled(field, editable);
        }
        auto text = formatEditorInteger(value);
        if (!text) {
            return Tina::Core::failure(std::move(text.error()));
        }
        if (auto status = tree.setText(field, text->view()); !status) {
            return status;
        }
        return tree.setEnabled(field, editable);
    };

    const auto& emitter = nodePropertySections_[FxEmitterSectionIndex];
    if (auto status = writeFloat(emitter.fields[0], desc.particle.originX); !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[1], desc.particle.originY); !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[2], desc.particle.positionOffsetMinX);
        !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[3], desc.particle.positionOffsetMinY);
        !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[4], desc.particle.positionOffsetMaxX);
        !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[5], desc.particle.positionOffsetMaxY);
        !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[6], desc.particle.velocityMinX); !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[7], desc.particle.velocityMinY); !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[8], desc.particle.velocityMaxX); !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[9], desc.particle.velocityMaxY); !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[10], desc.particle.lifetimeMinSeconds);
        !status) {
        return status;
    }
    if (auto status = writeFloat(emitter.fields[11], desc.particle.lifetimeMaxSeconds);
        !status) {
        return status;
    }
    if (auto status = writeU64(emitter.fields[12], desc.particle.capacity); !status) {
        return status;
    }
    if (auto status = writeU64(emitter.fields[13], desc.particle.count); !status) {
        return status;
    }
    if (auto status = writeU64(emitter.fields[14], desc.particle.randomSeed); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(emitter.activeSwitch, false); !status) {
        return status;
    }
    std::string resourceText = "Click to choose a Sprite";
    try {
        const auto* resourceAsset =
            projectAssets_.inspectorSnapshot(desc.spriteAssetId);
        if (resourceAsset != nullptr) {
            resourceText = resourceAsset->displayName;
        } else if (desc.spriteAssetId) {
            const auto idText = desc.spriteAssetId.canonicalText();
            resourceText.assign("Missing sprite ");
            resourceText.append(idText.data(), 8U);
        }
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::OutOfMemory,
                                   "Fx2D sprite slot label allocation failed");
    }
    if (auto status = tree.setText(emitter.resourceLabel, resourceText); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(emitter.resourceSlot, editable); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(emitter.resourceAssignButton,
                                      editable && selectedProjectSpriteAssetId());
        !status) {
        return status;
    }

    const auto& particle = nodePropertySections_[FxParticleSectionIndex];
    if (auto status = writeFloat(particle.fields[0], desc.particle.startWidthMeters);
        !status) {
        return status;
    }
    if (auto status = writeFloat(particle.fields[1], desc.particle.startHeightMeters);
        !status) {
        return status;
    }
    if (auto status = writeFloat(particle.fields[2], desc.particle.endWidthMeters);
        !status) {
        return status;
    }
    if (auto status = writeFloat(particle.fields[3], desc.particle.endHeightMeters);
        !status) {
        return status;
    }
    if (auto status = writeFloat(particle.fields[4],
                                 desc.particle.rotationRadians /
                                     Tina::Math::DegreesToRadians);
        !status) {
        return status;
    }
    if (auto status = writeI32(particle.fields[5], desc.particle.sortingLayer);
        !status) {
        return status;
    }
    if (auto status = writeI32(particle.fields[6], desc.particle.orderInLayer);
        !status) {
        return status;
    }
    if (auto status = writeU64(particle.fields[7],
                               desc.particle.blendMode == Tina::Core::BlendMode::Additive
                                   ? 1U
                                   : 0U);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(particle.activeSwitch, false); !status) {
        return status;
    }

    const auto& trail = nodePropertySections_[FxTrailSectionIndex];
    if (auto status = writeU64(trail.fields[0], desc.trail.segmentCapacity); !status) {
        return status;
    }
    if (auto status = writeFloat(trail.fields[1], desc.trail.segmentLifetimeSeconds);
        !status) {
        return status;
    }
    if (auto status = writeFloat(trail.fields[2], desc.trail.startWidthMeters); !status) {
        return status;
    }
    if (auto status = writeFloat(trail.fields[3], desc.trail.endWidthMeters); !status) {
        return status;
    }
    if (auto status = writeFloat(trail.fields[4], desc.trail.u0); !status) {
        return status;
    }
    if (auto status = writeFloat(trail.fields[5], desc.trail.v0); !status) {
        return status;
    }
    if (auto status = writeFloat(trail.fields[6], desc.trail.u1); !status) {
        return status;
    }
    if (auto status = writeFloat(trail.fields[7], desc.trail.v1); !status) {
        return status;
    }
    if (auto status = writeI32(trail.fields[8], desc.trail.sortingLayer); !status) {
        return status;
    }
    if (auto status = writeI32(trail.fields[9], desc.trail.orderInLayer); !status) {
        return status;
    }
    if (auto status = writeU64(trail.fields[10],
                               desc.trail.blendMode == Tina::Core::BlendMode::Additive
                                   ? 1U
                                   : 0U);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(trail.activeSwitch, false); !status) {
        return status;
    }
    if (fxPreviewButtonsRow_.hasValue()) {
        fxPreviewButtonsLayout_.visibility = UI::UIVisibility::Visible;
        if (auto status = tree.setLayoutStyle(fxPreviewButtonsRow_,
                                              fxPreviewButtonsLayout_);
            !status) {
            return status;
        }
    }
    if (auto status = tree.setEnabled(fxPreviewPlayButton_, editable); !status) {
        return status;
    }
    if (auto status = tree.setText(fxPreviewPlayButton_,
                                   fxPreviewPlaying_ ? "Pause preview" : "Play preview");
        !status) {
        return status;
    }
    return tree.setEnabled(fxPreviewRestartButton_, editable);
}

auto EditorWorkspaceState::rebuildFx2DPreview() -> Tina::Core::Status
{
    fxPreview_.reset();
    const auto& desc = fx2DDocument_.value();
    const Tina::Asset::AssetHandle sprite =
        loadedAsset(desc.spriteAssetId, Tina::AssetFormat::AssetKind::Sprite);
    if (!sprite) {
        fxPreviewRevision_ = fx2DDocument_.revision();
        return Tina::Core::success();
    }
    auto instance = Tina::Scene::createFx2DFromAsset(
        desc, sprite, Tina::Math::Vec3{}, assetResources_.memory);
    if (!instance) {
        return Tina::Core::failure(std::move(instance.error()));
    }
    if (auto status = instance->particles.emitBurst(instance->initialBurst); !status) {
        return status;
    }
    const Tina::Math::Vec2 origin{desc.particle.originX, desc.particle.originY};
    if (auto status = instance->trail.appendPoint(origin); !status) {
        return status;
    }
    fxPreview_.emplace(std::move(*instance));
    fxPreviewRevision_ = fx2DDocument_.revision();
    fxTrailPhase_ = 0.0F;
    return Tina::Core::success();
}

auto EditorWorkspaceState::tickFx2DPreview(Tina::Core::Duration delta)
    -> Tina::Core::Status
{
    if (!fxEditingContext() || playSessionActive()) {
        fxPreview_.reset();
        return Tina::Core::success();
    }
    if (fxPreviewRevision_ != fx2DDocument_.revision() || !fxPreview_.has_value()) {
        if (auto status = rebuildFx2DPreview(); !status) {
            return status;
        }
    }
    if (!fxPreviewPlaying_ || !fxPreview_.has_value()) {
        return Tina::Core::success();
    }
    if (auto updated = fxPreview_->particles.update(delta); !updated) {
        return Tina::Core::failure(std::move(updated.error()));
    }
    fxTrailPhase_ += static_cast<float>(delta.count());
    const auto& desc = fx2DDocument_.value();
    const Tina::Math::Vec2 point{
        desc.particle.originX + std::cos(fxTrailPhase_ * 2.4F) * 0.45F,
        desc.particle.originY + std::sin(fxTrailPhase_ * 2.4F) * 0.45F,
    };
    if (auto status = fxPreview_->trail.appendPoint(point); !status) {
        if (status.error().code != Tina::Scene::SceneErrorCode::CapacityExceeded) {
            return status;
        }
    }
    if (auto status = fxPreview_->trail.update(delta); !status) {
        return status;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::extractFx2DPreview(
    Tina::RenderSceneExtractionContext& context) const -> Tina::Core::Status
{
    if (!fxEditingContext() || playSessionActive() || !fxPreview_.has_value()) {
        return Tina::Core::success();
    }
    Tina::Asset::AssetFrameResourceResolver resolver{
        .userData = const_cast<EditorWorkspaceState*>(this),
        .resolve = &EditorWorkspaceState::resolvePreviewSprite,
    };
    auto particles = fxPreview_->particles.extract(
        context.renderSceneWriter(), context.frameResourceSink(), resolver);
    if (!particles) {
        return Tina::Core::failure(std::move(particles.error()));
    }
    if (auto status = fxPreview_->trail.extract(
            context.renderSceneWriter(), context.frameResourceSink(), resolver);
        !status) {
        return status;
    }
    counters_.gpuViewportSprites += fxPreview_->particles.liveCount();
    counters_.gpuViewportSprites += fxPreview_->trail.segmentCount();
    return Tina::Core::success();
}

} // namespace Tina::EditorApp::WorkspaceInternal
