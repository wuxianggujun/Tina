#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::prepareAutomaticViewportGizmo(
    Tina::Editor::EditorTransformGizmoMode mode) noexcept -> bool{
    using Handle = Tina::Editor::EditorTransformGizmoHandle;
    using Mode = Tina::Editor::EditorTransformGizmoMode;
    using Orientation = Tina::Editor::EditorTransformGizmoOrientation;
    const auto& snapshot = viewportTransformGizmo_.snapshot();
    if (!snapshot.framePublished || snapshot.dragging()) {
        return false;
    }
    ViewportToolMode toolMode = ViewportToolMode::Translate;
    if (mode == Mode::Rotate) {
        toolMode = ViewportToolMode::Rotate;
    } else if (mode == Mode::Scale) {
        toolMode = ViewportToolMode::Scale;
    }
    if (snapshot.mode != mode || viewportToolMode_ != toolMode) {
        pendingViewportToolMode_ = toolMode;
        return false;
    }
    if (snapshot.orientation != Orientation::World) {
        pendingGizmoOrientationToggle_ = true;
        return false;
    }
    if (viewportTransformGizmo_.snap().enabled) {
        pendingGizmoSnapToggle_ = true;
        return false;
    }
    Handle desired = workspaceMode_ == WorkspaceMode::World2D
                         ? Handle::PlaneXY
                         : Handle::PlaneXZ;
    if (mode == Mode::Rotate) {
        desired = workspaceMode_ == WorkspaceMode::World2D
                      ? Handle::AxisZ
                      : Handle::AxisY;
    } else if (mode == Mode::Scale) {
        desired = Handle::Uniform;
    }
    const auto geometry = std::find_if(
        snapshot.handles().begin(), snapshot.handles().end(),
        [desired](const auto& candidate) {
            return candidate.handle == desired && candidate.pointCount != 0U;
        });
    if (geometry == snapshot.handles().end()) {
        return false;
    }
    autoGizmoCenter_ = {};
    if (geometry->shape ==
        Tina::Editor::EditorTransformGizmoHandleShape::Ring) {
        autoGizmoCenter_ = {
            .x = geometry->points[0].x,
            .y = geometry->points[0].y,
        };
        autoGizmoStart_ = {
            .x = autoGizmoCenter_.x + geometry->radiusPixels,
            .y = autoGizmoCenter_.y,
        };
    } else {
        for (u32 index = 0; index < geometry->pointCount; ++index) {
            autoGizmoCenter_.x += geometry->points[index].x;
            autoGizmoCenter_.y += geometry->points[index].y;
        }
        autoGizmoCenter_.x /= static_cast<float>(geometry->pointCount);
        autoGizmoCenter_.y /= static_cast<float>(geometry->pointCount);
        autoGizmoStart_ = autoGizmoCenter_;
    }
    return beginViewportGizmo(Tina::Platform::PrimaryPointerId,
                              autoGizmoStart_);
}

auto EditorWorkspaceState::automaticViewportGizmoPoint(float fraction) const noexcept -> std::optional<UI::UILogicalPoint>{
    if (!std::isfinite(fraction) || !viewportGizmo_.captured) {
        return std::nullopt;
    }
    const auto mode = viewportTransformGizmo_.snapshot().mode;
    if (mode == Tina::Editor::EditorTransformGizmoMode::Rotate) {
        const float radius = std::hypot(
            autoGizmoStart_.x - autoGizmoCenter_.x,
            autoGizmoStart_.y - autoGizmoCenter_.y);
        const float radians =
            -std::numbers::pi_v<float> * 0.5F * fraction;
        return UI::UILogicalPoint{
            .x = autoGizmoCenter_.x + std::cos(radians) * radius,
            .y = autoGizmoCenter_.y + std::sin(radians) * radius,
        };
    }
    if (mode == Tina::Editor::EditorTransformGizmoMode::Scale) {
        constexpr float AutomaticScaleDragPixels = 32.0F;
        return UI::UILogicalPoint{
            .x = autoGizmoStart_.x + AutomaticScaleDragPixels * fraction,
            .y = autoGizmoStart_.y - AutomaticScaleDragPixels * fraction,
        };
    }
    if (viewportGizmo_.targetCount == 0U) {
        return std::nullopt;
    }
    // The automatic drag spans multiple frames. Keep its pointer path anchored
    // to the frozen transaction baseline while preview publication mutates World.
    const Tina::Math::Vec3 originWorld =
        viewportGizmo_.selectionCount > 1U ? viewportGizmo_.pivot
                                           : viewportGizmo_.targets[0].baselineWorld.position;
    const ViewportProjectedPoint origin =
        projectViewportWorldPoint(originWorld);
    const ViewportProjectedPoint xAxis = projectViewportWorldPoint(
        originWorld + Tina::Math::Vec3{1.0F, 0.0F, 0.0F});
    const Tina::Math::Vec3 secondDirection =
        workspaceMode_ == WorkspaceMode::World2D
            ? Tina::Math::Vec3{0.0F, 1.0F, 0.0F}
            : Tina::Math::Vec3{0.0F, 0.0F, 1.0F};
    const ViewportProjectedPoint secondAxis =
        projectViewportWorldPoint(originWorld + secondDirection);
    if (!origin.projectable || !xAxis.projectable ||
        !secondAxis.projectable) {
        return std::nullopt;
    }
    const float secondAmount =
        workspaceMode_ == WorkspaceMode::World2D ? -1.0F : 1.0F;
    return UI::UILogicalPoint{
        .x = autoGizmoStart_.x +
             ((xAxis.screen.x - origin.screen.x) * 2.0F +
              (secondAxis.screen.x - origin.screen.x) * secondAmount) *
                 fraction,
        .y = autoGizmoStart_.y +
             ((xAxis.screen.y - origin.screen.y) * 2.0F +
              (secondAxis.screen.y - origin.screen.y) * secondAmount) *
                 fraction,
    };
}

auto EditorWorkspaceState::prepareAutomaticViewportMarquee(
    Tina::Editor::EditorMarqueeSelectionMode mode) noexcept -> bool{
    if (viewportGizmo_.captured || viewportNavigationDrag_.captured ||
        viewportMarquee_.captured) {
        return false;
    }
    if (viewportToolMode_ != ViewportToolMode::Select) {
        pendingViewportToolMode_ = ViewportToolMode::Select;
        return false;
    }
    if (marqueeSelectionMode_ != mode) {
        pendingMarqueeSelectionMode_ = mode;
        return false;
    }

    std::array<Tina::Editor::EditorMarqueeCandidate,
               ViewportMarqueeCandidateCapacity>
        candidates{};
    const Tina::Core::usize candidateCount =
        collectViewportMarqueeCandidates(candidates);
    if (candidateCount < 2U) {
        return false;
    }
    const auto currentSelection = std::span{
        viewportSelectedEntityIds_.data(), viewportSelectedEntityCount_};
    constexpr std::array ProbeFactors{
        UI::UILogicalPoint{.x = 0.5F, .y = 0.5F},
        UI::UILogicalPoint{.x = 0.2F, .y = 0.2F},
        UI::UILogicalPoint{.x = 0.8F, .y = 0.2F},
        UI::UILogicalPoint{.x = 0.2F, .y = 0.8F},
        UI::UILogicalPoint{.x = 0.8F, .y = 0.8F},
    };
    constexpr float ProbeHalfExtent = 0.5F;
    for (Tina::Core::usize candidateIndex = 0;
         candidateIndex < candidateCount; ++candidateIndex) {
        const auto& bounds = candidates[candidateIndex].screenBounds;
        const float left = (std::min)(bounds.x0, bounds.x1);
        const float right = (std::max)(bounds.x0, bounds.x1);
        const float top = (std::min)(bounds.y0, bounds.y1);
        const float bottom = (std::max)(bounds.y0, bounds.y1);
        for (const UI::UILogicalPoint factor : ProbeFactors) {
            const UI::UILogicalPoint point{
                .x = left + (right - left) * factor.x,
                .y = top + (bottom - top) * factor.y,
            };
            const Tina::Editor::EditorMarqueeScreenRect rect{
                .x0 = point.x - ProbeHalfExtent,
                .y0 = point.y - ProbeHalfExtent,
                .x1 = point.x + ProbeHalfExtent,
                .y1 = point.y + ProbeHalfExtent,
            };
            auto evaluated = Tina::Editor::EditorMarqueeSelection::Evaluate(
                rect, std::span{candidates.data(), candidateCount},
                currentSelection, mode);
            if (!evaluated) {
                continue;
            }
            bool suitable = false;
            const Tina::Core::usize transformTargetCount =
                viewportTransformTargetCount(evaluated->selection());
            switch (mode) {
            case Tina::Editor::EditorMarqueeSelectionMode::Replace:
                suitable = evaluated->changed() &&
                           evaluated->selection().size() == 1U &&
                           transformTargetCount == 1U;
                break;
            case Tina::Editor::EditorMarqueeSelectionMode::Add:
                suitable = !evaluated->added().empty() &&
                           evaluated->selection().size() >
                               currentSelection.size() &&
                           transformTargetCount >= 2U;
                break;
            case Tina::Editor::EditorMarqueeSelectionMode::Toggle:
                suitable = !evaluated->removed().empty() &&
                           transformTargetCount == 1U;
                break;
            }
            if (!suitable ||
                !beginViewportMarquee(
                    Tina::Platform::PrimaryPointerId,
                    {.x = rect.x0, .y = rect.y0})) {
                continue;
            }
            if (!updateViewportMarquee(
                    Tina::Platform::PrimaryPointerId,
                    {.x = rect.x1, .y = rect.y1})) {
                viewportMarquee_ = {};
                return false;
            }
            viewportMarquee_.commitRequested = true;
            return true;
        }
    }
    return false;
}

