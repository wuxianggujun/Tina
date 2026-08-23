#include "EditorWorkspaceState.hpp"

namespace Tina::EditorApp::WorkspaceInternal {

auto EditorWorkspaceState::resetViewportInteractionState() noexcept -> void{
    if (viewportTransformGizmo_.snapshot().dragging()) {
        (void)viewportTransformGizmo_.cancelDrag(ViewportPrimaryPointerToken);
    }
    viewportGizmo_ = {};
    viewportNavigationDrag_ = {};
    viewportMarquee_ = {};
    if (viewportSelectedEntityCount_ != 0U) {
        ++viewportSelectionRevision_;
    }
    viewportSelectedEntityCount_ = 0;
    preserveViewportSelectionOnHierarchyPublish_ = false;
    pendingViewportNavigationCount_ = 0;
    viewportNavigationQueueOverflowed_ = false;
    pendingGizmoOrientationToggle_ = false;
    pendingGizmoSnapToggle_ = false;
    pendingMarqueeSelectionMode_.reset();
}

auto EditorWorkspaceState::queueViewportToolMode(ViewportToolMode mode) noexcept -> void{
    pendingViewportToolMode_ = mode;
}

auto EditorWorkspaceState::registerViewportPointerListeners(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const auto registerListener = [&](u32 index, UI::UIRoutedPointerEventKind kind,
                                      UI::UIRoutedPointerCallback callback) -> Tina::Core::Status {
        auto listener = tree.addRoutedPointerListener(
            {
                .node = viewportPreviewLayer_,
                .kind = kind,
                .phases = UI::UIEventPhaseMask::Target,
            },
            std::move(callback));
        if (!listener) {
            return Tina::Core::failure(std::move(listener.error()));
        }
        viewportPointerListeners_[index] = std::move(*listener);
        return Tina::Core::success();
    };

    if (auto status = registerListener(
            0, UI::UIRoutedPointerEventKind::ButtonDown,
            UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                handleViewportPointerDown(event);
            }});
        !status) {
        return status;
    }
    if (auto status = registerListener(
            1, UI::UIRoutedPointerEventKind::Move,
            UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                handleViewportPointerMove(event);
            }});
        !status) {
        return status;
    }
    if (auto status = registerListener(
            2, UI::UIRoutedPointerEventKind::ButtonUp,
            UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                handleViewportPointerUp(event);
            }});
        !status) {
        return status;
    }
    if (auto status = registerListener(
            3, UI::UIRoutedPointerEventKind::PointerCancel,
            UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
                handleViewportPointerCancel(event);
            }});
        !status) {
        return status;
    }
    return registerListener(
        4, UI::UIRoutedPointerEventKind::Wheel,
        UI::UIRoutedPointerCallback{[this](UI::UIRoutedPointerEvent& event) noexcept {
            handleViewportPointerWheel(event);
        }});
}

auto EditorWorkspaceState::viewportNavigationConfig() noexcept -> Tina::Editor::EditorViewportNavigationConfig{
    Tina::Editor::EditorViewportNavigationConfig config{};
    config.twoDWorldUnitsPerPixelAtZoomOne = 0.01F;
    config.minimumTwoDZoom = 0.25F;
    config.maximumTwoDZoom = 4.0F;
    config.minimumThreeDDistance = 0.5F;
    config.maximumThreeDDistance = 128.0F;
    return config;
}

auto EditorWorkspaceState::ensureViewportNavigation() -> Tina::Core::Status{
    if (viewportNavigation_.has_value()) {
        return Tina::Core::success();
    }
    auto navigation = Tina::Editor::EditorViewportNavigation::Create(
        viewportNavigationConfig(),
        viewport2DSessionState_, viewport3DSessionState_);
    if (!navigation) {
        return Tina::Core::failure(std::move(navigation.error()));
    }
    viewportNavigation_.emplace(std::move(*navigation));
    viewport2DSessionState_ = viewportNavigation_->twoD();
    viewport3DSessionState_ = viewportNavigation_->threeD();
    if (!viewport2DNavigationInitialized_) {
        viewport2DFrameAllState_ = viewport2DSessionState_;
    }
    if (!viewport3DNavigationInitialized_) {
        viewport3DFrameAllState_ = viewport3DSessionState_;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::viewportOrbitRotation(
    float yawRadians, float pitchRadians) noexcept -> Tina::Scene::Quaternion{
    const float halfYaw = yawRadians * 0.5F;
    const float halfPitch = -pitchRadians * 0.5F;
    const Tina::Scene::Quaternion yaw{
        .y = std::sin(halfYaw),
        .w = std::cos(halfYaw),
    };
    const Tina::Scene::Quaternion pitch{
        .x = std::sin(halfPitch),
        .w = std::cos(halfPitch),
    };
    return Tina::Scene::normalized(Tina::Scene::quaternionMultiply(yaw, pitch));
}

auto EditorWorkspaceState::syncViewportZoomFromNavigation() noexcept -> void{
    if (!viewportNavigation_.has_value()) {
        return;
    }
    if (workspaceMode_ == WorkspaceMode::World2D) {
        viewportZoomPercent_ =
            std::clamp(viewportNavigation_->twoD().zoom * 100.0F, 25.0F, 400.0F);
    } else {
        const float referenceDistance =
            (std::max)(viewport3DFrameAllState_.distance, 0.5F);
        viewportZoomPercent_ = std::clamp(
            referenceDistance * 100.0F /
                viewportNavigation_->threeD().distance,
            25.0F, 400.0F);
    }
    counters_.viewportZoomPercent = viewportZoomPercent_;
}

auto EditorWorkspaceState::persistViewportNavigationState() noexcept -> void{
    if (!viewportNavigation_.has_value()) {
        return;
    }
    if (workspaceMode_ == WorkspaceMode::World2D) {
        viewport2DSessionState_ = viewportNavigation_->twoD();
    } else {
        viewport3DSessionState_ = viewportNavigation_->threeD();
    }
}

auto EditorWorkspaceState::applyViewportNavigationToPreview() -> Tina::Core::Status{
    if (!previewWorld_.has_value()) {
        return Tina::Core::success();
    }
    if (auto status = ensureViewportNavigation(); !status) {
        return status;
    }
    Tina::Scene::EntityId camera{};
    Tina::Scene::LocalTransform transform{};
    if (workspaceMode_ == WorkspaceMode::World2D) {
        camera = previewCamera2D_;
        if (!camera.hasValue()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "editor viewport navigation has no independent Camera2D");
        }
        const Tina::Scene::LocalTransform* current =
            previewWorld_->localTransform(camera);
        if (current == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "editor viewport navigation cannot resolve Camera2D transform");
        }
        transform = *current;
        transform.position.x = viewportNavigation_->twoD().center.x;
        transform.position.y = viewportNavigation_->twoD().center.y;
    } else {
        camera = previewCamera3D_;
        if (!camera.hasValue()) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "editor viewport navigation has no independent Camera3D");
        }
        const Tina::Scene::LocalTransform* current =
            previewWorld_->localTransform(camera);
        if (current == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "editor viewport navigation cannot resolve Camera3D transform");
        }
        transform = *current;
        const auto& navigation = viewportNavigation_->threeD();
        const float cosinePitch = std::cos(navigation.pitchRadians);
        transform.position = {
            .x = navigation.target.x +
                 std::sin(navigation.yawRadians) * cosinePitch *
                     navigation.distance,
            .y = navigation.target.y +
                 std::sin(navigation.pitchRadians) * navigation.distance,
            .z = navigation.target.z +
                 std::cos(navigation.yawRadians) * cosinePitch *
                     navigation.distance,
        };
        transform.rotation = viewportOrbitRotation(
            navigation.yawRadians, navigation.pitchRadians);
    }
    if (auto status = previewWorld_->setLocalTransform(camera, transform); !status) {
        return status;
    }
    if (auto status = previewWorld_->updateWorldTransforms(); !status) {
        return status;
    }
    syncViewportZoomFromNavigation();
    persistViewportNavigationState();
    return Tina::Core::success();
}