auto EditorWorkspaceState::beginViewportGizmo(Tina::Platform::PointerId pointer,
                                      UI::UILogicalPoint position) noexcept -> bool{
    if (!authoringEnabled() || pointer != Tina::Platform::PrimaryPointerId ||
        (viewportToolMode_ != ViewportToolMode::Translate &&
         viewportToolMode_ != ViewportToolMode::Rotate &&
         viewportToolMode_ != ViewportToolMode::Scale) || viewportGizmo_.captured ||
        viewportNavigationDrag_.captured || viewportMarquee_.captured ||
        tileStroke_.captured || tileMapEditingContext() ||
        !std::isfinite(viewportLogicalRect_.width) ||
        !std::isfinite(viewportLogicalRect_.height) ||
        viewportLogicalRect_.width <= 0.0F || viewportLogicalRect_.height <= 0.0F) {
        return false;
    }
    const u32 stableEntityId = stableEntityIdForHierarchyItem(selectionKey_);
    if (stableEntityId == 0U) {
        return false;
    }
    ViewportTransformTransaction transaction{
        .workspace = workspaceMode_,
        .pointer = pointer,
        .stableEntityId = stableEntityId,
        .baselineRevision = activeDocumentRevision(),
        .selectionRevision = viewportSelectionRevision_,
        .captured = true,
    };
    if (auto status = captureViewportTransformTargets(transaction); !status) {
        return false;
    }
    const auto operation = viewportTransformGizmo_.beginDrag(
        ViewportPrimaryPointerToken, {.x = position.x, .y = position.y});
    if (operation != Tina::Editor::EditorTransformGizmoOperation::Success) {
        return false;
    }
    transaction.processedGizmoRevision = viewportTransformGizmo_.snapshot().revision;
    viewportGizmo_ = std::move(transaction);
    return true;
}

auto EditorWorkspaceState::updateViewportGizmo(Tina::Platform::PointerId pointer,
                                       UI::UILogicalPoint position) noexcept -> bool{
    if (!viewportGizmo_.captured || pointer != viewportGizmo_.pointer ||
        viewportGizmo_.commitRequested || viewportGizmo_.cancelRequested) {
        return false;
    }
    return viewportTransformGizmo_.updateDrag(
               ViewportPrimaryPointerToken,
               {.x = position.x, .y = position.y}) ==
           Tina::Editor::EditorTransformGizmoOperation::Success;
}

auto EditorWorkspaceState::requestViewportGizmoCommit(Tina::Platform::PointerId pointer,
                                              UI::UILogicalPoint position) noexcept -> bool{
    if (!viewportGizmo_.captured || pointer != viewportGizmo_.pointer ||
        viewportGizmo_.commitRequested || viewportGizmo_.cancelRequested ||
        viewportTransformGizmo_.endDrag(
            ViewportPrimaryPointerToken,
            {.x = position.x, .y = position.y}) !=
            Tina::Editor::EditorTransformGizmoOperation::Success) {
        return false;
    }
    viewportGizmo_.commitRequested = true;
    return true;
}

auto EditorWorkspaceState::beginViewportMarquee(
    Tina::Platform::PointerId pointer,
    UI::UILogicalPoint position) noexcept -> bool{
    if (viewportToolMode_ != ViewportToolMode::Select ||
        viewportMarquee_.captured || viewportGizmo_.captured ||
        viewportNavigationDrag_.captured || tileStroke_.captured) {
        return false;
    }
    viewportMarquee_ = {
        .pointer = pointer,
        .start = position,
        .current = position,
        .mode = marqueeSelectionMode_,
        .captured = true,
    };
    return true;
}

auto EditorWorkspaceState::updateViewportMarquee(
    Tina::Platform::PointerId pointer,
    UI::UILogicalPoint position) noexcept -> bool{
    if (!viewportMarquee_.captured || pointer != viewportMarquee_.pointer ||
        viewportMarquee_.commitRequested || viewportMarquee_.cancelRequested) {
        return false;
    }
    viewportMarquee_.current = position;
    return true;
}

auto EditorWorkspaceState::viewportTransformTargetCount(
    std::span<const u64> selection) const noexcept -> Tina::Core::usize{
    if (!previewWorld_.has_value()) {
        return 0U;
    }
    Tina::Core::usize count = 0;
    for (const u64 stableId : selection) {
        if (stableId == 0U ||
            stableId > (std::numeric_limits<u32>::max)()) {
            continue;
        }
        const Tina::Scene::EntityId entity =
            findPreviewEntity(static_cast<u32>(stableId));
        if (!entity.hasValue() ||
            previewEntityHasAncestorInSelection(entity, selection)) {
            continue;
        }
        const Tina::Scene::LocalTransform* local =
            previewWorld_->localTransform(entity);
        const Tina::Scene::WorldTransform* world =
            previewWorld_->worldTransform(entity);
        if (local != nullptr && world != nullptr &&
            Tina::Scene::isValid(*local) && Tina::Scene::isValid(*world)) {
            ++count;
        }
    }
    return count;
}

auto EditorWorkspaceState::viewportSelectionPivot() const noexcept -> Tina::Math::Vec3{
    if (!previewWorld_.has_value()) {
        return {};
    }
    Tina::Math::Vec3 sum{};
    Tina::Core::usize count = 0;
    const auto append = [&](u64 stableId) {
        const Tina::Scene::EntityId entity = findPreviewEntity(
            static_cast<u32>(stableId));
        if (!entity.hasValue() || previewEntityHasSelectedAncestor(entity)) {
            return;
        }
        const Tina::Scene::WorldTransform* transform =
            previewWorld_->worldTransform(entity);
        if (transform == nullptr || !Tina::Math::isFinite(transform->position)) {
            return;
        }
        sum = sum + transform->position;
        ++count;
    };
    for (Tina::Core::usize index = 0;
         index < viewportSelectedEntityCount_; ++index) {
        append(viewportSelectedEntityIds_[index]);
    }
    if (count == 0U) {
        const u32 primary = stableEntityIdForHierarchyItem(selectionKey_);
        append(primary);
    }
    if (count == 0U) {
        return {};
    }
    return sum * (1.0F / static_cast<float>(count));
}

auto EditorWorkspaceState::captureViewportTransformTargets(
    ViewportTransformTransaction& transaction) -> Tina::Core::Status{
    if (!previewWorld_.has_value()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Viewport transform requires a live preview world");
    }
    const u32 primaryStableId = stableEntityIdForHierarchyItem(selectionKey_);
    if (primaryStableId == 0U) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::EntityNotFound,
            "Viewport transform requires a scene selection");
    }

    std::array<u64, ViewportTransformTargetCapacity> orderedIds{};
    Tina::Core::usize orderedCount = 0;
    const auto appendId = [&](u64 stableId) {
        if (stableId == 0U || orderedCount == orderedIds.size() ||
            std::find(orderedIds.begin(), orderedIds.begin() +
                          static_cast<std::ptrdiff_t>(orderedCount),
                      stableId) !=
                orderedIds.begin() +
                    static_cast<std::ptrdiff_t>(orderedCount)) {
            return;
        }
        orderedIds[orderedCount++] = stableId;
    };
    appendId(primaryStableId);
    for (Tina::Core::usize index = 0;
         index < viewportSelectedEntityCount_; ++index) {
        appendId(viewportSelectedEntityIds_[index]);
    }

    transaction.targetCount = 0;
    transaction.selectionCount = viewportSelectedEntityCount_ != 0U
                                     ? viewportSelectedEntityCount_
                                     : Tina::Core::usize{1};
    Tina::Math::Vec3 pivotSum{};
    for (Tina::Core::usize index = 0; index < orderedCount; ++index) {
        const u32 stableId = static_cast<u32>(orderedIds[index]);
        const Tina::Scene::EntityId entity = findPreviewEntity(stableId);
        if (!entity.hasValue() || previewEntityHasSelectedAncestor(entity)) {
            continue;
        }
        const Tina::Scene::LocalTransform* local =
            previewWorld_->localTransform(entity);
        const Tina::Scene::WorldTransform* world =
            previewWorld_->worldTransform(entity);
        if (local == nullptr || world == nullptr ||
            !Tina::Scene::isValid(*local) ||
            !Tina::Scene::isValid(*world)) {
            continue;
        }
        if (transaction.targetCount == transaction.targets.size()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                "Viewport transform selection exceeds its fixed capacity");
        }
        auto& target = transaction.targets[transaction.targetCount++];
        target = {
            .stableEntityId = stableId,
            .entity = entity,
            .baselineLocal = *local,
            .baselineWorld = *world,
            .previewLocal = *local,
        };
        pivotSum = pivotSum + world->position;
    }
    if (transaction.targetCount == 0U) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::EntityNotFound,
            "Viewport transform selection has no transformable scene item");
    }
    transaction.pivot = pivotSum *
                        (1.0F / static_cast<float>(transaction.targetCount));
    transaction.stableEntityId = primaryStableId;
    transaction.selectionRevision = viewportSelectionRevision_;
    transaction.baselineTransform = transaction.targets[0].baselineLocal;
    transaction.previewTransform = transaction.targets[0].previewLocal;
    return Tina::Core::success();
}

auto EditorWorkspaceState::applyViewportWorldTransformDelta(
    const Tina::Scene::WorldTransform& baseline,
    Tina::Math::Vec3 pivot,
    const Tina::Editor::EditorTransformGizmoDelta& delta,
    Tina::Math::Quaternion localBasis) noexcept -> Tina::Core::Result<Tina::Scene::WorldTransform>{
    const Tina::Math::Vec3 translation{
        delta.translation.x, delta.translation.y, delta.translation.z};
    const Tina::Math::Vec3 rotationAxis{
        delta.rotationAxis.x, delta.rotationAxis.y, delta.rotationAxis.z};
    const Tina::Math::Vec3 scaleFactors{
        delta.scaleFactors.x, delta.scaleFactors.y, delta.scaleFactors.z};
    if (!Tina::Scene::isValid(baseline) ||
        !Tina::Math::isFinite(pivot) ||
        !Tina::Math::isFinite(translation) ||
        !Tina::Math::isFinite(rotationAxis) ||
        !std::isfinite(delta.rotationDegrees) ||
        !Tina::Math::isFinite(scaleFactors) ||
        scaleFactors.x <= 0.0F || scaleFactors.y <= 0.0F ||
        scaleFactors.z <= 0.0F) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Viewport group transform contains a non-finite value");
    }
    Tina::Scene::WorldTransform result = baseline;
    Tina::Math::Vec3 relative = result.position - pivot;
    if (std::abs(delta.rotationDegrees) > 1.0e-6F) {
        const float axisLengthSquared =
            Tina::Math::dot(rotationAxis, rotationAxis);
        if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= 1.0e-12F) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport group rotation has an invalid axis");
        }
        const Tina::Math::Vec3 axis =
            rotationAxis * (1.0F / std::sqrt(axisLengthSquared));
        const float halfRadians =
            delta.rotationDegrees * DegreesToRadians * 0.5F;
        const float sine = std::sin(halfRadians);
        const Tina::Math::Quaternion rotationDelta{
            .x = axis.x * sine,
            .y = axis.y * sine,
            .z = axis.z * sine,
            .w = std::cos(halfRadians),
        };
        relative = Tina::Math::rotate(rotationDelta, relative);
        result.rotation =
            Tina::Math::normalized(rotationDelta * result.rotation);
    }
    if (scaleFactors != Tina::Math::Vec3{1.0F, 1.0F, 1.0F}) {
        if (delta.orientation ==
            Tina::Editor::EditorTransformGizmoOrientation::Local) {
            const Tina::Math::Quaternion basis =
                Tina::Math::normalized(localBasis);
            relative = Tina::Math::rotate(
                basis,
                Tina::Math::rotate(Tina::Math::conjugate(basis),
                                    relative) * scaleFactors);
        } else {
            relative = relative * scaleFactors;
        }
        result.scale = result.scale * scaleFactors;
    }
    result.position = pivot + relative + translation;
    if (!Tina::Scene::isValid(result)) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Viewport group transform produced an invalid world transform");
    }
    return result;
}

auto EditorWorkspaceState::viewportTransformNearlyEqual(float left,
                                                       float right) noexcept -> bool{
    const float scale = (std::max)({1.0F, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= 2.0e-4F * scale;
}

auto EditorWorkspaceState::viewportWorldTransformsEquivalent(
    Tina::Scene::WorldTransform left,
    Tina::Scene::WorldTransform right) noexcept -> bool{
    const bool sameRotation =
        viewportTransformNearlyEqual(left.rotation.x, right.rotation.x) &&
        viewportTransformNearlyEqual(left.rotation.y, right.rotation.y) &&
        viewportTransformNearlyEqual(left.rotation.z, right.rotation.z) &&
        viewportTransformNearlyEqual(left.rotation.w, right.rotation.w);
    const bool oppositeRotation =
        viewportTransformNearlyEqual(left.rotation.x, -right.rotation.x) &&
        viewportTransformNearlyEqual(left.rotation.y, -right.rotation.y) &&
        viewportTransformNearlyEqual(left.rotation.z, -right.rotation.z) &&
        viewportTransformNearlyEqual(left.rotation.w, -right.rotation.w);
    return viewportTransformNearlyEqual(left.position.x, right.position.x) &&
           viewportTransformNearlyEqual(left.position.y, right.position.y) &&
           viewportTransformNearlyEqual(left.position.z, right.position.z) &&
           (sameRotation || oppositeRotation) &&
           viewportTransformNearlyEqual(left.scale.x, right.scale.x) &&
           viewportTransformNearlyEqual(left.scale.y, right.scale.y) &&
           viewportTransformNearlyEqual(left.scale.z, right.scale.z);
}

auto EditorWorkspaceState::localTransformFromWorld(Tina::Scene::EntityId entity,
                        Tina::Scene::WorldTransform world) const -> Tina::Core::Result<Tina::Scene::LocalTransform>{
    if (!previewWorld_.has_value() || !Tina::Scene::isValid(world)) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Viewport transform cannot publish an invalid world transform");
    }
    Tina::Scene::LocalTransform local{
        .position = world.position,
        .rotation = Tina::Math::normalized(world.rotation),
        .scale = world.scale,
    };
    const Tina::Scene::EntityId parent = previewWorld_->parent(entity);
    if (parent.hasValue()) {
        const Tina::Scene::WorldTransform* parentWorld =
            previewWorld_->worldTransform(parent);
        if (parentWorld == nullptr || !Tina::Scene::isValid(*parentWorld)) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport transform cannot resolve the target parent");
        }
        constexpr float MinimumParentScale = 1.0e-6F;
        if (std::abs(parentWorld->scale.x) <= MinimumParentScale ||
            std::abs(parentWorld->scale.y) <= MinimumParentScale ||
            std::abs(parentWorld->scale.z) <= MinimumParentScale) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport transform cannot edit below a zero-scale parent");
        }
        const Tina::Math::Quaternion inverseRotation =
            Tina::Math::conjugate(
                Tina::Math::normalized(parentWorld->rotation));
        const Tina::Math::Vec3 relative = world.position - parentWorld->position;
        const Tina::Math::Vec3 parentLocal =
            Tina::Math::rotate(inverseRotation, relative);
        local.position = {
            .x = parentLocal.x / parentWorld->scale.x,
            .y = parentLocal.y / parentWorld->scale.y,
            .z = parentLocal.z / parentWorld->scale.z,
        };
        local.rotation =
            Tina::Math::normalized(inverseRotation * world.rotation);
        local.scale = {
            .x = world.scale.x / parentWorld->scale.x,
            .y = world.scale.y / parentWorld->scale.y,
            .z = world.scale.z / parentWorld->scale.z,
        };
    }
    if (!Tina::Scene::isValid(local)) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Viewport transform produced an invalid local transform");
    }
    if (parent.hasValue()) {
        const Tina::Scene::WorldTransform* parentWorld =
            previewWorld_->worldTransform(parent);
        if (parentWorld == nullptr ||
            !Tina::Scene::supportsTrsComposition(*parentWorld, local)) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport group transform would require shear below a non-uniformly scaled parent");
        }
        Tina::Scene::WorldTransform recomposed{};
        if (!Tina::Scene::tryCompose(*parentWorld, local, recomposed) ||
            !viewportWorldTransformsEquivalent(recomposed, world)) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
                "Viewport group transform cannot preserve the requested world TRS below its parent");
        }
    }
    return local;
}

auto EditorWorkspaceState::viewportGizmoContextMatches() const noexcept -> bool{
    return viewportGizmo_.workspace == workspaceMode_ &&
           viewportGizmo_.stableEntityId == stableEntityIdForHierarchyItem(selectionKey_) &&
           viewportGizmo_.selectionRevision == viewportSelectionRevision_ &&
           viewportGizmo_.baselineRevision == activeDocumentRevision() &&
           viewportGizmo_.baselineRevision == previewRevision_ &&
           previewWorld_.has_value();
}

auto EditorWorkspaceState::viewportTransformDeltaIsIdentity(
    const Tina::Editor::EditorTransformGizmoDelta& delta) noexcept -> bool{
    constexpr float Epsilon = 1.0e-6F;
    return std::abs(delta.translation.x) <= Epsilon &&
           std::abs(delta.translation.y) <= Epsilon &&
           std::abs(delta.translation.z) <= Epsilon &&
           std::abs(delta.rotationDegrees) <= Epsilon &&
           std::abs(delta.scaleFactors.x - 1.0F) <= Epsilon &&
           std::abs(delta.scaleFactors.y - 1.0F) <= Epsilon &&
           std::abs(delta.scaleFactors.z - 1.0F) <= Epsilon;
}

auto EditorWorkspaceState::finishViewportGizmoWithoutCommit(Tina::PrimaryWindowUITreeUpdater& tree, bool rejected,
                                 std::string_view feedback) -> Tina::Core::Status{
    const bool group = viewportGizmo_.selectionCount > 1U;
    if (viewportTransformGizmo_.snapshot().dragging()) {
        (void)viewportTransformGizmo_.cancelDrag(ViewportPrimaryPointerToken);
    }
    const bool restorePreview = viewportGizmo_.previewPublished;
    viewportGizmo_ = {};
    if (restorePreview) {
        if (auto status = validateRuntimePreview(); !status) {
            return status;
        }
    }
    if (rejected) {
        ++counters_.viewportGizmoRejects;
    } else {
        ++counters_.viewportGizmoCancels;
    }
    try {
        authoringFeedback_ = group ? "Group transform | "
                                  : "Viewport transform | ";
        authoringFeedback_.append(feedback);
    } catch (const std::bad_alloc&) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::OutOfMemory,
            "Viewport transform feedback allocation failed");
    }
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::commitViewportGizmoTransform(const ViewportTransformTransaction& transaction) -> Tina::Core::Status{
    const std::span<const ViewportTransformTransaction::Target> targets{
        transaction.targets.data(), transaction.targetCount};
    if (transaction.workspace == WorkspaceMode::World3D) {
        std::vector<Tina::AssetFormat::PrefabNodeView> views;
        auto prefab = document3D_.parseCurrentPrefab(views);
        if (!prefab) {
            return Tina::Core::failure(std::move(prefab.error()));
        }
        std::vector<Tina::AssetFormat::PrefabNodeDesc> edited;
        try {
            edited.reserve(views.size());
            for (const auto& node : views) {
                Tina::AssetFormat::PrefabNodeDesc candidate =
                    Tina::AssetFormat::prefabNodeDescFromView(node);
                const auto target = std::find_if(
                    targets.begin(), targets.end(),
                    [&node](const auto& selected) {
                        return selected.stableEntityId == node.stableNodeId;
                    });
                if (target != targets.end()) {
                    candidate.positionX = target->previewLocal.position.x;
                    candidate.positionY = target->previewLocal.position.y;
                    candidate.positionZ = target->previewLocal.position.z;
                    candidate.rotationX = target->previewLocal.rotation.x;
                    candidate.rotationY = target->previewLocal.rotation.y;
                    candidate.rotationZ = target->previewLocal.rotation.z;
                    candidate.rotationW = target->previewLocal.rotation.w;
                    candidate.scaleX = target->previewLocal.scale.x;
                    candidate.scaleY = target->previewLocal.scale.y;
                    candidate.scaleZ = target->previewLocal.scale.z;
                }
                edited.push_back(candidate);
            }
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Viewport group transform document staging allocation failed");
        }
        return document3D_.replace({.nodes = std::span<const Tina::AssetFormat::PrefabNodeDesc>{edited}});
    }

    std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document_.parseCurrentSnapshot(storage);
    if (!snapshot) {
        return Tina::Core::failure(std::move(snapshot.error()));
    }
    for (auto& entity : storage) {
        const auto target = std::find_if(
            targets.begin(), targets.end(),
            [&entity](const auto& selected) {
                return selected.stableEntityId == entity.stableEntityId;
            });
        if (target == targets.end()) {
            continue;
        }
        entity.positionX = target->previewLocal.position.x;
        entity.positionY = target->previewLocal.position.y;
        entity.positionZ = target->previewLocal.position.z;
        entity.rotationX = target->previewLocal.rotation.x;
        entity.rotationY = target->previewLocal.rotation.y;
        entity.rotationZ = target->previewLocal.rotation.z;
        entity.rotationW = target->previewLocal.rotation.w;
        entity.scaleX = target->previewLocal.scale.x;
        entity.scaleY = target->previewLocal.scale.y;
        entity.scaleZ = target->previewLocal.scale.z;
    }
    return document_.replace({
        .entities = std::span<const Tina::AssetFormat::World2DEntityDesc>{storage},
        .gameplaySchema = snapshot->gameplaySchema,
        .gameplayVersion = snapshot->gameplayVersion,
        .gameplayBytes = snapshot->gameplayBytes,
    });
}