auto EditorWorkspaceState::initializeOrApplyViewportNavigation() -> Tina::Core::Status{
    if (auto status = ensureViewportNavigation(); !status) {
        return status;
    }
    if (!previewWorld_.has_value()) {
        return Tina::Core::success();
    }
    auto twoD = viewportNavigation_->twoD();
    auto threeD = viewportNavigation_->threeD();
    bool initialize = false;
    if (workspaceMode_ == WorkspaceMode::World2D &&
        !viewport2DNavigationInitialized_) {
        const Tina::Scene::WorldTransform* camera =
            previewWorld_->worldTransform(previewCamera2D_);
        if (camera == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "editor could not initialize Camera2D navigation");
        }
        twoD.center = {.x = camera->position.x, .y = camera->position.y};
        twoD.zoom = 1.0F;
        viewport2DSessionState_ = twoD;
        viewport2DFrameAllState_ = twoD;
        viewport2DNavigationInitialized_ = true;
        initialize = true;
    } else if (workspaceMode_ == WorkspaceMode::World3D &&
               !viewport3DNavigationInitialized_) {
        const Tina::Scene::WorldTransform* camera =
            previewWorld_->worldTransform(previewCamera3D_);
        const Tina::Scene::EntityId focusEntity =
            findPreviewEntity(stableEntityIdForHierarchyItem(selectionKey_));
        const Tina::Scene::WorldTransform* focus =
            focusEntity.hasValue() ? previewWorld_->worldTransform(focusEntity)
                                   : nullptr;
        if (camera == nullptr) {
            return Tina::Core::failure(
                Tina::Core::CoreErrorCode::Internal,
                "editor could not initialize Camera3D navigation");
        }
        Tina::Scene::Vec3 direction = Tina::Scene::rotate(
            camera->rotation, {.x = 0.0F, .y = 0.0F, .z = -1.0F});
        float distance = PreviewWorld3DCameraDistance;
        if (focus != nullptr) {
            const Tina::Scene::Vec3 offset = focus->position - camera->position;
            const float lengthSquared = Tina::Scene::dot(offset, offset);
            if (std::isfinite(lengthSquared) && lengthSquared > 0.25F) {
                distance = std::sqrt(lengthSquared);
                const float inverseDistance = 1.0F / distance;
                direction = offset * inverseDistance;
            }
        }
        threeD.target = {
            .x = camera->position.x + direction.x * distance,
            .y = camera->position.y + direction.y * distance,
            .z = camera->position.z + direction.z * distance,
        };
        threeD.yawRadians = std::atan2(-direction.x, -direction.z);
        threeD.pitchRadians = std::asin(
            std::clamp(-direction.y, -1.0F, 1.0F));
        threeD.distance = distance;
        viewport3DSessionState_ = threeD;
        viewport3DFrameAllState_ = threeD;
        viewport3DNavigationInitialized_ = true;
        viewport3DViewPreset_.reset();
        initialize = true;
    }
    if (initialize) {
        if (workspaceMode_ == WorkspaceMode::World2D) {
            if (auto status = viewportNavigation_->set2DView(twoD); !status) {
                return status;
            }
        } else if (auto status = viewportNavigation_->set3DView(threeD); !status) {
            return status;
        }
    }
    return applyViewportNavigationToPreview();
}

auto EditorWorkspaceState::focusViewportOnSelection() -> Tina::Core::Status{
    const u32 stableId = stableEntityIdForHierarchyItem(selectionKey_);
    if (stableId == 0U || !previewWorld_.has_value()) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::InvalidAuthoringOperation,
            "Select a scene item before focusing the viewport");
    }
    const Tina::Scene::EntityId entity = findPreviewEntity(stableId);
    const Tina::Scene::WorldTransform* transform = entity.hasValue()
        ? previewWorld_->worldTransform(entity)
        : nullptr;
    if (transform == nullptr) {
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::EntityNotFound,
            "The selected scene item is unavailable in the viewport preview");
    }
    if (auto status = ensureViewportNavigation(); !status) {
        return status;
    }
    auto twoD = viewportNavigation_->twoD();
    auto threeD = viewportNavigation_->threeD();
    if (workspaceMode_ == WorkspaceMode::World2D) {
        twoD.center = {
            .x = transform->position.x,
            .y = transform->position.y,
        };
        viewport2DNavigationInitialized_ = true;
    } else {
        threeD.target = {
            .x = transform->position.x,
            .y = transform->position.y,
            .z = transform->position.z,
        };
        viewport3DNavigationInitialized_ = true;
    }
    if (workspaceMode_ == WorkspaceMode::World2D) {
        if (auto status = viewportNavigation_->set2DView(twoD); !status) {
            return status;
        }
    } else if (auto status = viewportNavigation_->set3DView(threeD); !status) {
        return status;
    }
    return applyViewportNavigationToPreview();
}

auto EditorWorkspaceState::queueViewportNavigationInput(
    Tina::Editor::EditorViewportNavigationInput input) noexcept -> bool{
    using Kind = Tina::Editor::EditorViewportNavigationInputKind;
    if (pendingViewportNavigationCount_ != 0U) {
        auto& previous = pendingViewportNavigationInputs_[
            pendingViewportNavigationCount_ - 1U];
        if (previous.kind == input.kind && input.kind != Kind::Zoom2D) {
            if (input.kind == Kind::Dolly3D) {
                previous.wheelSteps += input.wheelSteps;
            } else {
                previous.pixelDelta.x += input.pixelDelta.x;
                previous.pixelDelta.y += input.pixelDelta.y;
            }
            return std::isfinite(previous.pixelDelta.x) &&
                   std::isfinite(previous.pixelDelta.y) &&
                   std::isfinite(previous.wheelSteps);
        }
    }
    if (pendingViewportNavigationCount_ ==
        pendingViewportNavigationInputs_.size()) {
        viewportNavigationQueueOverflowed_ = true;
        return false;
    }
    pendingViewportNavigationInputs_[pendingViewportNavigationCount_++] = input;
    return true;
}

auto EditorWorkspaceState::frameViewportContents() -> Tina::Core::Status{
    if (auto status = ensureViewportNavigation(); !status) {
        return status;
    }
    auto twoD = viewportNavigation_->twoD();
    auto threeD = viewportNavigation_->threeD();
    if (workspaceMode_ == WorkspaceMode::World2D) {
        twoD = viewport2DFrameAllState_;
    } else {
        threeD = viewport3DFrameAllState_;
        viewport3DViewPreset_.reset();
    }
    if (workspaceMode_ == WorkspaceMode::World2D) {
        if (auto status = viewportNavigation_->set2DView(twoD); !status) {
            return status;
        }
    } else if (auto status = viewportNavigation_->set3DView(threeD); !status) {
        return status;
    }
    return applyViewportNavigationToPreview();
}

auto EditorWorkspaceState::processViewportNavigation() -> Tina::Core::Status{
    if (viewportNavigationQueueOverflowed_) {
        viewportNavigationQueueOverflowed_ = false;
        pendingViewportNavigationCount_ = 0;
        return Tina::Core::failure(
            Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
            "editor viewport navigation input exceeded its fixed batch capacity");
    }
    if (auto status = ensureViewportNavigation(); !status) {
        return status;
    }
    if (pendingViewportNavigationCount_ != 0U) {
        std::array<Tina::Editor::EditorViewportNavigationInput,
                   Tina::Editor::EditorViewportNavigationLimits::MaximumInputCommandsPerBatch>
            inputs{};
        std::copy_n(pendingViewportNavigationInputs_.begin(),
                    pendingViewportNavigationCount_, inputs.begin());
        const Tina::Core::usize inputCount =
            std::exchange(pendingViewportNavigationCount_, Tina::Core::usize{0});
        const float viewportHeight = viewportLogicalRect_.height;
        if (!std::isfinite(viewportHeight) || viewportHeight <= 0.0F) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::InvalidConfiguration,
                "editor viewport navigation requires a committed viewport height");
        }
        const auto& config = viewportNavigation_->config();
        bool orbitInputObserved = false;
        for (Tina::Core::usize index = 0; index < inputCount; ++index) {
            auto& input = inputs[index];
            if (input.kind ==
                    Tina::Editor::EditorViewportNavigationInputKind::Orbit3D &&
                (input.pixelDelta.x != 0.0F || input.pixelDelta.y != 0.0F)) {
                orbitInputObserved = true;
            }
            if (input.kind ==
                Tina::Editor::EditorViewportNavigationInputKind::Pan2D) {
                const float desiredWorldUnitsPerPixel =
                    viewportWorldHeight() / viewportHeight;
                const float moduleWorldUnitsPerPixel =
                    config.twoDWorldUnitsPerPixelAtZoomOne /
                    viewportNavigation_->twoD().zoom;
                const float scale = desiredWorldUnitsPerPixel /
                                    moduleWorldUnitsPerPixel;
                input.pixelDelta.x *= scale;
                input.pixelDelta.y *= scale;
            } else if (input.kind ==
                       Tina::Editor::EditorViewportNavigationInputKind::Pan3D) {
                const float desiredWorldUnitsPerPixel =
                    2.0F * viewportNavigation_->threeD().distance *
                    std::tan(55.0F * DegreesToRadians * 0.5F) /
                    viewportHeight;
                const float moduleWorldUnitsPerPixel =
                    viewportNavigation_->threeD().distance *
                    config.threeDPanWorldUnitsPerPixelAtUnitDistance;
                const float scale = desiredWorldUnitsPerPixel /
                                    moduleWorldUnitsPerPixel;
                input.pixelDelta.x *= scale;
                input.pixelDelta.y *= scale;
            }
        }
        if (auto status = viewportNavigation_->apply(
                std::span{inputs.data(), inputCount});
            !status) {
            return status;
        }
        ++counters_.viewportNavigationBatches;
        for (Tina::Core::usize index = 0; index < inputCount; ++index) {
            switch (inputs[index].kind) {
            case Tina::Editor::EditorViewportNavigationInputKind::Pan2D:
                ++counters_.viewportPan2DInputs;
                break;
            case Tina::Editor::EditorViewportNavigationInputKind::Zoom2D:
                ++counters_.viewportZoom2DInputs;
                break;
            case Tina::Editor::EditorViewportNavigationInputKind::Orbit3D:
                ++counters_.viewportOrbit3DInputs;
                break;
            case Tina::Editor::EditorViewportNavigationInputKind::Pan3D:
                ++counters_.viewportPan3DInputs;
                break;
            case Tina::Editor::EditorViewportNavigationInputKind::Dolly3D:
                ++counters_.viewportDolly3DInputs;
                break;
            }
        }
        if (orbitInputObserved) {
            viewport3DViewPreset_.reset();
            viewportViewModeRefreshPending_ = true;
        }
    }
    if (auto status = applyViewportNavigationToPreview(); !status) {
        return status;
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::queueAutomaticViewportNavigation() noexcept -> bool{
    if (pendingViewportNavigationCount_ != 0U ||
        viewportNavigationQueueOverflowed_ || viewportGizmo_.captured ||
        viewportNavigationDrag_.captured || viewportMarquee_.captured ||
        !std::isfinite(viewportLogicalRect_.width) ||
        !std::isfinite(viewportLogicalRect_.height) ||
        viewportLogicalRect_.width <= 0.0F ||
        viewportLogicalRect_.height <= 0.0F) {
        return false;
    }
    using Kind = Tina::Editor::EditorViewportNavigationInputKind;
    if (workspaceMode_ == WorkspaceMode::World2D) {
        const bool panQueued = queueViewportNavigationInput({
            .kind = Kind::Pan2D,
            .pixelDelta = {.x = 24.0F, .y = -12.0F},
        });
        const bool zoomQueued = queueViewportNavigationInput({
            .kind = Kind::Zoom2D,
            // Keep the fixed auto-demo TileMap fully inside the viewport while
            // still exercising the anchored zoom path.
            .wheelSteps = -1.0F,
            .viewportSizePixels = {
                .x = viewportLogicalRect_.width,
                .y = viewportLogicalRect_.height,
            },
            .anchorPixels = {
                .x = viewportLogicalRect_.width * 0.65F,
                .y = viewportLogicalRect_.height * 0.4F,
            },
        });
        return panQueued && zoomQueued;
    }
    return queueViewportNavigationInput({
               .kind = Kind::Orbit3D,
               .pixelDelta = {.x = 18.0F, .y = -8.0F},
           }) &&
           queueViewportNavigationInput({
               .kind = Kind::Pan3D,
               .pixelDelta = {.x = -10.0F, .y = 6.0F},
           }) &&
           queueViewportNavigationInput({
               .kind = Kind::Dolly3D,
               .wheelSteps = 1.0F,
           });
}

auto EditorWorkspaceState::beginViewportNavigation(
    Tina::Platform::PointerId pointer,
    Tina::Platform::PointerButton button) noexcept -> bool{
    const bool supported = button == Tina::Platform::PointerButton::Middle ||
                           (workspaceMode_ == WorkspaceMode::World3D &&
                            button == Tina::Platform::PointerButton::Secondary);
    if (!supported || viewportNavigationDrag_.captured || viewportGizmo_.captured ||
        viewportMarquee_.captured) {
        return false;
    }
    viewportNavigationDrag_ = {
        .pointer = pointer,
        .button = button,
        .captured = true,
    };
    return true;
}

auto EditorWorkspaceState::updateViewportNavigation(
    const UI::UIPointerInputEvent& input) noexcept -> bool{
    if (!viewportNavigationDrag_.captured ||
        input.pointer != viewportNavigationDrag_.pointer) {
        return false;
    }
    using Kind = Tina::Editor::EditorViewportNavigationInputKind;
    Kind kind = Kind::Pan2D;
    if (workspaceMode_ == WorkspaceMode::World3D) {
        kind = viewportNavigationDrag_.button ==
                       Tina::Platform::PointerButton::Secondary
                   ? Kind::Orbit3D
                   : Kind::Pan3D;
    }
    return queueViewportNavigationInput({
        .kind = kind,
        .pixelDelta = {.x = input.delta.x, .y = input.delta.y},
    });
}

auto EditorWorkspaceState::handleViewportPointerDown(UI::UIRoutedPointerEvent& event) noexcept -> void{
    const UI::UIPointerInputEvent& input = event.input();
    if (input.button != Tina::Platform::PointerButton::Primary) {
        if (!beginViewportNavigation(input.pointer, input.button)) {
            return;
        }
        event.capturePointer();
        (void)event.claimPointerButton(input.button);
        event.consumeInputTransition();
        event.preventDefaultAction();
        return;
    }
    if (queueViewportTileBrush(input.position)) {
        (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
        event.consumeInputTransition();
        event.preventDefaultAction();
        return;
    }
    const bool began = beginViewportGizmo(input.pointer, input.position);
    const bool marqueeBegan = !began &&
                              beginViewportMarquee(input.pointer,
                                                   input.position);
    if (!began && !marqueeBegan && !viewportGizmo_.captured &&
        !viewportMarquee_.captured) {
        return;
    }
    if (began || marqueeBegan) {
        event.capturePointer();
    }
    (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
    event.consumeInputTransition();
    event.preventDefaultAction();
}

auto EditorWorkspaceState::handleViewportPointerMove(UI::UIRoutedPointerEvent& event) noexcept -> void{
    const UI::UIPointerInputEvent& input = event.input();
    if (viewportNavigationDrag_.captured) {
        if (!updateViewportNavigation(input)) {
            return;
        }
        (void)event.claimPointerButton(viewportNavigationDrag_.button);
        event.consumeInputTransition();
        event.preventDefaultAction();
        return;
    }
    if (updateViewportGizmo(input.pointer, input.position)) {
        (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
        event.consumeInputTransition();
        event.preventDefaultAction();
        return;
    }
    if (updateViewportMarquee(input.pointer, input.position)) {
        (void)event.claimPointerButton(Tina::Platform::PointerButton::Primary);
        event.consumeInputTransition();
        event.preventDefaultAction();
        return;
    }
    if (viewportToolMode_ == ViewportToolMode::Translate ||
        viewportToolMode_ == ViewportToolMode::Rotate ||
        viewportToolMode_ == ViewportToolMode::Scale) {
        (void)viewportTransformGizmo_.updateHover(
            {.x = input.position.x, .y = input.position.y});
    }
}

auto EditorWorkspaceState::handleViewportPointerUp(UI::UIRoutedPointerEvent& event) noexcept -> void{
    const UI::UIPointerInputEvent& input = event.input();
    if (viewportNavigationDrag_.captured &&
        input.pointer == viewportNavigationDrag_.pointer &&
        input.button == viewportNavigationDrag_.button) {
        viewportNavigationDrag_ = {};
        event.releasePointerCapture();
        event.consumeInputTransition();
        event.preventDefaultAction();
        return;
    }
    if (input.button != Tina::Platform::PointerButton::Primary) {
        return;
    }
    bool handled = requestViewportGizmoCommit(input.pointer, input.position);
    if (!handled && updateViewportMarquee(input.pointer, input.position)) {
        viewportMarquee_.commitRequested = true;
        handled = true;
    }
    if (!handled) {
        return;
    }
    event.releasePointerCapture();
    event.consumeInputTransition();
    event.preventDefaultAction();
}

auto EditorWorkspaceState::handleViewportPointerCancel(UI::UIRoutedPointerEvent& event) noexcept -> void{
    const UI::UIPointerInputEvent& input = event.input();
    bool handled = false;
    if (viewportNavigationDrag_.captured &&
        input.pointer == viewportNavigationDrag_.pointer) {
        viewportNavigationDrag_ = {};
        handled = true;
    }
    if (viewportGizmo_.captured && input.pointer == viewportGizmo_.pointer) {
        (void)viewportTransformGizmo_.cancelDrag(ViewportPrimaryPointerToken);
        viewportGizmo_.cancelRequested = true;
        handled = true;
    }
    if (viewportMarquee_.captured && input.pointer == viewportMarquee_.pointer) {
        viewportMarquee_.cancelRequested = true;
        handled = true;
    }
    if (!handled) {
        return;
    }
    event.releasePointerCapture();
    event.consumeInputTransition();
    event.preventDefaultAction();
}

auto EditorWorkspaceState::handleViewportPointerWheel(UI::UIRoutedPointerEvent& event) noexcept -> void{
    const UI::UIPointerInputEvent& input = event.input();
    if (viewportNavigationDrag_.captured || viewportGizmo_.captured ||
        viewportMarquee_.captured || viewportLogicalRect_.width <= 0.0F ||
        viewportLogicalRect_.height <= 0.0F) {
        return;
    }
    const float wheelSteps = input.delta.y != 0.0F
                                 ? input.delta.y
                                 : input.delta.x;
    if (!std::isfinite(wheelSteps) || wheelSteps == 0.0F) {
        return;
    }
    using Kind = Tina::Editor::EditorViewportNavigationInputKind;
    Tina::Editor::EditorViewportNavigationInput navigation{
        .kind = workspaceMode_ == WorkspaceMode::World2D
                    ? Kind::Zoom2D
                    : Kind::Dolly3D,
        .wheelSteps = wheelSteps,
    };
    if (workspaceMode_ == WorkspaceMode::World2D) {
        navigation.viewportSizePixels = {
            .x = viewportLogicalRect_.width,
            .y = viewportLogicalRect_.height,
        };
        navigation.anchorPixels = {
            .x = input.position.x - viewportLogicalRect_.x,
            .y = input.position.y - viewportLogicalRect_.y,
        };
    }
    if (!queueViewportNavigationInput(navigation)) {
        return;
    }
    event.consumeInputTransition();
    event.preventDefaultAction();
}

auto EditorWorkspaceState::viewportSelectionContains(u64 stableId) const noexcept -> bool{
    if (stableId == 0U) {
        return false;
    }
    return std::find(viewportSelectedEntityIds_.begin(),
                     viewportSelectedEntityIds_.begin() +
                         static_cast<std::ptrdiff_t>(
                             viewportSelectedEntityCount_),
                     stableId) !=
           viewportSelectedEntityIds_.begin() +
               static_cast<std::ptrdiff_t>(viewportSelectedEntityCount_);
}

auto EditorWorkspaceState::refreshViewportToolUi(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const bool authoring = authoringEnabled();
    const bool tileToolsAvailable = authoring && tileMapEditingContext();
    if (!tileToolsAvailable &&
        (viewportToolMode_ == ViewportToolMode::TilePaint ||
         viewportToolMode_ == ViewportToolMode::TileErase)) {
        viewportToolMode_ = ViewportToolMode::Select;
    }
    const bool selectActive = viewportToolMode_ == ViewportToolMode::Select;
    const auto refreshToolButtons = [&tree](const auto& buttons, bool selected,
                                            bool enabled) -> Tina::Core::Status {
        for (const UI::UINodeId button : buttons) {
            if (auto status = tree.setRadioButtonSelected(button, selected); !status) {
                return status;
            }
            if (auto status = tree.setEnabled(button, enabled); !status) {
                return status;
            }
        }
        return Tina::Core::success();
    };
    if (auto status = refreshToolButtons(selectToolButtons_, selectActive, authoring); !status) {
        return status;
    }
    if (auto status = refreshToolButtons(
            translateToolButtons_, viewportToolMode_ == ViewportToolMode::Translate, authoring);
        !status) {
        return status;
    }
    if (auto status = refreshToolButtons(
            rotateToolButtons_, viewportToolMode_ == ViewportToolMode::Rotate, authoring);
        !status) {
        return status;
    }
    if (auto status = refreshToolButtons(
            scaleToolButtons_, viewportToolMode_ == ViewportToolMode::Scale, authoring);
        !status) {
        return status;
    }
    if (auto status = tree.setRadioButtonSelected(
            tilePaintToolButton_, viewportToolMode_ == ViewportToolMode::TilePaint); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(tilePaintToolButton_, tileToolsAvailable); !status) {
        return status;
    }
    if (auto status = tree.setRadioButtonSelected(
            tileEraseToolButton_, viewportToolMode_ == ViewportToolMode::TileErase); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(tileEraseToolButton_, tileToolsAvailable); !status) {
        return status;
    }
    const bool transformTool = viewportToolMode_ == ViewportToolMode::Translate ||
                               viewportToolMode_ == ViewportToolMode::Rotate ||
                               viewportToolMode_ == ViewportToolMode::Scale;
    const bool interactionActive = viewportGizmo_.captured ||
                                   viewportNavigationDrag_.captured ||
                                   viewportMarquee_.captured;
    if (auto status = tree.setEnabled(
            orientationButton_, authoring && transformTool && !interactionActive);
        !status) {
        return status;
    }
    if (auto status = tree.setEnabled(
            snapButton_, authoring && transformTool && !interactionActive);
        !status) {
        return status;
    }

    const auto orientation = viewportTransformGizmo_.snapshot().orientation;
    const bool snapEnabled = viewportTransformGizmo_.snap().enabled;
    if (auto status = tree.setRadioButtonSelected(
            orientationButton_,
            orientation == Tina::Editor::EditorTransformGizmoOrientation::World);
        !status) {
        return status;
    }
    if (auto status = tree.setRadioButtonSelected(snapButton_, snapEnabled);
        !status) {
        return status;
    }

    constexpr std::array marqueeModes{
        Tina::Editor::EditorMarqueeSelectionMode::Replace,
        Tina::Editor::EditorMarqueeSelectionMode::Add,
        Tina::Editor::EditorMarqueeSelectionMode::Toggle,
    };
    for (Tina::Core::usize index = 0; index < marqueeModeButtons_.size();
         ++index) {
        const bool active = marqueeSelectionMode_ == marqueeModes[index];
        if (auto status = tree.setRadioButtonSelected(marqueeModeButtons_[index], active); !status) {
            return status;
        }
        if (auto status = tree.setEnabled(
                marqueeModeButtons_[index], selectActive && !interactionActive); !status) {
            return status;
        }
    }
    return Tina::Core::success();
}

auto EditorWorkspaceState::extractWorld3DViewport(Tina::RenderSceneExtractionContext& context) const -> Tina::Core::Status{
    if (!previewCamera3D_.hasValue()) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor World3D viewport is missing its camera");
    }
    const Tina::Scene::PerspectiveCamera3D* currentCamera =
        previewWorld_->perspectiveCamera3D(previewCamera3D_);
    if (currentCamera == nullptr) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "editor World3D viewport camera component is unavailable");
    }
    Tina::Scene::PerspectiveCamera3D camera = *currentCamera;
    camera.normalizedViewport = *viewportNormalized_;
    camera.active = true;
    if (auto status = previewWorld_->setPerspectiveCamera3D(previewCamera3D_, camera); !status) {
        return status;
    }
    if (auto status = Tina::Scene::extractRenderSceneFromWorld(
            *previewWorld_, context.renderSceneWriter(), context.frameResourceSink(),
            Tina::Scene::ExtractRenderSceneParams{
                .surfaceViewport = {
                    .pixelWidth = surfacePixelWidth_,
                    .pixelHeight = surfacePixelHeight_,
                },
                .mesh3DBindingResolver = {
                    .userData = const_cast<EditorWorkspaceState*>(this),
                    .resolve = &EditorWorkspaceState::resolvePreviewMesh,
                },
                .material3DBindingResolver = {
                    .userData = const_cast<EditorWorkspaceState*>(this),
                    .resolve = &EditorWorkspaceState::resolvePreviewMaterial,
                },
                .skinnedMesh3DBindingResolver = {
                    .userData = const_cast<EditorWorkspaceState*>(this),
                    .resolve = &EditorWorkspaceState::resolvePreviewSkinnedMesh,
                },
            });
        !status) {
        return status;
    }
    counters_.gpuViewportMeshes = previewResolvedMeshCount_;
    counters_.gpuViewportDocumentRevision = previewRevision_;
    return Tina::Core::success();
}

auto EditorWorkspaceState::viewportWorldHeight() const noexcept -> float{
    return PreviewWorldHeight * 100.0F / viewportZoomPercent_;
}

auto EditorWorkspaceState::viewportWorldWidth() const noexcept -> float{
    if (viewportLogicalRect_.width > 0.0F && viewportLogicalRect_.height > 0.0F) {
        return viewportWorldHeight() * viewportLogicalRect_.width /
               viewportLogicalRect_.height;
    }
    return PreviewWorldWidth * 100.0F / viewportZoomPercent_;
}

auto EditorWorkspaceState::viewportGridColor(
    Tina::Editor::EditorViewportGridSegmentKind kind) noexcept -> UI::UIStraightSrgba8Color{
    using Kind = Tina::Editor::EditorViewportGridSegmentKind;
    switch (kind) {
    case Kind::Major:
        return UI::rgb(0x9AA8B8, 116);
    case Kind::AxisX:
        return UI::rgb(0xE16060, 225);
    case Kind::AxisY:
        return UI::rgb(0x52BE7A, 225);
    case Kind::AxisZ:
        return UI::rgb(0x5894EA, 225);
    case Kind::Minor:
    default:
        return UI::rgb(0x748397, 66);
    }
}

auto EditorWorkspaceState::viewportGridLayout(
    const Tina::Editor::EditorViewportGridSegment& segment,
    float viewportWidth,
    float viewportHeight,
    UI::UILineGeometry& line) noexcept -> UI::UILayoutStyle{
    using Kind = Tina::Editor::EditorViewportGridSegmentKind;
    const bool axis = segment.kind == Kind::AxisX ||
                      segment.kind == Kind::AxisY ||
                      segment.kind == Kind::AxisZ;
    const float thickness = axis ? 2.0F
                                 : (segment.kind == Kind::Major ? 1.25F : 1.0F);
    const float startX = segment.startX * viewportWidth;
    const float startY = segment.startY * viewportHeight;
    const float endX = segment.endX * viewportWidth;
    const float endY = segment.endY * viewportHeight;
    const float deltaX = endX - startX;
    const float deltaY = endY - startY;
    const float length = std::hypot(deltaX, deltaY);
    if (!(std::isfinite(length) && length > 1.0e-4F) ||
        !(std::isfinite(viewportWidth) && std::isfinite(viewportHeight)) ||
        viewportWidth <= 0.0F || viewportHeight <= 0.0F) {
        // Collapsed nodes still carry a committed paint value. Keep that
        // value structurally valid even when clipping removes the segment.
        line = UI::UILineGeometry{
            .start = UI::UILogicalPoint{.x = 0.0F, .y = 0.5F},
            .end = UI::UILogicalPoint{.x = 1.0F, .y = 0.5F},
            .thickness = 1.0F,
        };
        UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
        collapsed.placement = UI::UILayoutPlacement::Overlay;
        collapsed.visibility = UI::UIVisibility::Collapsed;
        return collapsed;
    }

    const float halfThickness = thickness * 0.5F;
    const float extentX = std::abs(deltaY / length) * halfThickness;
    const float extentY = std::abs(deltaX / length) * halfThickness;
    const float left = (std::min)(startX, endX) - extentX;
    const float top = (std::min)(startY, endY) - extentY;
    const float right = (std::max)(startX, endX) + extentX;
    const float bottom = (std::max)(startY, endY) + extentY;
    line = UI::UILineGeometry{
        .start = UI::UILogicalPoint{.x = startX - left, .y = startY - top},
        .end = UI::UILogicalPoint{.x = endX - left, .y = endY - top},
        .thickness = thickness,
    };
    UI::UILayoutStyle style{};
    style.placement = UI::UILayoutPlacement::Overlay;
    style.overlay.horizontal = UI::UIAxisAlignment::Start;
    style.overlay.vertical = UI::UIAxisAlignment::Start;
    style.overlay.offset.x = UI::UILayoutLength::Px(left);
    style.overlay.offset.y = UI::UILayoutLength::Px(top);
    style.size.width = UI::UILayoutLength::Px(right - left);
    style.size.height = UI::UILayoutLength::Px(bottom - top);
    return style;
}

auto EditorWorkspaceState::updateViewportGrid(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    if (!viewportNavigation_.has_value()) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "editor viewport grid has no navigation state");
    }
    const auto& twoD = viewportNavigation_->twoD();
    const auto& threeD = viewportNavigation_->threeD();
    Tina::Editor::EditorViewportGridConfig gridConfig{
        .projection = workspaceMode_ == WorkspaceMode::World2D
                          ? Tina::Editor::EditorViewportGridProjection::Orthographic2D
                          : Tina::Editor::EditorViewportGridProjection::Perspective3D,
        .logicalWidth = viewportLogicalRect_.width,
        .logicalHeight = viewportLogicalRect_.height,
        .zoomPercent = viewportZoomPercent_,
        .cameraCenterX = twoD.center.x,
        .cameraCenterY = twoD.center.y,
        .cameraTargetX = threeD.target.x,
        .cameraTargetY = threeD.target.y,
        .cameraTargetZ = threeD.target.z,
        .cameraYawRadians = threeD.yawRadians,
        .cameraPitchRadians = threeD.pitchRadians,
        .cameraDistance = threeD.distance,
        .verticalFovDegrees = 55.0F,
    };
    auto updated = viewportGrid_.update(gridConfig);
    if (!updated) {
        return Tina::Core::failure(std::move(updated.error()));
    }
    if (!*updated) {
        return Tina::Core::success();
    }

    const std::span segments = viewportGrid_.segments();
    Tina::Core::usize visualNodeCount = 0;
    for (const auto& segment : segments) {
        if (visualNodeCount == viewportGridNodes_.size()) {
            return Tina::Core::failure(
                Tina::Editor::EditorErrorCode::DocumentCapacityExceeded,
                "editor viewport grid exceeded its fixed visual node capacity");
        }
        UI::UILineGeometry line{};
        const UI::UINodeId node = viewportGridNodes_[visualNodeCount++];
        if (auto status = tree.setLayoutStyle(
                node, viewportGridLayout(segment, viewportLogicalRect_.width,
                                         viewportLogicalRect_.height,
                                         line));
            !status) {
            return status;
        }
        UI::UIBoxPaint paint = UI::makeSolidLine(
            viewportGridColor(segment.kind), line.start, line.end, line.thickness);
        if (auto status = tree.setBoxPaint(node, paint); !status) {
            return status;
        }
    }
    for (Tina::Core::usize index = visualNodeCount;
         index < viewportGridVisibleNodeCount_; ++index) {
        UI::UILayoutStyle collapsed = fixedSize(1.0F, 1.0F);
        collapsed.placement = UI::UILayoutPlacement::Overlay;
        collapsed.visibility = UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(viewportGridNodes_[index], collapsed);
            !status) {
            return status;
        }
    }
    viewportGridVisibleNodeCount_ = visualNodeCount;

    const auto& stats = viewportGrid_.stats();
    counters_.viewportGridRevision = stats.revision;
    counters_.viewportGridSegments = stats.segmentCount;
    counters_.viewportGridMinorLines = stats.minorSegmentCount;
    counters_.viewportGridMajorLines = stats.majorSegmentCount;
    counters_.viewportGridAxisLines = stats.axisSegmentCount;
    counters_.viewportZoomPercent = viewportZoomPercent_;
    counters_.viewportGridReady = !segments.empty();
    if (workspaceMode_ == WorkspaceMode::World2D) {
        counters_.viewportGrid2DObserved = true;
    } else {
        counters_.viewportGrid3DObserved = true;
    }

    return Tina::Core::success();
}