auto EditorWorkspaceState::processViewportGizmo(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (!viewportGizmo_.captured) {
        return Tina::Core::success();
    }
    if (viewportGizmo_.cancelRequested) {
        return finishViewportGizmoWithoutCommit(
            tree, false, "cancelled; document unchanged");
    }
    if (!viewportGizmoContextMatches() || pendingEditorCommand_.has_value()) {
        return finishViewportGizmoWithoutCommit(
            tree, true,
            "rejected after selection, workspace, or document revision changed");
    }

    if (viewportGizmo_.targetCount == 0U) {
        return finishViewportGizmoWithoutCommit(
            tree, true, "rejected because preview targets are unavailable");
    }
    if (!viewportGizmo_.baselineReady) {
        viewportGizmo_.baselineReady = true;
        ++counters_.viewportGizmoBegins;
        if (viewportGizmo_.selectionCount > 1U) {
            try {
                authoringFeedback_ = "Group transform started | ";
                authoringFeedback_ +=
                    std::to_string(viewportGizmo_.selectionCount);
                authoringFeedback_ += " selected | Group pivot";
            } catch (const std::bad_alloc&) {
                return Tina::Core::failure(
                    Tina::Core::CoreErrorCode::OutOfMemory,
                    "Group transform start feedback allocation failed");
            }
        } else {
            authoringFeedback_ = "Viewport transform started";
        }
    }

    const auto& snapshot = viewportTransformGizmo_.snapshot();
    if (snapshot.revision != viewportGizmo_.processedGizmoRevision) {
        viewportGizmo_.processedGizmoRevision = snapshot.revision;
        viewportGizmo_.delta = snapshot.delta;
        bool changed = false;
        Tina::Math::Quaternion localBasis = viewportGizmo_.baselineTransform.rotation;
        if (viewportGizmo_.targetCount != 0U) {
            localBasis = viewportGizmo_.targets[0].baselineWorld.rotation;
        }
        std::array<Tina::Scene::LocalTransform,
                   ViewportTransformTargetCapacity>
            stagedLocals{};
        for (Tina::Core::usize targetIndex = 0;
             targetIndex < viewportGizmo_.targetCount; ++targetIndex) {
            auto& target = viewportGizmo_.targets[targetIndex];
            auto world = applyViewportWorldTransformDelta(
                target.baselineWorld, viewportGizmo_.pivot,
                viewportGizmo_.delta, localBasis);
            if (!world) {
                return finishViewportGizmoWithoutCommit(
                    tree, true, world.error().message);
            }
            auto local = localTransformFromWorld(target.entity, *world);
            if (!local) {
                return finishViewportGizmoWithoutCommit(
                    tree, true, local.error().message);
            }
            stagedLocals[targetIndex] = *local;
            changed = changed || *local != target.previewLocal;
        }
        if (changed) {
            for (Tina::Core::usize targetIndex = 0;
                 targetIndex < viewportGizmo_.targetCount; ++targetIndex) {
                auto& target = viewportGizmo_.targets[targetIndex];
                target.previewLocal = stagedLocals[targetIndex];
                if (auto status = previewWorld_->setLocalTransform(
                        target.entity, target.previewLocal);
                    !status) {
                    return status;
                }
            }
            viewportGizmo_.previewTransform =
                viewportGizmo_.targets[0].previewLocal;
            if (auto status = previewWorld_->updateWorldTransforms(); !status) {
                return status;
            }
            viewportGizmo_.previewPublished = true;
            ++counters_.viewportGizmoPreviews;
            switch (snapshot.mode) {
            case Tina::Editor::EditorTransformGizmoMode::Rotate:
                counters_.viewportGizmoRotationDegrees =
                    viewportGizmo_.delta.rotationDegrees;
                authoringFeedback_ = viewportGizmo_.selectionCount > 1U
                                         ? "Group rotation preview"
                                         : "Viewport rotation preview";
                break;
            case Tina::Editor::EditorTransformGizmoMode::Scale:
                counters_.viewportGizmoScaleFactorX =
                    viewportGizmo_.delta.scaleFactors.x;
                counters_.viewportGizmoScaleFactorY =
                    viewportGizmo_.delta.scaleFactors.y;
                counters_.viewportGizmoScaleFactorZ =
                    viewportGizmo_.delta.scaleFactors.z;
                authoringFeedback_ = viewportGizmo_.selectionCount > 1U
                                         ? "Group scale preview"
                                         : "Viewport scale preview";
                break;
            case Tina::Editor::EditorTransformGizmoMode::Translate:
            default:
                counters_.viewportGizmoWorldDeltaX =
                    viewportGizmo_.delta.translation.x;
                counters_.viewportGizmoWorldDeltaY =
                    viewportGizmo_.delta.translation.y;
                counters_.viewportGizmoWorldDeltaZ =
                    viewportGizmo_.delta.translation.z;
                authoringFeedback_ = viewportGizmo_.selectionCount > 1U
                                         ? "Group translation preview"
                                         : (workspaceMode_ == WorkspaceMode::World2D
                                                ? "Move preview on the World2D XY plane"
                                                : "Move preview in the World3D viewport");
                break;
            }
        }
    }

    if (!viewportGizmo_.commitRequested) {
        return Tina::Core::success();
    }
    if (viewportTransformDeltaIsIdentity(viewportGizmo_.delta)) {
        return finishViewportGizmoWithoutCommit(tree, false,
                                                "ended without a document change");
    }
    if (!viewportGizmoContextMatches()) {
        return finishViewportGizmoWithoutCommit(
            tree, true, "rejected before commit because its baseline changed");
    }

    if (auto status = commitViewportGizmoTransform(viewportGizmo_); !status) {
        if (auto restoreStatus = validateRuntimePreview(); !restoreStatus) {
            return restoreStatus;
        }
        viewportGizmo_ = {};
        return status;
    }
    const u64 committedRevision = activeDocumentRevision();
    if (committedRevision == viewportGizmo_.baselineRevision) {
        return finishViewportGizmoWithoutCommit(
            tree, false, "ended without a document change");
    }
    if (committedRevision < viewportGizmo_.baselineRevision ||
        committedRevision - viewportGizmo_.baselineRevision != 1U) {
        viewportGizmo_ = {};
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "viewport gizmo commit did not publish exactly one document revision");
    }
    const Tina::Core::usize selectionCount = viewportGizmo_.selectionCount;
    const Tina::Core::usize targetCount = viewportGizmo_.targetCount;
    const bool multiSelection = selectionCount > 1U;
    const bool multiTarget = targetCount > 1U;
    const Tina::Editor::EditorTransformGizmoMode committedMode = snapshot.mode;
    viewportGizmo_ = {};
    ++counters_.viewportGizmoCommits;
    counters_.viewportMaximumGizmoTargets = (std::max)(
        counters_.viewportMaximumGizmoTargets,
        static_cast<u64>(targetCount));
    switch (committedMode) {
    case Tina::Editor::EditorTransformGizmoMode::Translate:
        ++counters_.viewportTranslateGizmoCommits;
        break;
    case Tina::Editor::EditorTransformGizmoMode::Rotate:
        ++counters_.viewportRotateGizmoCommits;
        if (multiTarget) {
            ++counters_.viewportGroupRotateGizmoCommits;
        }
        break;
    case Tina::Editor::EditorTransformGizmoMode::Scale:
        ++counters_.viewportScaleGizmoCommits;
        if (multiTarget) {
            ++counters_.viewportGroupScaleGizmoCommits;
        }
        break;
    }
    if (multiTarget) {
        ++counters_.viewportGroupGizmoCommits;
    }
    ++counters_.authoringEdits;
    if (auto status = validateRuntimePreview(); !status) {
        return status;
    }
    if (multiSelection) {
        try {
            authoringFeedback_ = "Group transform committed as one document revision | ";
            authoringFeedback_ += std::to_string(selectionCount);
            authoringFeedback_ += " selected | Group pivot";
        } catch (const std::bad_alloc&) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::OutOfMemory,
                "Group transform commit feedback allocation failed");
        }
    } else {
        authoringFeedback_ =
            "Viewport transform committed as one document revision";
    }
    return refreshAuthoringUi(tree);
}

auto EditorWorkspaceState::projectViewportWorldPoint(
    Tina::Math::Vec3 worldPoint) const noexcept -> ViewportProjectedPoint{
    if (!previewWorld_.has_value() || viewportLogicalRect_.width <= 0.0F ||
        viewportLogicalRect_.height <= 0.0F) {
        return {};
    }
    if (workspaceMode_ == WorkspaceMode::World2D) {
        const Tina::Scene::WorldTransform* camera =
            previewWorld_->worldTransform(previewCamera2D_);
        if (camera == nullptr) {
            return {};
        }
        const float normalizedX =
            (worldPoint.x - camera->position.x) / viewportWorldWidth() + 0.5F;
        const float normalizedY =
            0.5F - (worldPoint.y - camera->position.y) / viewportWorldHeight();
        if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY)) {
            return {};
        }
        return {
            .screen = {
                .x = viewportLogicalRect_.x + normalizedX * viewportLogicalRect_.width,
                .y = viewportLogicalRect_.y + normalizedY * viewportLogicalRect_.height,
            },
            .cameraDepth = 1.0F,
            .projectable = normalizedX >= -0.25F && normalizedX <= 1.25F &&
                           normalizedY >= -0.25F && normalizedY <= 1.25F,
        };
    }

    const Tina::Scene::WorldTransform* camera =
        previewWorld_->worldTransform(previewCamera3D_);
    if (camera == nullptr) {
        return {};
    }
    const Tina::Math::Vec3 relative = worldPoint - camera->position;
    const Tina::Math::Vec3 cameraSpace = Tina::Math::rotate(
        Tina::Math::conjugate(camera->rotation), relative);
    const float depth = -cameraSpace.z;
    if (!std::isfinite(depth) || depth <= 0.01F) {
        return {};
    }
    const float tangent = std::tan(ViewportPerspectiveFovDegrees * DegreesToRadians * 0.5F);
    const float aspect = viewportLogicalRect_.width / viewportLogicalRect_.height;
    const float normalizedDeviceX = cameraSpace.x / (depth * tangent * aspect);
    const float normalizedDeviceY = cameraSpace.y / (depth * tangent);
    if (!std::isfinite(normalizedDeviceX) ||
        !std::isfinite(normalizedDeviceY)) {
        return {};
    }
    return {
        .screen = {
            .x = viewportLogicalRect_.x +
                 (normalizedDeviceX * 0.5F + 0.5F) * viewportLogicalRect_.width,
            .y = viewportLogicalRect_.y +
                 (0.5F - normalizedDeviceY * 0.5F) * viewportLogicalRect_.height,
        },
        .cameraDepth = depth,
        .projectable = normalizedDeviceX >= -1.5F &&
                       normalizedDeviceX <= 1.5F &&
                       normalizedDeviceY >= -1.5F &&
                       normalizedDeviceY <= 1.5F,
    };
}

auto EditorWorkspaceState::viewportGizmoColor(
    Tina::Editor::EditorTransformGizmoHandle handle,
    bool highlighted) noexcept -> UI::UIStraightSrgba8Color{
    using Handle = Tina::Editor::EditorTransformGizmoHandle;
    if (highlighted) {
        return UI::rgb(0xF4F8FC, 250);
    }
    switch (handle) {
    case Handle::AxisX:
        return UI::rgb(0xE16060, 235);
    case Handle::AxisY:
        return UI::rgb(0x52BE7A, 235);
    case Handle::AxisZ:
        return UI::rgb(0x5894EA, 235);
    case Handle::PlaneXY:
        return UI::rgb(0xE8D45C, 210);
    case Handle::PlaneXZ:
        return UI::rgb(0xD75EC8, 210);
    case Handle::PlaneYZ:
        return UI::rgb(0x59C9C6, 210);
    case Handle::Uniform:
        return UI::rgb(0xE8EDF3, 235);
    case Handle::None:
    default:
        return UI::rgb(0x9AA8B8, 180);
    }
}

auto EditorWorkspaceState::collapseViewportGizmoVisuals(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    for (Tina::Core::usize index = 0;
         index < viewportGizmoVisibleNodeCount_; ++index) {
        UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
        collapsed.placement = UI::UILayoutPlacement::Overlay;
        collapsed.visibility = UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                viewportGizmoVisualNodes_[index], collapsed);
            !status) {
            return status;
        }
    }
    viewportGizmoVisibleNodeCount_ = 0;
    return Tina::Core::success();
}

auto EditorWorkspaceState::materializeViewportGizmoVisuals(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    Tina::Core::usize nodeCount = 0;
    const auto& snapshot = viewportTransformGizmo_.snapshot();
    const auto appendRect = [&](UI::UILogicalRect screenRect,
                                UI::UIStraightSrgba8Color color)
        -> Tina::Core::Status {
        if (!std::isfinite(screenRect.x) || !std::isfinite(screenRect.y) ||
            !std::isfinite(screenRect.width) ||
            !std::isfinite(screenRect.height) || screenRect.width <= 0.0F ||
            screenRect.height <= 0.0F) {
            return Tina::Core::success();
        }
        const float localLeft = screenRect.x - viewportLogicalRect_.x;
        const float localTop = screenRect.y - viewportLogicalRect_.y;
        const float localRight = localLeft + screenRect.width;
        const float localBottom = localTop + screenRect.height;
        const float clippedLeft = std::clamp(
            localLeft, 0.0F, viewportLogicalRect_.width);
        const float clippedTop = std::clamp(
            localTop, 0.0F, viewportLogicalRect_.height);
        const float clippedRight = std::clamp(
            localRight, 0.0F, viewportLogicalRect_.width);
        const float clippedBottom = std::clamp(
            localBottom, 0.0F, viewportLogicalRect_.height);
        if (!(clippedRight > clippedLeft) ||
            !(clippedBottom > clippedTop)) {
            return Tina::Core::success();
        }
        if (nodeCount == viewportGizmoVisualNodes_.size()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                "editor transform gizmo visual exceeded fixed node capacity");
        }
        UI::UILayoutStyle style = fixedSize(clippedRight - clippedLeft,
                                            clippedBottom - clippedTop);
        style.placement = UI::UILayoutPlacement::Overlay;
        style.overlay.horizontal = UI::UIAxisAlignment::Start;
        style.overlay.vertical = UI::UIAxisAlignment::Start;
        style.overlay.offset.x = UI::UILayoutLength::Px(clippedLeft);
        style.overlay.offset.y = UI::UILayoutLength::Px(clippedTop);
        const UI::UINodeId node = viewportGizmoVisualNodes_[nodeCount++];
        if (auto status = tree.setLayoutStyle(node, style); !status) {
            return status;
        }
        return tree.setBoxPaint(node, UI::makeSolidBox(color));
    };
    const auto appendLine = [&](Tina::Editor::EditorTransformGizmoPoint start,
                                Tina::Editor::EditorTransformGizmoPoint end,
                                Tina::Editor::EditorTransformGizmoHandle handle,
                                bool highlighted) -> Tina::Core::Status {
        const UI::UIStraightSrgba8Color color =
            viewportGizmoColor(handle, highlighted);
        const float thickness = highlighted ? 3.0F : 2.0F;
        if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
            !std::isfinite(end.x) || !std::isfinite(end.y) ||
            !std::isfinite(thickness) || thickness <= 0.0F) {
            return Tina::Core::success();
        }

        const float x0 = start.x - viewportLogicalRect_.x;
        const float y0 = start.y - viewportLogicalRect_.y;
        const float x1 = end.x - viewportLogicalRect_.x;
        const float y1 = end.y - viewportLogicalRect_.y;
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float length = std::hypot(dx, dy);
        if (!(std::isfinite(length) && length > 1.0e-4F)) {
            return Tina::Core::success();
        }
        const float halfThickness = thickness * 0.5F;
        const float extentX = std::abs(dy / length) * halfThickness;
        const float extentY = std::abs(dx / length) * halfThickness;
        const float left = (std::min)(x0, x1) - extentX;
        const float top = (std::min)(y0, y1) - extentY;
        const float right = (std::max)(x0, x1) + extentX;
        const float bottom = (std::max)(y0, y1) + extentY;
        if (right <= 0.0F || bottom <= 0.0F ||
            left >= viewportLogicalRect_.width ||
            top >= viewportLogicalRect_.height) {
            return Tina::Core::success();
        }
        if (nodeCount == viewportGizmoVisualNodes_.size()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                "editor transform gizmo visual exceeded fixed node capacity");
        }

        UI::UILayoutStyle style = fixedSize(right - left, bottom - top);
        style.placement = UI::UILayoutPlacement::Overlay;
        style.overlay.horizontal = UI::UIAxisAlignment::Start;
        style.overlay.vertical = UI::UIAxisAlignment::Start;
        style.overlay.offset.x = UI::UILayoutLength::Px(left);
        style.overlay.offset.y = UI::UILayoutLength::Px(top);
        const UI::UINodeId node = viewportGizmoVisualNodes_[nodeCount++];
        if (auto status = tree.setLayoutStyle(node, style); !status) {
            return status;
        }
        UI::UIBoxPaint paint = UI::makeSolidLine(
            color,
            UI::UILogicalPoint{.x = x0 - left, .y = y0 - top},
            UI::UILogicalPoint{.x = x1 - left, .y = y1 - top},
            thickness);
        return tree.setBoxPaint(node, paint);
    };
    const auto appendEllipseOutline = [&]
        (Tina::Editor::EditorTransformGizmoPoint center, float radius,
         Tina::Editor::EditorTransformGizmoHandle handle,
         bool highlighted) -> Tina::Core::Status {
        const UI::UIStraightSrgba8Color color =
            viewportGizmoColor(handle, highlighted);
        const float strokeWidth = highlighted ? 3.0F : 2.0F;
        if (!std::isfinite(center.x) || !std::isfinite(center.y) ||
            !std::isfinite(radius) || radius <= 0.0F) {
            return Tina::Core::success();
        }
        const float localCenterX = center.x - viewportLogicalRect_.x;
        const float localCenterY = center.y - viewportLogicalRect_.y;
        const float left = localCenterX - radius;
        const float top = localCenterY - radius;
        const float diameter = radius * 2.0F;
        if (left + diameter <= 0.0F || top + diameter <= 0.0F ||
            left >= viewportLogicalRect_.width ||
            top >= viewportLogicalRect_.height) {
            return Tina::Core::success();
        }
        if (nodeCount == viewportGizmoVisualNodes_.size()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                "editor transform gizmo visual exceeded fixed node capacity");
        }
        UI::UILayoutStyle style = fixedSize(diameter, diameter);
        style.placement = UI::UILayoutPlacement::Overlay;
        style.overlay.horizontal = UI::UIAxisAlignment::Start;
        style.overlay.vertical = UI::UIAxisAlignment::Start;
        style.overlay.offset.x = UI::UILayoutLength::Px(left);
        style.overlay.offset.y = UI::UILayoutLength::Px(top);
        const UI::UINodeId node = viewportGizmoVisualNodes_[nodeCount++];
        if (auto status = tree.setLayoutStyle(node, style); !status) {
            return status;
        }
        return tree.setBoxPaint(
            node, UI::makeEllipseOutline(color, strokeWidth));
    };

    for (const auto& geometry : snapshot.handles()) {
        const bool highlighted = geometry.handle == snapshot.activeHandle ||
                                 geometry.handle == snapshot.hoveredHandle;
        if (geometry.shape ==
            Tina::Editor::EditorTransformGizmoHandleShape::Segment) {
            if (auto status = appendLine(geometry.points[0], geometry.points[1],
                                         geometry.handle, highlighted);
                !status) {
                return status;
            }
        } else if (geometry.shape ==
                   Tina::Editor::EditorTransformGizmoHandleShape::Ring) {
            if (auto status = appendEllipseOutline(
                    geometry.points[0], geometry.radiusPixels,
                    geometry.handle, highlighted);
                !status) {
                return status;
            }
        } else {
            if (geometry.pointCount < 3U) {
                continue;
            }
            float minimumX = geometry.points[0].x;
            float minimumY = geometry.points[0].y;
            float maximumX = geometry.points[0].x;
            float maximumY = geometry.points[0].y;
            for (u32 pointIndex = 1; pointIndex < geometry.pointCount;
                 ++pointIndex) {
                minimumX = (std::min)(minimumX, geometry.points[pointIndex].x);
                minimumY = (std::min)(minimumY, geometry.points[pointIndex].y);
                maximumX = (std::max)(maximumX, geometry.points[pointIndex].x);
                maximumY = (std::max)(maximumY, geometry.points[pointIndex].y);
            }
            UI::UIStraightSrgba8Color fillColor =
                viewportGizmoColor(geometry.handle, highlighted);
            fillColor.alpha = highlighted ? 82U : 44U;
            if (auto status = appendRect(
                    {
                        .x = minimumX,
                        .y = minimumY,
                        .width = maximumX - minimumX,
                        .height = maximumY - minimumY,
                    },
                    fillColor);
                !status) {
                return status;
            }
            for (u32 pointIndex = 0; pointIndex < geometry.pointCount;
                 ++pointIndex) {
                const u32 nextPoint = (pointIndex + 1U) % geometry.pointCount;
                if (auto status = appendLine(
                        geometry.points[pointIndex], geometry.points[nextPoint],
                        geometry.handle, highlighted);
                    !status) {
                    return status;
                }
            }
        }
    }
    for (Tina::Core::usize index = nodeCount;
         index < viewportGizmoVisibleNodeCount_; ++index) {
        UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
        collapsed.placement = UI::UILayoutPlacement::Overlay;
        collapsed.visibility = UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                viewportGizmoVisualNodes_[index], collapsed);
            !status) {
            return status;
        }
    }
    viewportGizmoVisibleNodeCount_ = nodeCount;
    return Tina::Core::success();
}