auto EditorWorkspaceState::updateViewportOrientationCompass(
    Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status
{
    if (!viewportNavigation_.has_value()) {
        return Tina::Core::failure(
            Tina::Core::CoreErrorCode::Internal,
            "editor viewport orientation compass has no navigation state");
    }
    const auto& navigation3D = viewportNavigation_->threeD();
    const ViewportOrientationCompassVisualState visualState{
        .workspace = workspaceMode_,
        .viewportWidth = viewportLogicalRect_.width,
        .viewportHeight = viewportLogicalRect_.height,
        .cameraYawRadians = workspaceMode_ == WorkspaceMode::World3D
                                ? navigation3D.yawRadians
                                : 0.0F,
        .cameraPitchRadians = workspaceMode_ == WorkspaceMode::World3D
                                  ? navigation3D.pitchRadians
                                  : 0.0F,
    };
    if (viewportOrientationCompassVisualState_ == visualState) {
        return Tina::Core::success();
    }
    const bool world3D = workspaceMode_ == WorkspaceMode::World3D;
    const float compassExtent = world3D ? ViewportOrientationCompassExtent
                                        : ViewportOrientationCompass2DExtent;
    const float axisLength = world3D ? ViewportOrientationAxisLength
                                     : ViewportOrientationAxis2DLength;
    if (!std::isfinite(visualState.viewportWidth) ||
        !std::isfinite(visualState.viewportHeight) ||
        visualState.viewportWidth <= 0.0F || visualState.viewportHeight <= 0.0F ||
        visualState.viewportWidth < compassExtent +
                                        ViewportOrientationCompassInset * 2.0F ||
        visualState.viewportHeight < compassExtent +
                                         ViewportOrientationCompassInset * 2.0F) {
        UI::UILayoutStyle collapsed = fixedSize(
            compassExtent, compassExtent);
        collapsed.placement = UI::UILayoutPlacement::Overlay;
        collapsed.visibility = UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                viewportOrientationCompass_, collapsed);
            !status) {
            return status;
        }
        viewportOrientationCompassVisualState_ = visualState;
        return Tina::Core::success();
    }

    UI::UILayoutStyle compassLayout = fixedSize(
        compassExtent, compassExtent);
    compassLayout.placement = UI::UILayoutPlacement::Overlay;
    compassLayout.overlay.horizontal = UI::UIAxisAlignment::Start;
    compassLayout.overlay.vertical = UI::UIAxisAlignment::Start;
    compassLayout.overlay.offset.x = UI::UILayoutLength::Px(
        visualState.viewportWidth - compassExtent -
        ViewportOrientationCompassInset);
    compassLayout.overlay.offset.y = UI::UILayoutLength::Px(
        ViewportOrientationCompassInset);
    if (auto status = tree.setLayoutStyle(
            viewportOrientationCompass_, compassLayout);
        !status) {
        return status;
    }
    if (auto status = tree.setBoxPaint(
            viewportOrientationCompass_,
            UI::makeSolidEllipse(
                world3D ? UI::rgb(0x10171C, 242)
                        : UI::rgb(0x000000, 0)));
        !status) {
        return status;
    }
    for (Tina::Core::usize layer = 0;
         layer < ViewportOrientationOrbLayerCount; ++layer) {
        UI::UILayoutStyle layerLayout =
            viewportOrientationOrbLayerLayouts_[layer];
        layerLayout.visibility = world3D ? UI::UIVisibility::Visible
                                         : UI::UIVisibility::Collapsed;
        if (auto status = tree.setLayoutStyle(
                viewportOrientationOrbLayers_[layer], layerLayout);
            !status) {
            return status;
        }
    }

    constexpr std::array worldDirections{
        Tina::Scene::Vec3{1.0F, 0.0F, 0.0F},
        Tina::Scene::Vec3{0.0F, 1.0F, 0.0F},
        Tina::Scene::Vec3{0.0F, 0.0F, 1.0F},
    };
    constexpr std::array axisKinds{
        Tina::Editor::EditorViewportGridSegmentKind::AxisX,
        Tina::Editor::EditorViewportGridSegmentKind::AxisY,
        Tina::Editor::EditorViewportGridSegmentKind::AxisZ,
    };
    const float center = compassExtent * 0.5F;
    const Tina::Scene::Quaternion inverseCameraRotation =
        Tina::Scene::quaternionConjugate(viewportOrbitRotation(
            visualState.cameraYawRadians, visualState.cameraPitchRadians));

    for (Tina::Core::usize axis = 0; axis < ViewportOrientationAxisCount;
         ++axis) {
        const bool visible = world3D || axis < 2U;
        UI::UILayoutStyle lineLayout = fixedSize(
            compassExtent, compassExtent);
        lineLayout.placement = UI::UILayoutPlacement::Overlay;
        UI::UILayoutStyle endpointLayout = fixedSize(
            ViewportOrientationEndpointExtent,
            ViewportOrientationEndpointExtent);
        endpointLayout.placement = UI::UILayoutPlacement::Overlay;
        if (!visible) {
            lineLayout.visibility = UI::UIVisibility::Collapsed;
            endpointLayout.visibility = UI::UIVisibility::Collapsed;
            if (auto status = tree.setLayoutStyle(
                    viewportOrientationAxisLines_[axis], lineLayout);
                !status) {
                return status;
            }
            if (auto status = tree.setLayoutStyle(
                    viewportOrientationAxisEndpoints_[axis], endpointLayout);
                !status) {
                return status;
            }
            if (auto status = tree.setLayoutStyle(
                    viewportOrientationAxisLabels_[axis], endpointLayout);
                !status) {
                return status;
            }
            continue;
        }

        Tina::Scene::Vec3 cameraDirection = worldDirections[axis];
        if (world3D) {
            cameraDirection = Tina::Scene::rotate(
                inverseCameraRotation, worldDirections[axis]);
        }
        const UI::UILogicalPoint endpoint{
            .x = center + cameraDirection.x * axisLength,
            .y = center - cameraDirection.y * axisLength,
        };
        const float projectedLength = std::hypot(
            endpoint.x - center, endpoint.y - center);
        if (!(std::isfinite(projectedLength) && projectedLength > 0.5F)) {
            lineLayout.visibility = UI::UIVisibility::Collapsed;
        }
        if (auto status = tree.setLayoutStyle(
                viewportOrientationAxisLines_[axis], lineLayout);
            !status) {
            return status;
        }

        UI::UIStraightSrgba8Color axisColor = viewportGridColor(axisKinds[axis]);
        if (world3D && cameraDirection.z < 0.0F) {
            axisColor.alpha = 165U;
        } else {
            axisColor.alpha = 238U;
        }
        if (auto status = tree.setBoxPaint(
                viewportOrientationAxisLines_[axis],
                UI::makeSolidLine(
                    axisColor,
                    UI::UILogicalPoint{.x = center, .y = center}, endpoint,
                    2.0F));
            !status) {
            return status;
        }

        const float endpointHalf = ViewportOrientationEndpointExtent * 0.5F;
        endpointLayout.overlay.horizontal = UI::UIAxisAlignment::Start;
        endpointLayout.overlay.vertical = UI::UIAxisAlignment::Start;
        endpointLayout.overlay.offset.x = UI::UILayoutLength::Px(
            endpoint.x - endpointHalf);
        endpointLayout.overlay.offset.y = UI::UILayoutLength::Px(
            endpoint.y - endpointHalf);
        if (auto status = tree.setLayoutStyle(
                viewportOrientationAxisEndpoints_[axis], endpointLayout);
            !status) {
            return status;
        }
        if (auto status = tree.setBoxPaint(
                viewportOrientationAxisEndpoints_[axis],
                UI::makeSolidEllipse(axisColor));
            !status) {
            return status;
        }
        if (auto status = tree.setLayoutStyle(
                viewportOrientationAxisLabels_[axis], endpointLayout);
            !status) {
            return status;
        }
    }

    viewportOrientationCompassVisualState_ = visualState;
    return Tina::Core::success();
}