auto EditorWorkspaceState::updateViewportTransformGizmo(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const bool transformTool =
        viewportToolMode_ == ViewportToolMode::Translate ||
        viewportToolMode_ == ViewportToolMode::Rotate ||
        viewportToolMode_ == ViewportToolMode::Scale;
    const Tina::Scene::EntityId entity =
        findPreviewEntity(stableEntityIdForHierarchyItem(selectionKey_));
    const Tina::Scene::WorldTransform* transform =
        previewWorld_.has_value() && entity.hasValue()
            ? previewWorld_->worldTransform(entity)
            : nullptr;
    if (!transformTool || transform == nullptr) {
        return collapseViewportGizmoVisuals(tree);
    }
    if (!viewportGizmo_.captured) {
        const Tina::Math::Vec3 gizmoOrigin = viewportSelectedEntityCount_ > 1U
                                                  ? viewportSelectionPivot()
                                                  : transform->position;
        const ViewportProjectedPoint origin =
            projectViewportWorldPoint(gizmoOrigin);
        if (!origin.projectable) {
            return collapseViewportGizmoVisuals(tree);
        }
        constexpr std::array worldDirections{
            Tina::Math::Vec3{1.0F, 0.0F, 0.0F},
            Tina::Math::Vec3{0.0F, 1.0F, 0.0F},
            Tina::Math::Vec3{0.0F, 0.0F, 1.0F},
        };
        Tina::Editor::EditorTransformGizmoFrame frame{
            .dimension = workspaceMode_ == WorkspaceMode::World2D
                             ? Tina::Editor::EditorTransformGizmoDimension::TwoD
                             : Tina::Editor::EditorTransformGizmoDimension::ThreeD,
            .screenOrigin = {.x = origin.screen.x, .y = origin.screen.y},
        };
        for (Tina::Core::usize axis = 0; axis < worldDirections.size();
             ++axis) {
            const Tina::Math::Vec3 worldDirection = worldDirections[axis];
            const Tina::Math::Vec3 localDirection =
                Tina::Math::rotate(transform->rotation, worldDirection);
            const ViewportProjectedPoint worldEndpoint =
                projectViewportWorldPoint(gizmoOrigin + worldDirection);
            const ViewportProjectedPoint localEndpoint =
                projectViewportWorldPoint(gizmoOrigin + localDirection);
            frame.worldAxes[axis] = {
                .screenPerWorldUnit = {
                    .x = worldEndpoint.projectable
                             ? worldEndpoint.screen.x - origin.screen.x
                             : 0.0F,
                    .y = worldEndpoint.projectable
                             ? worldEndpoint.screen.y - origin.screen.y
                             : 0.0F,
                },
                .worldDirection = {
                    .x = worldDirection.x,
                    .y = worldDirection.y,
                    .z = worldDirection.z,
                },
            };
            frame.localAxes[axis] = {
                .screenPerWorldUnit = {
                    .x = localEndpoint.projectable
                             ? localEndpoint.screen.x - origin.screen.x
                             : 0.0F,
                    .y = localEndpoint.projectable
                             ? localEndpoint.screen.y - origin.screen.y
                             : 0.0F,
                },
                .worldDirection = {
                    .x = localDirection.x,
                    .y = localDirection.y,
                    .z = localDirection.z,
                },
            };
        }
        if (viewportTransformGizmo_.publishFrame(frame) !=
            Tina::Editor::EditorTransformGizmoOperation::Success) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidConfiguration,
                "editor transform gizmo could not publish its projected frame");
        }
    }
    return materializeViewportGizmoVisuals(tree);
}

auto EditorWorkspaceState::updateViewportMarqueeVisual(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    UI::UILayoutStyle style = fixedSize(1.0F, 1.0F);
    style.placement = UI::UILayoutPlacement::Overlay;
    if (!viewportMarquee_.captured || viewportMarquee_.cancelRequested) {
        style.visibility = UI::UIVisibility::Collapsed;
        return tree.setLayoutStyle(viewportMarqueeNode_, style);
    }
    const float left = (std::min)(viewportMarquee_.start.x,
                                  viewportMarquee_.current.x) -
                       viewportLogicalRect_.x;
    const float top = (std::min)(viewportMarquee_.start.y,
                                 viewportMarquee_.current.y) -
                      viewportLogicalRect_.y;
    const float width = std::abs(viewportMarquee_.current.x -
                                 viewportMarquee_.start.x);
    const float height = std::abs(viewportMarquee_.current.y -
                                  viewportMarquee_.start.y);
    style.overlay.horizontal = UI::UIAxisAlignment::Start;
    style.overlay.vertical = UI::UIAxisAlignment::Start;
    style.overlay.offset.x = UI::UILayoutLength::Px(
        std::clamp(left, 0.0F, viewportLogicalRect_.width));
    style.overlay.offset.y = UI::UILayoutLength::Px(
        std::clamp(top, 0.0F, viewportLogicalRect_.height));
    style.size.width = UI::UILayoutLength::Px(
        std::clamp(width, 1.0F, viewportLogicalRect_.width));
    style.size.height = UI::UILayoutLength::Px(
        std::clamp(height, 1.0F, viewportLogicalRect_.height));
    return tree.setLayoutStyle(viewportMarqueeNode_, style);
}

auto EditorWorkspaceState::viewportStableIdAtPosition(
    UI::UILogicalPoint position) const noexcept -> std::optional<u32>
{
    if (viewportLogicalRect_.width <= 0.0F ||
        viewportLogicalRect_.height <= 0.0F ||
        position.x < viewportLogicalRect_.x ||
        position.x >= viewportLogicalRect_.right() ||
        position.y < viewportLogicalRect_.y ||
        position.y >= viewportLogicalRect_.bottom()) {
        return std::nullopt;
    }
    // 3D picks against world geometry along a ray, so the nearest object under the
    // cursor wins. Screen-space bounds cannot express depth: they would hand the
    // click to whichever box happens to be smaller on screen, which is the far
    // object as often as the near one.
    if (workspaceMode_ != WorkspaceMode::World2D) {
        const std::optional<Tina::Math::Ray> ray = viewportPickRay(position);
        if (!ray) {
            return std::nullopt;
        }
        std::array<Tina::Editor::EditorViewportPickCandidate,
                   Tina::Editor::EditorViewportPickCandidateCapacity>
            candidates{};
        const Tina::Core::usize candidateCount =
            collectViewportPickCandidates(candidates);
        const std::optional<Tina::Editor::EditorViewportPickHit> hit =
            Tina::Editor::pickNearestViewportCandidate(
                *ray, std::span{candidates.data(), candidateCount});
        if (!hit) {
            return std::nullopt;
        }
        return static_cast<u32>(hit->stableId);
    }

    // 2D uses an orthographic camera, so the screen bounds are a linear map of the
    // world bounds and carry no depth ambiguity to resolve.
    std::array<Tina::Editor::EditorMarqueeCandidate,
               ViewportMarqueeCandidateCapacity>
        candidates{};
    const Tina::Core::usize candidateCount =
        collectViewportMarqueeCandidates(candidates);
    std::optional<u32> hit{};
    float smallestArea = (std::numeric_limits<float>::max)();
    for (Tina::Core::usize index = 0; index < candidateCount; ++index) {
        const auto& bounds = candidates[index].screenBounds;
        const float left = (std::min)(bounds.x0, bounds.x1);
        const float right = (std::max)(bounds.x0, bounds.x1);
        const float top = (std::min)(bounds.y0, bounds.y1);
        const float bottom = (std::max)(bounds.y0, bounds.y1);
        if (position.x < left || position.x > right || position.y < top ||
            position.y > bottom) {
            continue;
        }
        const float area = (std::max)(1.0F, (right - left) * (bottom - top));
        // Prefer the smallest visible bounds so a small child remains
        // discoverable when it overlaps a larger parent preview.
        if (!hit.has_value() || area <= smallestArea) {
            hit = static_cast<u32>(candidates[index].stableId);
            smallestArea = area;
        }
    }
    return hit;
}

auto EditorWorkspaceState::updateViewportPreselectionVisual(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    const UI::UINodeId node = viewportPreselectionVisualNodes_[0];
    UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
    collapsed.placement = UI::UILayoutPlacement::Overlay;
    collapsed.visibility = UI::UIVisibility::Collapsed;
    if (!node.hasValue() || viewportPreselectionStableId_ == 0U ||
        viewportGizmo_.captured || viewportMarquee_.captured ||
        viewportNavigationDrag_.captured) {
        return tree.setLayoutStyle(node, collapsed);
    }
    std::array<Tina::Editor::EditorMarqueeCandidate,
               ViewportMarqueeCandidateCapacity>
        candidates{};
    const Tina::Core::usize candidateCount =
        collectViewportMarqueeCandidates(candidates);
    const auto candidate = std::find_if(
        candidates.begin(), candidates.begin() +
                              static_cast<std::ptrdiff_t>(candidateCount),
        [this](const Tina::Editor::EditorMarqueeCandidate& item) {
            return item.stableId == viewportPreselectionStableId_;
        });
    if (candidate == candidates.begin() +
                         static_cast<std::ptrdiff_t>(candidateCount)) {
        viewportPreselectionStableId_ = 0U;
        return tree.setLayoutStyle(node, collapsed);
    }
    const float left = (std::min)(candidate->screenBounds.x0,
                                  candidate->screenBounds.x1) - 2.0F;
    const float top = (std::min)(candidate->screenBounds.y0,
                                 candidate->screenBounds.y1) - 2.0F;
    const float right = (std::max)(candidate->screenBounds.x0,
                                   candidate->screenBounds.x1) + 2.0F;
    const float bottom = (std::max)(candidate->screenBounds.y0,
                                    candidate->screenBounds.y1) + 2.0F;
    const float clippedLeft = std::clamp(
        left - viewportLogicalRect_.x, 0.0F, viewportLogicalRect_.width);
    const float clippedTop = std::clamp(
        top - viewportLogicalRect_.y, 0.0F, viewportLogicalRect_.height);
    const float clippedRight = std::clamp(
        right - viewportLogicalRect_.x, 0.0F, viewportLogicalRect_.width);
    const float clippedBottom = std::clamp(
        bottom - viewportLogicalRect_.y, 0.0F, viewportLogicalRect_.height);
    if (!(clippedRight > clippedLeft) || !(clippedBottom > clippedTop)) {
        return tree.setLayoutStyle(node, collapsed);
    }
    UI::UILayoutStyle style = fixedSize(
        clippedRight - clippedLeft, clippedBottom - clippedTop);
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.horizontal = UI::UIAxisAlignment::Start;
    style.overlay.vertical = UI::UIAxisAlignment::Start;
    style.overlay.offset.x = UI::UILayoutLength::Px(clippedLeft);
    style.overlay.offset.y = UI::UILayoutLength::Px(clippedTop);
    UI::UIBoxPaint paint = UI::makeSolidBox(UI::rgb(0x000000, 0));
    const UI::UIStraightSrgba8Color color =
        viewportSelectionContains(viewportPreselectionStableId_)
            ? UI::rgb(0x8BE8CC, 245)
            : UI::rgb(0x64D8B4, 238);
    paint.borderLight = color;
    paint.borderDark = color;
    paint.borderWidth = viewportSelectionContains(viewportPreselectionStableId_)
                            ? 2.0F
                            : 1.5F;
    if (auto status = tree.setBoxPaint(node, paint); !status) {
        return status;
    }
    return tree.setLayoutStyle(node, style);
}

auto EditorWorkspaceState::updateViewportCollisionShapeVisuals(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    Tina::Core::usize nodeCount = 0;
    const auto hideRemaining = [&]() -> Tina::Core::Status {
        UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
        collapsed.placement = UI::UILayoutPlacement::Overlay;
        collapsed.visibility = UI::UIVisibility::Collapsed;
        for (Tina::Core::usize index = nodeCount;
             index < viewportCollisionShapeVisualNodes_.size(); ++index) {
            if (auto status = tree.setLayoutStyle(
                    viewportCollisionShapeVisualNodes_[index], collapsed);
                !status) {
                return status;
            }
        }
        return Tina::Core::success();
    };
    if (workspaceMode_ != WorkspaceMode::World2D || !previewWorld_.has_value() ||
        viewportLogicalRect_.width <= 0.0F ||
        viewportLogicalRect_.height <= 0.0F) {
        return hideRemaining();
    }

    // Screen-space axis-aligned quad. Shapes carry a local angle, but the local
    // rotation is not applied here: an unrotated outline is still an honest bound
    // and a rotated one needs its own primitive rather than a stretched rect.
    const auto appendQuad = [&](float left, float top, float right, float bottom,
                                const UI::UIBoxPaint& paint)
        -> Tina::Core::Status {
        if (nodeCount == viewportCollisionShapeVisualNodes_.size()) {
            return Tina::Core::success();
        }
        const float clippedLeft = std::clamp(
            left - viewportLogicalRect_.x, 0.0F, viewportLogicalRect_.width);
        const float clippedTop = std::clamp(
            top - viewportLogicalRect_.y, 0.0F, viewportLogicalRect_.height);
        const float clippedRight = std::clamp(
            right - viewportLogicalRect_.x, 0.0F, viewportLogicalRect_.width);
        const float clippedBottom = std::clamp(
            bottom - viewportLogicalRect_.y, 0.0F, viewportLogicalRect_.height);
        if (!(clippedRight > clippedLeft) || !(clippedBottom > clippedTop)) {
            return Tina::Core::success();
        }
        UI::UILayoutStyle style = fixedSize(clippedRight - clippedLeft,
                                            clippedBottom - clippedTop);
        style.placement = UI::UILayoutPlacement::Overlay;
        style.overlay.horizontal = UI::UIAxisAlignment::Start;
        style.overlay.vertical = UI::UIAxisAlignment::Start;
        style.overlay.offset.x = UI::UILayoutLength::Px(clippedLeft);
        style.overlay.offset.y = UI::UILayoutLength::Px(clippedTop);
        const UI::UINodeId node =
            viewportCollisionShapeVisualNodes_[nodeCount++];
        if (auto status = tree.setLayoutStyle(node, style); !status) {
            return status;
        }
        return tree.setBoxPaint(node, paint);
    };

    std::vector<Tina::AssetFormat::World2DEntityDesc> storage;
    auto snapshot = document_.parseCurrentSnapshot(storage);
    if (!snapshot) {
        // A shape overlay must never be the reason a frame fails; the document is
        // reported through the authoring path instead.
        return hideRemaining();
    }
    const float pixelsPerMeterX =
        viewportLogicalRect_.width / viewportWorldWidth();
    const float pixelsPerMeterY =
        viewportLogicalRect_.height / viewportWorldHeight();
    if (!std::isfinite(pixelsPerMeterX) || !std::isfinite(pixelsPerMeterY)) {
        return hideRemaining();
    }
    for (const auto& entity : storage) {
        if (!entity.physicsShape.has_value() || !entity.physicsShape->enabled) {
            continue;
        }
        const auto binding = std::find_if(
            previewBindings_.begin(), previewBindings_.end(),
            [&entity](const auto& candidate) {
                return candidate.stableEntityId == entity.stableEntityId;
            });
        if (binding == previewBindings_.end()) {
            continue;
        }
        const Tina::Scene::WorldTransform* transform =
            previewWorld_->worldTransform(binding->entity);
        if (transform == nullptr) {
            continue;
        }
        const auto& shape = *entity.physicsShape;
        Tina::Math::Vec3 center = transform->position;
        center.x += shape.localCenterX;
        center.y += shape.localCenterY;
        const ViewportProjectedPoint projected =
            projectViewportWorldPoint(center);
        if (!projected.projectable) {
            continue;
        }
        const bool selected = viewportSelectionContains(entity.stableEntityId);
        // Sensors read as outlines you can pass through, solid shapes as barriers.
        const UI::UIStraightSrgba8Color color =
            shape.sensor ? UI::rgb(0xE0C060, selected ? 245 : 200)
                         : UI::rgb(0x60C0E0, selected ? 245 : 200);
        const float thickness = selected ? 2.0F : 1.25F;
        const float scaleX = std::abs(transform->scale.x);
        const float scaleY = std::abs(transform->scale.y);
        if (shape.kind == Tina::AssetFormat::World2DPhysicsShapeKind::Box) {
            const float halfWidth =
                shape.halfExtentX * scaleX * pixelsPerMeterX;
            const float halfHeight =
                shape.halfExtentY * scaleY * pixelsPerMeterY;
            if (!std::isfinite(halfWidth) || !std::isfinite(halfHeight) ||
                halfWidth <= 0.0F || halfHeight <= 0.0F) {
                continue;
            }
            UI::UIBoxPaint paint = UI::makeSolidBox(UI::rgb(0x000000, 0));
            paint.borderLight = color;
            paint.borderDark = color;
            paint.borderWidth = thickness;
            if (auto status = appendQuad(projected.screen.x - halfWidth,
                                         projected.screen.y - halfHeight,
                                         projected.screen.x + halfWidth,
                                         projected.screen.y + halfHeight, paint);
                !status) {
                return status;
            }
            continue;
        }
        // Circle and Capsule both key off radius. A capsule's length lives in its
        // local points, which have no Inspector row yet, so its ring shows the
        // radius that is actually editable.
        const float radiusX = shape.radius * scaleX * pixelsPerMeterX;
        const float radiusY = shape.radius * scaleY * pixelsPerMeterY;
        if (!std::isfinite(radiusX) || !std::isfinite(radiusY) ||
            radiusX <= 0.0F || radiusY <= 0.0F) {
            continue;
        }
        if (auto status = appendQuad(
                projected.screen.x - radiusX, projected.screen.y - radiusY,
                projected.screen.x + radiusX, projected.screen.y + radiusY,
                UI::makeEllipseOutline(color, thickness));
            !status) {
            return status;
        }
    }
    return hideRemaining();
}