auto EditorWorkspaceState::updateGpuViewport(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    auto viewportRect = tree.committedLayoutRect(viewportPreviewLayer_);
    if (!viewportRect) {
        return Tina::Core::failure(std::move(viewportRect.error()));
    }
    auto rootRect = tree.committedLayoutRect(uiRoot_.rootNodeId());
    if (!rootRect) {
        return Tina::Core::failure(std::move(rootRect.error()));
    }
    if (!std::isfinite(rootRect->x) || !std::isfinite(rootRect->y) ||
        !std::isfinite(rootRect->width) || !std::isfinite(rootRect->height) ||
        !std::isfinite(viewportRect->x) || !std::isfinite(viewportRect->y) ||
        !std::isfinite(viewportRect->width) || !std::isfinite(viewportRect->height) ||
        rootRect->width <= 0.0F || rootRect->height <= 0.0F ||
        viewportRect->width <= 0.0F || viewportRect->height <= 0.0F) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor GPU viewport committed layout is not usable");
    }

    const double rootWidth = rootRect->width;
    const double rootHeight = rootRect->height;
    const double left = std::clamp(
        (static_cast<double>(viewportRect->x) - rootRect->x) / rootWidth, 0.0, 1.0);
    const double top = std::clamp(
        (static_cast<double>(viewportRect->y) - rootRect->y) / rootHeight, 0.0, 1.0);
    const double right = std::clamp(
        (static_cast<double>(viewportRect->x) + viewportRect->width - rootRect->x) /
            rootWidth,
        0.0, 1.0);
    const double bottom = std::clamp(
        (static_cast<double>(viewportRect->y) + viewportRect->height - rootRect->y) /
            rootHeight,
        0.0, 1.0);
    if (!(right > left) || !(bottom > top)) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor GPU viewport lies outside the committed UI root");
    }

    Tina::Render::RenderNormalizedViewport normalized{
        .x = static_cast<float>(left),
        .y = static_cast<float>(top),
        .width = static_cast<float>(right - left),
        .height = static_cast<float>(bottom - top),
    };
    if (static_cast<double>(normalized.x) + normalized.width > 1.0) {
        normalized.width = std::nextafter(normalized.width, 0.0F);
    }
    if (static_cast<double>(normalized.y) + normalized.height > 1.0) {
        normalized.height = std::nextafter(normalized.height, 0.0F);
    }
    if (normalized.width <= 0.0F || normalized.height <= 0.0F) {
        return Tina::Core::failure(Tina::Core::CoreErrorCode::Internal,
                                   "editor GPU viewport normalized extent is empty");
    }

    viewportLogicalRect_ = *viewportRect;
    viewportNormalized_ = normalized;
    const double maximumSurfaceExtent =
        static_cast<double>((std::numeric_limits<u32>::max)());
    surfacePixelWidth_ = static_cast<u32>(
        std::clamp(std::round(static_cast<double>(rootRect->width)), 1.0,
                   maximumSurfaceExtent));
    surfacePixelHeight_ = static_cast<u32>(
        std::clamp(std::round(static_cast<double>(rootRect->height)), 1.0,
                   maximumSurfaceExtent));
    counters_.viewportLogicalX = viewportRect->x;
    counters_.viewportLogicalY = viewportRect->y;
    counters_.viewportLogicalWidth = viewportRect->width;
    counters_.viewportLogicalHeight = viewportRect->height;
    counters_.viewportNormalizedX = normalized.x;
    counters_.viewportNormalizedY = normalized.y;
    counters_.viewportNormalizedWidth = normalized.width;
    counters_.viewportNormalizedHeight = normalized.height;
    counters_.gpuViewportReady = true;
    return Tina::Core::success();
}

auto EditorWorkspaceState::refreshViewportViewModeUi(Tina::PrimaryWindowUITreeUpdater& tree) -> Tina::Core::Status{
    const bool world2D = workspaceMode_ == WorkspaceMode::World2D;
    std::string label = "View: ";
    label += viewport3DViewPreset_.has_value()
                 ? viewportViewPresetName(*viewport3DViewPreset_)
                 : "Custom";
    if (auto status = tree.setText(viewportMode_, label); !status) {
        return status;
    }
    if (auto status = tree.setEnabled(viewportMode_, !world2D); !status) {
        return status;
    }
    viewportModeLayout_.visibility = world2D ? UI::UIVisibility::Collapsed
                                             : UI::UIVisibility::Visible;
    return tree.setLayoutStyle(viewportMode_, viewportModeLayout_);
}

} // namespace Tina::EditorApp::WorkspaceInternal