auto EditorWorkspaceState::collectViewportMarqueeCandidates(
    std::span<Tina::Editor::EditorMarqueeCandidate> output) const noexcept -> Tina::Core::usize{
    if (!previewWorld_.has_value()) {
        return 0;
    }
    Tina::Core::usize count = 0;
    const auto append = [&](u64 stableId, Tina::Scene::EntityId entity) {
        if (stableId == 0U || count == output.size()) {
            return;
        }
        const Tina::Scene::WorldTransform* transform =
            previewWorld_->worldTransform(entity);
        if (transform == nullptr) {
            return;
        }
        const ViewportProjectedPoint projected =
            projectViewportWorldPoint(transform->position);
        if (!projected.projectable) {
            return;
        }
        float halfWidth = 9.0F;
        float halfHeight = 9.0F;
        if (workspaceMode_ == WorkspaceMode::World2D) {
            if (const Tina::Scene::SpriteRenderer2D* sprite =
                    previewWorld_->spriteRenderer2D(entity);
                sprite != nullptr) {
                halfWidth = sprite->sizeOverrideMeters.x *
                            std::abs(transform->scale.x) /
                            viewportWorldWidth() *
                            viewportLogicalRect_.width * 0.5F;
                halfHeight = sprite->sizeOverrideMeters.y *
                             std::abs(transform->scale.y) /
                             viewportWorldHeight() *
                             viewportLogicalRect_.height * 0.5F;
            }
        } else if (const Tina::Scene::MeshRenderer3D* mesh =
                       previewWorld_->meshRenderer3D(entity);
                   mesh != nullptr) {
            const float maximumScale = (std::max)(
                {std::abs(transform->scale.x), std::abs(transform->scale.y),
                 std::abs(transform->scale.z)});
            const float pixelsPerWorldUnit =
                viewportLogicalRect_.height /
                (2.0F * projected.cameraDepth *
                 std::tan(ViewportPerspectiveFovDegrees * DegreesToRadians * 0.5F));
            halfWidth = halfHeight = (std::max)(
                6.0F, mesh->localBounds.radius * maximumScale *
                          pixelsPerWorldUnit);
        }
        output[count++] = {
            .stableId = stableId,
            .screenBounds = {
                .x0 = projected.screen.x - halfWidth,
                .y0 = projected.screen.y - halfHeight,
                .x1 = projected.screen.x + halfWidth,
                .y1 = projected.screen.y + halfHeight,
            },
        };
    };
    if (workspaceMode_ == WorkspaceMode::World2D) {
        for (const auto& binding : previewBindings_) {
            append(binding.stableEntityId, binding.entity);
        }
    } else {
        for (const auto& binding : preview3DBindings_) {
            append(binding.stableNodeId, binding.entity);
        }
    }
    return count;
}

auto EditorWorkspaceState::collectViewportPickCandidates(
    std::span<Tina::Editor::EditorViewportPickCandidate> output) const noexcept
    -> Tina::Core::usize{
    if (!previewWorld_.has_value() || workspaceMode_ == WorkspaceMode::World2D) {
        return 0;
    }
    Tina::Core::usize count = 0;
    for (const auto& binding : preview3DBindings_) {
        if (binding.stableNodeId == 0U || count == output.size()) {
            break;
        }
        const Tina::Scene::WorldTransform* transform =
            previewWorld_->worldTransform(binding.entity);
        if (transform == nullptr) {
            continue;
        }
        const Tina::Scene::MeshRenderer3D* mesh =
            previewWorld_->meshRenderer3D(binding.entity);
        // Authored local bounds, center included. Reducing this to a radius around
        // the transform origin is what previously put the hot zone away from an
        // imported mesh whose bounds are offset.
        const Tina::Math::Sphere localBounds =
            mesh != nullptr
                ? Tina::Math::Sphere{
                      Tina::Math::Vec3{mesh->localBounds.centerX,
                                       mesh->localBounds.centerY,
                                       mesh->localBounds.centerZ},
                      mesh->localBounds.radius}
                // A node without a mesh still needs a grabbable volume; use a small
                // sphere at its origin so empties stay selectable.
                : Tina::Math::Sphere{Tina::Math::Vec3{}, 0.25F};
        const std::optional<Tina::Math::Sphere> worldBounds = Tina::Math::transformed(
            localBounds, transform->position, transform->rotation, transform->scale);
        if (!worldBounds) {
            continue;
        }
        output[count++] = Tina::Editor::EditorViewportPickCandidate{
            .stableId = binding.stableNodeId,
            .worldBounds = *worldBounds,
        };
    }
    return count;
}

auto EditorWorkspaceState::viewportPickRay(
    UI::UILogicalPoint position) const noexcept -> std::optional<Tina::Math::Ray>{
    if (!previewWorld_.has_value()) {
        return std::nullopt;
    }
    const Tina::Scene::WorldTransform* camera =
        previewWorld_->worldTransform(previewCamera3D_);
    if (camera == nullptr) {
        return std::nullopt;
    }
    return Tina::Editor::editorViewportPickRay(Tina::Editor::EditorViewportRayQuery{
        .cameraPosition = camera->position,
        .cameraRotation = camera->rotation,
        .verticalFovDegrees = ViewportPerspectiveFovDegrees,
        .viewportWidth = viewportLogicalRect_.width,
        .viewportHeight = viewportLogicalRect_.height,
        .pointerX = position.x - viewportLogicalRect_.x,
        .pointerY = position.y - viewportLogicalRect_.y,
    });
}

auto EditorWorkspaceState::processViewportMarquee(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (!viewportMarquee_.captured) {
        return Tina::Core::success();
    }
    if (viewportMarquee_.cancelRequested) {
        viewportMarquee_ = {};
        ++counters_.viewportMarqueeCancels;
        return Tina::Core::success();
    }
    if (!viewportMarquee_.commitRequested) {
        return Tina::Core::success();
    }
    std::array<Tina::Editor::EditorMarqueeCandidate,
               ViewportMarqueeCandidateCapacity>
        candidates{};
    const Tina::Core::usize candidateCount =
        collectViewportMarqueeCandidates(candidates);
    const auto evaluated = Tina::Editor::EditorMarqueeSelection::Evaluate(
        {
            .x0 = viewportMarquee_.start.x,
            .y0 = viewportMarquee_.start.y,
            .x1 = viewportMarquee_.current.x,
            .y1 = viewportMarquee_.current.y,
        },
        std::span{candidates.data(), candidateCount},
        std::span{viewportSelectedEntityIds_.data(),
                  viewportSelectedEntityCount_},
        viewportMarquee_.mode);
    const Tina::Editor::EditorMarqueeSelectionMode committedMode =
        viewportMarquee_.mode;
    viewportMarquee_ = {};
    if (!evaluated) {
        ++counters_.viewportMarqueeRejects;
        return Tina::Core::failure(std::move(evaluated.error()));
    }
    ++counters_.viewportMarqueeCommits;
    switch (committedMode) {
    case Tina::Editor::EditorMarqueeSelectionMode::Replace:
        ++counters_.viewportMarqueeReplaceCommits;
        break;
    case Tina::Editor::EditorMarqueeSelectionMode::Add:
        ++counters_.viewportMarqueeAddCommits;
        break;
    case Tina::Editor::EditorMarqueeSelectionMode::Toggle:
        ++counters_.viewportMarqueeToggleCommits;
        break;
    }
    if (evaluated->changed()) {
        ++counters_.viewportMarqueeSelectionChanges;
    }
    counters_.viewportMarqueeAddedItems += evaluated->added().size();
    counters_.viewportMarqueeRemovedItems += evaluated->removed().size();
    counters_.viewportMarqueeMaximumSelection = (std::max)(
        counters_.viewportMarqueeMaximumSelection,
        static_cast<u64>(evaluated->selection().size()));
    std::array<u64, Tina::Editor::EditorMarqueeSelectionCapacity>
        previousSelectionStorage{};
    const Tina::Core::usize previousSelectionCount =
        viewportSelectedEntityCount_;
    std::copy_n(viewportSelectedEntityIds_.begin(),
                previousSelectionCount, previousSelectionStorage.begin());
    viewportSelectedEntityCount_ = evaluated->selection().size();
    std::copy(evaluated->selection().begin(), evaluated->selection().end(),
              viewportSelectedEntityIds_.begin());
    if (previousSelectionCount != viewportSelectedEntityCount_ ||
        !std::equal(previousSelectionStorage.begin(),
                    previousSelectionStorage.begin() +
                        static_cast<std::ptrdiff_t>(previousSelectionCount),
                    viewportSelectedEntityIds_.begin())) {
        ++viewportSelectionRevision_;
    }
    const std::optional<u64> hierarchyIndex =
        viewportSelectedEntityCount_ == 0U
            ? std::optional<u64>{0U}
            : hierarchyIndexForStableId(viewportSelectedEntityIds_[0]);
    if (hierarchyIndex.has_value()) {
        if (auto status = tree.setTreeViewSelectedIndex(
                hierarchyTree_, *hierarchyIndex);
            !status) {
            return status;
        }
        preserveViewportSelectionOnHierarchyPublish_ = true;
    }
    authoringFeedback_ = viewportSelectedEntityCount_ == 0U
                             ? "Viewport selection cleared"
                             : "Viewport marquee selection published";
    return refreshAuthoringUi(tree);
}

} // namespace Tina::EditorApp::WorkspaceInternal
