#include <tina/runtime/PrimaryWindowUI.hpp>

#include "ui/PrimaryWindowUICapabilityState.hpp"

#include <tina/runtime/RuntimeErrors.hpp>

#include <string_view>
#include <utility>

namespace Tina {
namespace {

template <typename Value> [[nodiscard]] Core::Result<Value> expiredFacade(std::string_view operation)
{
    Core::Error error{RuntimeErrorCode::UIPhaseCapabilityExpired, "The primary-window UI capability has expired"};
    error.addContext(operation, "moved-from or detached facade");
    return Core::failure(std::move(error));
}

} // namespace

PrimaryWindowUIBuildTransaction::PrimaryWindowUIBuildTransaction(
    Runtime::Detail::PrimaryWindowUICapabilityState& state, u64 epoch,
    Runtime::Detail::PrimaryWindowUIPhase phase) noexcept
    : m_state(&state), m_epoch(epoch), m_phase(phase)
{
}

PrimaryWindowUIBuildTransaction::~PrimaryWindowUIBuildTransaction() noexcept
{
    reset();
}

PrimaryWindowUIBuildTransaction::PrimaryWindowUIBuildTransaction(
    PrimaryWindowUIBuildTransaction&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)), m_epoch(std::exchange(other.m_epoch, 0)),
      m_phase(std::exchange(other.m_phase, Runtime::Detail::PrimaryWindowUIPhase::None))
{
}

PrimaryWindowUIBuildTransaction& PrimaryWindowUIBuildTransaction::operator=(
    PrimaryWindowUIBuildTransaction&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    reset();
    m_state = std::exchange(other.m_state, nullptr);
    m_epoch = std::exchange(other.m_epoch, 0);
    m_phase = std::exchange(other.m_phase, Runtime::Detail::PrimaryWindowUIPhase::None);
    return *this;
}

Core::Result<UI::UINodeId> PrimaryWindowUIBuildTransaction::createElement(
    UI::UINodeId parent, const UI::UIElementDescriptor& descriptor)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUIBuildTransaction::createElement");
    }
    return m_state->createElementFromBuildTransaction(m_epoch, m_phase, parent, descriptor);
}

Core::Result<UI::UINodeId> PrimaryWindowUIBuildTransaction::commit()
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUIBuildTransaction::commit");
    }
    auto componentRoot = m_state->commitBuildTransaction(m_epoch, m_phase);
    if (componentRoot)
    {
        m_state = nullptr;
        m_epoch = 0;
        m_phase = Runtime::Detail::PrimaryWindowUIPhase::None;
    }
    return componentRoot;
}

void PrimaryWindowUIBuildTransaction::reset() noexcept
{
    if (m_state != nullptr)
    {
        m_state->resetBuildTransaction(m_epoch, m_phase);
    }
    m_state = nullptr;
    m_epoch = 0;
    m_phase = Runtime::Detail::PrimaryWindowUIPhase::None;
}

UI::UINodeId PrimaryWindowUIBuildTransaction::rootNodeId() const noexcept
{
    return m_state != nullptr ? m_state->buildTransactionRootNodeId(m_epoch, m_phase) : UI::UINodeId{};
}

UI::UIComponentBuildBudget PrimaryWindowUIBuildTransaction::remainingBudget() const noexcept
{
    return m_state != nullptr ? m_state->buildTransactionRemainingBudget(m_epoch, m_phase)
                              : UI::UIComponentBuildBudget{};
}

bool PrimaryWindowUIBuildTransaction::isActive() const noexcept
{
    return m_state != nullptr && m_state->isBuildTransactionActive(m_epoch, m_phase);
}

PrimaryWindowUIBuildTransaction::operator bool() const noexcept
{
    return isActive();
}

PrimaryWindowUIImageResolverRegistration::PrimaryWindowUIImageResolverRegistration(
    Runtime::Detail::PrimaryWindowUICapabilityState& state, u32 slot, u32 generation) noexcept
    : m_state(&state), m_slot(slot), m_generation(generation)
{
}

PrimaryWindowUIImageResolverRegistration::~PrimaryWindowUIImageResolverRegistration() noexcept
{
    reset();
}

PrimaryWindowUIImageResolverRegistration::PrimaryWindowUIImageResolverRegistration(
    PrimaryWindowUIImageResolverRegistration&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)), m_slot(std::exchange(other.m_slot, 0)),
      m_generation(std::exchange(other.m_generation, 0))
{
}

PrimaryWindowUIImageResolverRegistration&
PrimaryWindowUIImageResolverRegistration::operator=(
    PrimaryWindowUIImageResolverRegistration&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    reset();
    m_state = std::exchange(other.m_state, nullptr);
    m_slot = std::exchange(other.m_slot, 0);
    m_generation = std::exchange(other.m_generation, 0);
    return *this;
}

void PrimaryWindowUIImageResolverRegistration::reset() noexcept
{
    if (m_state != nullptr && m_generation != 0)
    {
        m_state->unbindImageResolver(m_slot, m_generation);
    }
    m_state = nullptr;
    m_slot = 0;
    m_generation = 0;
}

bool PrimaryWindowUIImageResolverRegistration::isActive() const noexcept
{
    return m_state != nullptr && m_generation != 0 &&
           m_state->isImageResolverActive(m_slot, m_generation);
}

PrimaryWindowUIImageResolverRegistration::operator bool() const noexcept
{
    return isActive();
}

PrimaryWindowUITreeUpdater::PrimaryWindowUITreeUpdater(Runtime::Detail::PrimaryWindowUICapabilityState& state,
                                                       u64 epoch, Runtime::Detail::PrimaryWindowUIPhase phase,
                                                       UI::UITreeUpdater updater) noexcept
    : m_state(&state), m_epoch(epoch), m_phase(phase), m_updater(std::move(updater))
{
}

PrimaryWindowUITreeUpdater::PrimaryWindowUITreeUpdater(PrimaryWindowUITreeUpdater&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)), m_epoch(std::exchange(other.m_epoch, 0)),
      m_phase(std::exchange(other.m_phase, Runtime::Detail::PrimaryWindowUIPhase::None)),
      m_updater(std::move(other.m_updater))
{
}

PrimaryWindowUITreeUpdater& PrimaryWindowUITreeUpdater::operator=(PrimaryWindowUITreeUpdater&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    m_state = std::exchange(other.m_state, nullptr);
    m_epoch = std::exchange(other.m_epoch, 0);
    m_phase = std::exchange(other.m_phase, Runtime::Detail::PrimaryWindowUIPhase::None);
    m_updater = std::move(other.m_updater);
    return *this;
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isAlive(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isAlive");
    }
    return m_state->isAlive(m_epoch, m_phase, m_updater, node);
}

Core::Result<UI::UILogicalRect> PrimaryWindowUITreeUpdater::committedLayoutRect(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UILogicalRect>("PrimaryWindowUITreeUpdater::committedLayoutRect");
    }
    return m_state->committedLayoutRect(m_epoch, m_phase, m_updater, node);
}

Core::Result<UI::UINodeId>
PrimaryWindowUITreeUpdater::createElement(UI::UINodeId parent, const UI::UIElementDescriptor& descriptor)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::createElement");
    }
    return m_state->createElement(m_epoch, m_phase, m_updater, parent, descriptor);
}

Core::Result<UI::UIFlowLayerId> PrimaryWindowUITreeUpdater::registerFlowLayer(UI::UINodeId layer)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFlowLayerId>("PrimaryWindowUITreeUpdater::registerFlowLayer");
    }
    return m_state->registerFlowLayer(m_epoch, m_phase, m_updater, layer);
}

Core::Result<UI::UIFlowScreenId>
PrimaryWindowUITreeUpdater::registerFlowScreen(UI::UIFlowLayerId layer, UI::UINodeId screen)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFlowScreenId>("PrimaryWindowUITreeUpdater::registerFlowScreen");
    }
    return m_state->registerFlowScreen(m_epoch, m_phase, m_updater, layer, screen);
}

Core::Status PrimaryWindowUITreeUpdater::pushFlowScreen(UI::UIFlowScreenId screen)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::pushFlowScreen");
    }
    return m_state->pushFlowScreen(m_epoch, m_phase, m_updater, screen);
}

Core::Result<UI::UIFlowScreenId> PrimaryWindowUITreeUpdater::popFlowScreen(UI::UIFlowLayerId layer)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFlowScreenId>("PrimaryWindowUITreeUpdater::popFlowScreen");
    }
    return m_state->popFlowScreen(m_epoch, m_phase, m_updater, layer);
}

Core::Result<UI::UIFlowScreenId> PrimaryWindowUITreeUpdater::replaceFlowScreen(UI::UIFlowScreenId screen)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFlowScreenId>("PrimaryWindowUITreeUpdater::replaceFlowScreen");
    }
    return m_state->replaceFlowScreen(m_epoch, m_phase, m_updater, screen);
}

Core::Result<UI::UIFlowScreenId> PrimaryWindowUITreeUpdater::activeFlowScreen(UI::UIFlowLayerId layer) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFlowScreenId>("PrimaryWindowUITreeUpdater::activeFlowScreen");
    }
    return m_state->activeFlowScreen(m_epoch, m_phase, m_updater, layer);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isFlowScreenActive(UI::UIFlowScreenId screen) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isFlowScreenActive");
    }
    return m_state->isFlowScreenActive(m_epoch, m_phase, m_updater, screen);
}

Core::Status PrimaryWindowUITreeUpdater::assignFlowGamepad(
    Platform::GamepadId gamepad, UI::UIFlowLocalUserId localUser)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::assignFlowGamepad");
    }
    return m_state->assignFlowGamepad(m_epoch, m_phase, m_updater, gamepad,
                                      localUser);
}

Core::Status PrimaryWindowUITreeUpdater::clearFlowGamepadAssignment(
    Platform::GamepadId gamepad)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>(
            "PrimaryWindowUITreeUpdater::clearFlowGamepadAssignment");
    }
    return m_state->clearFlowGamepadAssignment(m_epoch, m_phase, m_updater,
                                               gamepad);
}

Core::Result<UI::UIFlowLocalUserId>
PrimaryWindowUITreeUpdater::flowLocalUserForGamepad(
    Platform::GamepadId gamepad) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFlowLocalUserId>(
            "PrimaryWindowUITreeUpdater::flowLocalUserForGamepad");
    }
    return m_state->flowLocalUserForGamepad(m_epoch, m_phase, m_updater,
                                            gamepad);
}

Core::Result<UI::UIFlowInputDeviceState>
PrimaryWindowUITreeUpdater::flowInputDeviceState(
    UI::UIFlowLocalUserId localUser) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFlowInputDeviceState>(
            "PrimaryWindowUITreeUpdater::flowInputDeviceState");
    }
    return m_state->flowInputDeviceState(m_epoch, m_phase, m_updater, localUser);
}

Core::Status PrimaryWindowUITreeUpdater::setFlowScreenAction(
    UI::UIFlowScreenId screen, UI::UIFlowAction action, UI::UIFlowActionCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setFlowScreenAction");
    }
    return m_state->setFlowScreenAction(m_epoch, m_phase, m_updater, screen, action,
                                        std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearFlowScreenAction(UI::UIFlowScreenId screen,
                                                               UI::UIFlowAction action)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearFlowScreenAction");
    }
    return m_state->clearFlowScreenAction(m_epoch, m_phase, m_updater, screen, action);
}

Core::Result<PrimaryWindowUIBuildTransaction>
PrimaryWindowUITreeUpdater::beginBuildTransaction(
    UI::UINodeId parent, const UI::UIElementDescriptor& rootDescriptor,
    UI::UIComponentBuildBudget budget)
{
    if (m_state == nullptr)
    {
        return expiredFacade<PrimaryWindowUIBuildTransaction>(
            "PrimaryWindowUITreeUpdater::beginBuildTransaction");
    }
    return m_state->beginBuildTransaction(
        m_epoch, m_phase, m_updater, parent, rootDescriptor, budget);
}

Core::Result<UI::UIIconButtonParts>
PrimaryWindowUITreeUpdater::buildIconButton(
    UI::UINodeId parent, const UI::UIIconButtonConfig& config)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIIconButtonParts>(
            "PrimaryWindowUITreeUpdater::buildIconButton");
    }
    return m_state->buildIconButton(m_epoch, m_phase, m_updater, parent, config);
}

Core::Result<UI::UIFormFieldParts>
PrimaryWindowUITreeUpdater::buildFormField(
    UI::UINodeId parent, const UI::UIFormFieldConfig& config)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFormFieldParts>(
            "PrimaryWindowUITreeUpdater::buildFormField");
    }
    return m_state->buildFormField(m_epoch, m_phase, m_updater, parent, config);
}

Core::Result<UI::UIDialogParts>
PrimaryWindowUITreeUpdater::buildDialog(
    UI::UINodeId parent, const UI::UIDialogConfig& config)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIDialogParts>(
            "PrimaryWindowUITreeUpdater::buildDialog");
    }
    return m_state->buildDialog(m_epoch, m_phase, m_updater, parent, config);
}

Core::Result<UI::UISnackbarHostParts>
PrimaryWindowUITreeUpdater::buildSnackbarHost(
    UI::UINodeId parent, const UI::UISnackbarHostConfig& config)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UISnackbarHostParts>(
            "PrimaryWindowUITreeUpdater::buildSnackbarHost");
    }
    return m_state->buildSnackbarHost(
        m_epoch, m_phase, m_updater, parent, config);
}

Core::Result<UI::UINumberFieldParts>
PrimaryWindowUITreeUpdater::buildNumberField(
    UI::UINodeId parent, const UI::UINumberFieldConfig& config)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINumberFieldParts>(
            "PrimaryWindowUITreeUpdater::buildNumberField");
    }
    return m_state->buildNumberField(m_epoch, m_phase, m_updater, parent, config);
}

Core::Result<UI::UICollapsibleSectionParts>
PrimaryWindowUITreeUpdater::buildCollapsibleSection(
    UI::UINodeId parent, const UI::UICollapsibleSectionConfig& config)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UICollapsibleSectionParts>(
            "PrimaryWindowUITreeUpdater::buildCollapsibleSection");
    }
    return m_state->buildCollapsibleSection(
        m_epoch, m_phase, m_updater, parent, config);
}

Core::Result<UI::UIColorFieldParts>
PrimaryWindowUITreeUpdater::buildColorField(
    UI::UINodeId parent, const UI::UIColorFieldConfig& config)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIColorFieldParts>(
            "PrimaryWindowUITreeUpdater::buildColorField");
    }
    return m_state->buildColorField(m_epoch, m_phase, m_updater, parent, config);
}

Core::Result<UI::UIColorPickerParts>
PrimaryWindowUITreeUpdater::buildColorPicker(
    UI::UINodeId parent, const UI::UIColorPickerConfig& config)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIColorPickerParts>(
            "PrimaryWindowUITreeUpdater::buildColorPicker");
    }
    return m_state->buildColorPicker(m_epoch, m_phase, m_updater, parent, config);
}

Core::Status PrimaryWindowUITreeUpdater::setLayoutStyle(UI::UINodeId node, const UI::UILayoutStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setLayoutStyle");
    }
    return m_state->setLayoutStyle(m_epoch, m_phase, m_updater, node, style);
}

Core::Status PrimaryWindowUITreeUpdater::setPointerHitPolicy(UI::UINodeId node, UI::UIPointerHitPolicy policy)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setPointerHitPolicy");
    }
    return m_state->setPointerHitPolicy(m_epoch, m_phase, m_updater, node, policy);
}

Core::Status PrimaryWindowUITreeUpdater::setEnabled(UI::UINodeId node, bool enabled)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setEnabled");
    }
    return m_state->setEnabled(m_epoch, m_phase, m_updater, node, enabled);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isEnabled(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isEnabled");
    }
    return m_state->isEnabled(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::setFocusScopeMode(UI::UINodeId node, UI::UIFocusScopeMode mode)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setFocusScopeMode");
    }
    return m_state->setFocusScopeMode(m_epoch, m_phase, m_updater, node, mode);
}

Core::Result<UI::UIFocusScopeMode> PrimaryWindowUITreeUpdater::focusScopeMode(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIFocusScopeMode>("PrimaryWindowUITreeUpdater::focusScopeMode");
    }
    return m_state->focusScopeMode(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::requestFocus(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::requestFocus");
    }
    return m_state->requestFocus(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::clearFocus()
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearFocus");
    }
    return m_state->clearFocus(m_epoch, m_phase, m_updater);
}

Core::Status PrimaryWindowUITreeUpdater::setStyleRole(UI::UINodeId node, UI::UIStyleRoleId role)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setStyleRole");
    }
    return m_state->setStyleRole(m_epoch, m_phase, m_updater, node, role);
}

Core::Result<UI::UIStyleRoleId> PrimaryWindowUITreeUpdater::styleRole(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIStyleRoleId>("PrimaryWindowUITreeUpdater::styleRole");
    }
    return m_state->styleRole(m_epoch, m_phase, m_updater, node);
}

Core::Result<UI::UIStraightSrgba8Color>
PrimaryWindowUITreeUpdater::styleColorToken(UI::UIStyleTokenId token) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIStraightSrgba8Color>(
            "PrimaryWindowUITreeUpdater::styleColorToken");
    }
    return m_state->styleColorToken(m_epoch, m_phase, token);
}

Core::Status PrimaryWindowUITreeUpdater::setStyleColorToken(
    UI::UIStyleTokenId token, UI::UIStraightSrgba8Color value)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>(
            "PrimaryWindowUITreeUpdater::setStyleColorToken");
    }
    return m_state->setStyleColorToken(m_epoch, m_phase, token, value);
}

Core::Status PrimaryWindowUITreeUpdater::clearOverride(UI::UINodeId node, UI::UIStyleOverride properties)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearOverride");
    }
    return m_state->clearOverride(m_epoch, m_phase, m_updater, node, properties);
}

Core::Result<UI::UITheme> PrimaryWindowUITreeUpdater::productTheme() const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITheme>("PrimaryWindowUITreeUpdater::productTheme");
    }
    return m_state->productTheme(m_epoch, m_phase);
}

Core::Status PrimaryWindowUITreeUpdater::setProductTheme(const UI::UITheme& theme)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProductTheme");
    }
    return m_state->setProductTheme(m_epoch, m_phase, theme);
}

Core::Status PrimaryWindowUITreeUpdater::setBoxPaint(UI::UINodeId node, const UI::UIBoxPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setBoxPaint");
    }
    return m_state->setBoxPaint(m_epoch, m_phase, m_updater, node, paint);
}

Core::Status PrimaryWindowUITreeUpdater::setImageTint(UI::UINodeId node, UI::UIStraightSrgba8Color tint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setImageTint");
    }
    return m_state->setImageTint(m_epoch, m_phase, m_updater, node, tint);
}

Core::Result<UI::UIStraightSrgba8Color> PrimaryWindowUITreeUpdater::imageTint(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIStraightSrgba8Color>("PrimaryWindowUITreeUpdater::imageTint");
    }
    return m_state->imageTint(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::setReducedMotion(bool enabled)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setReducedMotion");
    }
    return m_state->setReducedMotion(m_epoch, m_phase, enabled);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::reducedMotion() const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::reducedMotion");
    }
    return m_state->reducedMotion(m_epoch, m_phase);
}

Core::Status PrimaryWindowUITreeUpdater::setStyleBackgroundColorTransition(const UI::UITransitionSpec& spec)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setStyleBackgroundColorTransition");
    }
    return m_state->setStyleBackgroundColorTransition(m_epoch, m_phase, spec);
}

Core::Result<UI::UITransitionSpec> PrimaryWindowUITreeUpdater::styleBackgroundColorTransition() const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITransitionSpec>("PrimaryWindowUITreeUpdater::styleBackgroundColorTransition");
    }
    return m_state->styleBackgroundColorTransition(m_epoch, m_phase);
}

Core::Status PrimaryWindowUITreeUpdater::beginBackgroundColorTransition(
    UI::UINodeId node, UI::UIStraightSrgba8Color target, const UI::UITransitionSpec& spec)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::beginBackgroundColorTransition");
    }
    return m_state->beginBackgroundColorTransition(m_epoch, m_phase, node, target, spec);
}

Core::Status PrimaryWindowUITreeUpdater::beginBorderColorTransition(
    UI::UINodeId node, UI::UIStraightSrgba8Color target, const UI::UITransitionSpec& spec)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::beginBorderColorTransition");
    }
    return m_state->beginBorderColorTransition(m_epoch, m_phase, node, target, spec);
}

Core::Status PrimaryWindowUITreeUpdater::beginTextColorTransition(
    UI::UINodeId node, UI::UIStraightSrgba8Color target, const UI::UITransitionSpec& spec)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::beginTextColorTransition");
    }
    return m_state->beginTextColorTransition(m_epoch, m_phase, node, target, spec);
}

Core::Status PrimaryWindowUITreeUpdater::beginOpacityTransition(
    UI::UINodeId node, float targetOpacity, const UI::UITransitionSpec& spec)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::beginOpacityTransition");
    }
    return m_state->beginOpacityTransition(m_epoch, m_phase, node, targetOpacity, spec);
}

Core::Status PrimaryWindowUITreeUpdater::beginCornerRadiusTransition(
    UI::UINodeId node, float targetRadius, const UI::UITransitionSpec& spec)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::beginCornerRadiusTransition");
    }
    return m_state->beginCornerRadiusTransition(m_epoch, m_phase, node, targetRadius, spec);
}

Core::Status PrimaryWindowUITreeUpdater::beginVisualOffsetTransition(
    UI::UINodeId node, float targetOffsetX, float targetOffsetY, const UI::UITransitionSpec& spec)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::beginVisualOffsetTransition");
    }
    return m_state->beginVisualOffsetTransition(m_epoch, m_phase, node, targetOffsetX, targetOffsetY, spec);
}

Core::Result<UI::UITimelineId>
PrimaryWindowUITreeUpdater::createTimeline(const UI::UITimelineDesc& desc)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITimelineId>("PrimaryWindowUITreeUpdater::createTimeline");
    }
    return m_state->createTimeline(m_epoch, m_phase, desc);
}

Core::Status PrimaryWindowUITreeUpdater::replaceTimeline(
    UI::UITimelineId timeline, const UI::UITimelineDesc& desc)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::replaceTimeline");
    }
    return m_state->replaceTimeline(m_epoch, m_phase, timeline, desc);
}

Core::Status PrimaryWindowUITreeUpdater::playTimeline(UI::UITimelineId timeline)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::playTimeline");
    }
    return m_state->playTimeline(m_epoch, m_phase, timeline);
}

Core::Status PrimaryWindowUITreeUpdater::cancelTimeline(UI::UITimelineId timeline)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::cancelTimeline");
    }
    return m_state->cancelTimeline(m_epoch, m_phase, timeline);
}

Core::Status PrimaryWindowUITreeUpdater::destroyTimeline(UI::UITimelineId timeline)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::destroyTimeline");
    }
    return m_state->destroyTimeline(m_epoch, m_phase, timeline);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isTimelineActive(UI::UITimelineId timeline) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isTimelineActive");
    }
    return m_state->isTimelineActive(m_epoch, m_phase, timeline);
}

Core::Status PrimaryWindowUITreeUpdater::setButtonPaint(UI::UINodeId button, const UI::UIButtonPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setButtonPaint");
    }
    return m_state->setButtonPaint(m_epoch, m_phase, m_updater, button, paint);
}

Core::Result<UI::UIButtonPaint> PrimaryWindowUITreeUpdater::buttonPaint(UI::UINodeId button) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIButtonPaint>("PrimaryWindowUITreeUpdater::buttonPaint");
    }
    return m_state->buttonPaint(m_epoch, m_phase, m_updater, button);
}

Core::Status PrimaryWindowUITreeUpdater::setText(UI::UINodeId node, std::string_view utf8)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setText");
    }
    return m_state->setText(m_epoch, m_phase, m_updater, node, utf8);
}

Core::Status PrimaryWindowUITreeUpdater::setTextStyle(UI::UINodeId node, const UI::UITextStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTextStyle");
    }
    return m_state->setTextStyle(m_epoch, m_phase, m_updater, node, style);
}

Core::Status PrimaryWindowUITreeUpdater::setTextOverflow(UI::UINodeId node,
                                                         UI::UITextOverflow overflow)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTextOverflow");
    }
    return m_state->setTextOverflow(m_epoch, m_phase, m_updater, node, overflow);
}

Core::Result<UI::UITextOverflow> PrimaryWindowUITreeUpdater::textOverflow(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITextOverflow>("PrimaryWindowUITreeUpdater::textOverflow");
    }
    return m_state->textOverflow(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::setContentAlignment(UI::UINodeId node,
                                                              UI::UIContentAlignment alignment)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setContentAlignment");
    }
    return m_state->setContentAlignment(m_epoch, m_phase, m_updater, node, alignment);
}

Core::Result<std::string_view> PrimaryWindowUITreeUpdater::text(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<std::string_view>("PrimaryWindowUITreeUpdater::text");
    }
    return m_state->text(m_epoch, m_phase, m_updater, node);
}

Core::Result<UI::UITextStyle> PrimaryWindowUITreeUpdater::textStyle(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITextStyle>("PrimaryWindowUITreeUpdater::textStyle");
    }
    return m_state->textStyle(m_epoch, m_phase, m_updater, node);
}

Core::Result<UI::UIContentAlignment> PrimaryWindowUITreeUpdater::contentAlignment(UI::UINodeId node) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIContentAlignment>("PrimaryWindowUITreeUpdater::contentAlignment");
    }
    return m_state->contentAlignment(m_epoch, m_phase, m_updater, node);
}

Core::Status PrimaryWindowUITreeUpdater::setTextSelection(UI::UINodeId textEdit, UI::UITextSelection selection)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTextSelection");
    }
    return m_state->setTextSelection(m_epoch, m_phase, m_updater, textEdit, selection);
}

Core::Result<UI::UITextSelection> PrimaryWindowUITreeUpdater::textSelection(UI::UINodeId textEdit) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITextSelection>("PrimaryWindowUITreeUpdater::textSelection");
    }
    return m_state->textSelection(m_epoch, m_phase, m_updater, textEdit);
}

Core::Status PrimaryWindowUITreeUpdater::setTextEditPaint(UI::UINodeId textEdit,
                                                          const UI::UITextEditPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTextEditPaint");
    }
    return m_state->setTextEditPaint(m_epoch, m_phase, m_updater, textEdit, paint);
}

Core::Result<UI::UITextEditPaint> PrimaryWindowUITreeUpdater::textEditPaint(UI::UINodeId textEdit) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITextEditPaint>("PrimaryWindowUITreeUpdater::textEditPaint");
    }
    return m_state->textEditPaint(m_epoch, m_phase, m_updater, textEdit);
}

Core::Status PrimaryWindowUITreeUpdater::setButtonAction(UI::UINodeId button, UI::UIButtonActionCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setButtonAction");
    }
    return m_state->setButtonAction(m_epoch, m_phase, m_updater, button, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearButtonAction(UI::UINodeId button)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearButtonAction");
    }
    return m_state->clearButtonAction(m_epoch, m_phase, m_updater, button);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isButtonPressed(UI::UINodeId button) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isButtonPressed");
    }
    return m_state->isButtonPressed(m_epoch, m_phase, m_updater, button);
}

Core::Status PrimaryWindowUITreeUpdater::setCheckboxAction(UI::UINodeId checkbox, UI::UIButtonActionCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setCheckboxAction");
    }
    return m_state->setCheckboxAction(m_epoch, m_phase, m_updater, checkbox, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearCheckboxAction(UI::UINodeId checkbox)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearCheckboxAction");
    }
    return m_state->clearCheckboxAction(m_epoch, m_phase, m_updater, checkbox);
}

Core::Status PrimaryWindowUITreeUpdater::setCheckboxPaint(UI::UINodeId checkbox, const UI::UICheckboxPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setCheckboxPaint");
    }
    return m_state->setCheckboxPaint(m_epoch, m_phase, m_updater, checkbox, paint);
}

Core::Result<UI::UICheckboxPaint> PrimaryWindowUITreeUpdater::checkboxPaint(UI::UINodeId checkbox) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UICheckboxPaint>("PrimaryWindowUITreeUpdater::checkboxPaint");
    }
    return m_state->checkboxPaint(m_epoch, m_phase, m_updater, checkbox);
}

Core::Status PrimaryWindowUITreeUpdater::setChecked(UI::UINodeId checkbox, bool checked)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setChecked");
    }
    return m_state->setChecked(m_epoch, m_phase, m_updater, checkbox, checked);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isChecked(UI::UINodeId checkbox) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isChecked");
    }
    return m_state->isChecked(m_epoch, m_phase, m_updater, checkbox);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isCheckboxPressed(UI::UINodeId checkbox) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isCheckboxPressed");
    }
    return m_state->isCheckboxPressed(m_epoch, m_phase, m_updater, checkbox);
}

Core::Status PrimaryWindowUITreeUpdater::setSliderRange(UI::UINodeId slider, float minValue, float maxValue, float step)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSliderRange");
    }
    return m_state->setSliderRange(m_epoch, m_phase, m_updater, slider, minValue, maxValue, step);
}

Core::Status PrimaryWindowUITreeUpdater::setSliderValue(UI::UINodeId slider, float value)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSliderValue");
    }
    return m_state->setSliderValue(m_epoch, m_phase, m_updater, slider, value);
}

Core::Result<float> PrimaryWindowUITreeUpdater::sliderValue(UI::UINodeId slider) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<float>("PrimaryWindowUITreeUpdater::sliderValue");
    }
    return m_state->sliderValue(m_epoch, m_phase, m_updater, slider);
}

Core::Status PrimaryWindowUITreeUpdater::setSliderPaint(UI::UINodeId slider, const UI::UISliderPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSliderPaint");
    }
    return m_state->setSliderPaint(m_epoch, m_phase, m_updater, slider, paint);
}

Core::Result<UI::UISliderPaint> PrimaryWindowUITreeUpdater::sliderPaint(UI::UINodeId slider) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UISliderPaint>("PrimaryWindowUITreeUpdater::sliderPaint");
    }
    return m_state->sliderPaint(m_epoch, m_phase, m_updater, slider);
}

Core::Status PrimaryWindowUITreeUpdater::setSliderChangeCallback(UI::UINodeId slider,
                                                                 UI::UISliderChangeCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSliderChangeCallback");
    }
    return m_state->setSliderChangeCallback(m_epoch, m_phase, m_updater, slider, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearSliderChangeCallback(UI::UINodeId slider)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearSliderChangeCallback");
    }
    return m_state->clearSliderChangeCallback(m_epoch, m_phase, m_updater, slider);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isSliderDragging(UI::UINodeId slider) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isSliderDragging");
    }
    return m_state->isSliderDragging(m_epoch, m_phase, m_updater, slider);
}

Core::Status PrimaryWindowUITreeUpdater::setSplitViewParts(
    UI::UINodeId splitView, UI::UINodeId primaryPane, UI::UINodeId splitter,
    UI::UINodeId secondaryPane)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSplitViewParts");
    }
    return m_state->setSplitViewParts(m_epoch, m_phase, m_updater, splitView,
                                      primaryPane, splitter, secondaryPane);
}

Core::Status PrimaryWindowUITreeUpdater::clearSplitViewParts(UI::UINodeId splitView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearSplitViewParts");
    }
    return m_state->clearSplitViewParts(m_epoch, m_phase, m_updater, splitView);
}

Core::Result<UI::UISplitViewParts>
PrimaryWindowUITreeUpdater::splitViewParts(UI::UINodeId splitView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UISplitViewParts>(
            "PrimaryWindowUITreeUpdater::splitViewParts");
    }
    return m_state->splitViewParts(m_epoch, m_phase, m_updater, splitView);
}

Core::Status PrimaryWindowUITreeUpdater::setSplitViewFraction(
    UI::UINodeId splitView, float fraction)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setSplitViewFraction");
    }
    return m_state->setSplitViewFraction(m_epoch, m_phase, m_updater, splitView, fraction);
}

Core::Result<float>
PrimaryWindowUITreeUpdater::splitViewFraction(UI::UINodeId splitView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<float>("PrimaryWindowUITreeUpdater::splitViewFraction");
    }
    return m_state->splitViewFraction(m_epoch, m_phase, m_updater, splitView);
}

Core::Result<UI::UISplitViewMetrics>
PrimaryWindowUITreeUpdater::splitViewMetrics(UI::UINodeId splitView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UISplitViewMetrics>(
            "PrimaryWindowUITreeUpdater::splitViewMetrics");
    }
    return m_state->splitViewMetrics(m_epoch, m_phase, m_updater, splitView);
}

Core::Result<bool>
PrimaryWindowUITreeUpdater::isSplitterDragging(UI::UINodeId splitter) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isSplitterDragging");
    }
    return m_state->isSplitterDragging(m_epoch, m_phase, m_updater, splitter);
}

Core::Status PrimaryWindowUITreeUpdater::setSplitterPaint(
    UI::UINodeId splitter, const UI::UISplitterPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>(
            "PrimaryWindowUITreeUpdater::setSplitterPaint");
    }
    return m_state->setSplitterPaint(
        m_epoch, m_phase, m_updater, splitter, paint);
}

Core::Result<UI::UISplitterPaint>
PrimaryWindowUITreeUpdater::splitterPaint(UI::UINodeId splitter) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UISplitterPaint>(
            "PrimaryWindowUITreeUpdater::splitterPaint");
    }
    return m_state->splitterPaint(m_epoch, m_phase, m_updater, splitter);
}

Core::Status PrimaryWindowUITreeUpdater::setTabViewItems(
    UI::UINodeId tabView, std::span<const UI::UITabViewItem> items, u32 activeIndex)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTabViewItems");
    }
    return m_state->setTabViewItems(m_epoch, m_phase, m_updater, tabView, items, activeIndex);
}

Core::Status PrimaryWindowUITreeUpdater::clearTabViewItems(UI::UINodeId tabView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearTabViewItems");
    }
    return m_state->clearTabViewItems(m_epoch, m_phase, m_updater, tabView);
}

Core::Result<u32> PrimaryWindowUITreeUpdater::tabViewItemCount(UI::UINodeId tabView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<u32>("PrimaryWindowUITreeUpdater::tabViewItemCount");
    }
    return m_state->tabViewItemCount(m_epoch, m_phase, m_updater, tabView);
}

Core::Result<UI::UITabViewItem> PrimaryWindowUITreeUpdater::tabViewItemAt(
    UI::UINodeId tabView, u32 index) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITabViewItem>("PrimaryWindowUITreeUpdater::tabViewItemAt");
    }
    return m_state->tabViewItemAt(m_epoch, m_phase, m_updater, tabView, index);
}

Core::Status PrimaryWindowUITreeUpdater::setTabViewActiveTab(
    UI::UINodeId tabView, UI::UINodeId tab)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTabViewActiveTab");
    }
    return m_state->setTabViewActiveTab(m_epoch, m_phase, m_updater, tabView, tab);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::tabViewActiveTab(
    UI::UINodeId tabView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::tabViewActiveTab");
    }
    return m_state->tabViewActiveTab(m_epoch, m_phase, m_updater, tabView);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::tabViewActivePanel(
    UI::UINodeId tabView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::tabViewActivePanel");
    }
    return m_state->tabViewActivePanel(m_epoch, m_phase, m_updater, tabView);
}

Core::Result<UI::UITabViewMetrics> PrimaryWindowUITreeUpdater::tabViewMetrics(
    UI::UINodeId tabView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITabViewMetrics>("PrimaryWindowUITreeUpdater::tabViewMetrics");
    }
    return m_state->tabViewMetrics(m_epoch, m_phase, m_updater, tabView);
}

Core::Result<UI::UITabViewCommandResult> PrimaryWindowUITreeUpdater::routeTabViewCommand(
    UI::UINodeId tabView, UI::UITabViewCommand command)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITabViewCommandResult>(
            "PrimaryWindowUITreeUpdater::routeTabViewCommand");
    }
    return m_state->routeTabViewCommand(m_epoch, m_phase, m_updater, tabView, command);
}

Core::Status PrimaryWindowUITreeUpdater::setTabPaint(
    UI::UINodeId tab, const UI::UITabPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTabPaint");
    }
    return m_state->setTabPaint(m_epoch, m_phase, m_updater, tab, paint);
}

Core::Result<UI::UITabPaint> PrimaryWindowUITreeUpdater::tabPaint(UI::UINodeId tab) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITabPaint>("PrimaryWindowUITreeUpdater::tabPaint");
    }
    return m_state->tabPaint(m_epoch, m_phase, m_updater, tab);
}

Core::Status PrimaryWindowUITreeUpdater::setScrollViewStyle(UI::UINodeId scrollView,
                                                           const UI::UIScrollViewStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setScrollViewStyle");
    }
    return m_state->setScrollViewStyle(m_epoch, m_phase, m_updater, scrollView, style);
}

Core::Result<UI::UIScrollViewStyle> PrimaryWindowUITreeUpdater::scrollViewStyle(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIScrollViewStyle>("PrimaryWindowUITreeUpdater::scrollViewStyle");
    }
    return m_state->scrollViewStyle(m_epoch, m_phase, m_updater, scrollView);
}

Core::Status PrimaryWindowUITreeUpdater::setScrollViewOffset(UI::UINodeId scrollView, UI::UIScrollOffset offset)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setScrollViewOffset");
    }
    return m_state->setScrollViewOffset(m_epoch, m_phase, m_updater, scrollView, offset);
}

Core::Result<UI::UIScrollOffset> PrimaryWindowUITreeUpdater::scrollViewOffset(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIScrollOffset>("PrimaryWindowUITreeUpdater::scrollViewOffset");
    }
    return m_state->scrollViewOffset(m_epoch, m_phase, m_updater, scrollView);
}

Core::Result<UI::UIScrollViewMetrics> PrimaryWindowUITreeUpdater::scrollViewMetrics(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIScrollViewMetrics>("PrimaryWindowUITreeUpdater::scrollViewMetrics");
    }
    return m_state->scrollViewMetrics(m_epoch, m_phase, m_updater, scrollView);
}

Core::Status PrimaryWindowUITreeUpdater::setScrollViewPaint(UI::UINodeId scrollView,
                                                           const UI::UIScrollViewPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setScrollViewPaint");
    }
    return m_state->setScrollViewPaint(m_epoch, m_phase, m_updater, scrollView, paint);
}

Core::Result<UI::UIScrollViewPaint> PrimaryWindowUITreeUpdater::scrollViewPaint(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIScrollViewPaint>("PrimaryWindowUITreeUpdater::scrollViewPaint");
    }
    return m_state->scrollViewPaint(m_epoch, m_phase, m_updater, scrollView);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isScrollViewDragging(UI::UINodeId scrollView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isScrollViewDragging");
    }
    return m_state->isScrollViewDragging(m_epoch, m_phase, m_updater, scrollView);
}

Core::Status PrimaryWindowUITreeUpdater::setListViewDataSource(UI::UINodeId listView,
                                                               UI::UIListViewDataSource source)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setListViewDataSource");
    }
    return m_state->setListViewDataSource(m_epoch, m_phase, m_updater, listView, source);
}

Core::Status PrimaryWindowUITreeUpdater::clearListViewDataSource(UI::UINodeId listView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearListViewDataSource");
    }
    return m_state->clearListViewDataSource(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::invalidateListViewItems(UI::UINodeId listView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::invalidateListViewItems");
    }
    return m_state->invalidateListViewItems(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::setListViewStyle(UI::UINodeId listView,
                                                          const UI::UIListViewStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setListViewStyle");
    }
    return m_state->setListViewStyle(m_epoch, m_phase, m_updater, listView, style);
}

Core::Result<UI::UIListViewStyle> PrimaryWindowUITreeUpdater::listViewStyle(UI::UINodeId listView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIListViewStyle>("PrimaryWindowUITreeUpdater::listViewStyle");
    }
    return m_state->listViewStyle(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::setListViewPaint(UI::UINodeId listView,
                                                          const UI::UIListViewPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setListViewPaint");
    }
    return m_state->setListViewPaint(m_epoch, m_phase, m_updater, listView, paint);
}

Core::Result<UI::UIListViewPaint> PrimaryWindowUITreeUpdater::listViewPaint(UI::UINodeId listView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIListViewPaint>("PrimaryWindowUITreeUpdater::listViewPaint");
    }
    return m_state->listViewPaint(m_epoch, m_phase, m_updater, listView);
}

Core::Result<UI::UIListViewMetrics> PrimaryWindowUITreeUpdater::listViewMetrics(UI::UINodeId listView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIListViewMetrics>("PrimaryWindowUITreeUpdater::listViewMetrics");
    }
    return m_state->listViewMetrics(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::setListViewSelectedIndex(UI::UINodeId listView, u64 logicalIndex)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setListViewSelectedIndex");
    }
    return m_state->setListViewSelectedIndex(m_epoch, m_phase, m_updater, listView, logicalIndex);
}

Core::Status PrimaryWindowUITreeUpdater::clearListViewSelection(UI::UINodeId listView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearListViewSelection");
    }
    return m_state->clearListViewSelection(m_epoch, m_phase, m_updater, listView);
}

Core::Result<UI::UIListViewSelection> PrimaryWindowUITreeUpdater::listViewSelection(UI::UINodeId listView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIListViewSelection>("PrimaryWindowUITreeUpdater::listViewSelection");
    }
    return m_state->listViewSelection(m_epoch, m_phase, m_updater, listView);
}

Core::Status PrimaryWindowUITreeUpdater::scrollListViewToIndex(UI::UINodeId listView, u64 logicalIndex,
                                                               UI::UIListViewScrollAlignment alignment)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::scrollListViewToIndex");
    }
    return m_state->scrollListViewToIndex(m_epoch, m_phase, m_updater, listView, logicalIndex, alignment);
}

Core::Status PrimaryWindowUITreeUpdater::setVirtualGridViewDataSource(
    UI::UINodeId virtualGridView, UI::UIVirtualGridViewDataSource source)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setVirtualGridViewDataSource");
    return m_state->setVirtualGridViewDataSource(m_epoch, m_phase, m_updater, virtualGridView, source);
}

Core::Status PrimaryWindowUITreeUpdater::clearVirtualGridViewDataSource(UI::UINodeId virtualGridView)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearVirtualGridViewDataSource");
    return m_state->clearVirtualGridViewDataSource(m_epoch, m_phase, m_updater, virtualGridView);
}

Core::Status PrimaryWindowUITreeUpdater::invalidateVirtualGridViewItems(UI::UINodeId virtualGridView)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::invalidateVirtualGridViewItems");
    return m_state->invalidateVirtualGridViewItems(m_epoch, m_phase, m_updater, virtualGridView);
}

Core::Status PrimaryWindowUITreeUpdater::setVirtualGridViewStyle(
    UI::UINodeId virtualGridView, const UI::UIVirtualGridViewStyle& style)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setVirtualGridViewStyle");
    return m_state->setVirtualGridViewStyle(m_epoch, m_phase, m_updater, virtualGridView, style);
}

Core::Result<UI::UIVirtualGridViewStyle>
PrimaryWindowUITreeUpdater::virtualGridViewStyle(UI::UINodeId virtualGridView) const
{
    if (m_state == nullptr)
        return expiredFacade<UI::UIVirtualGridViewStyle>("PrimaryWindowUITreeUpdater::virtualGridViewStyle");
    return m_state->virtualGridViewStyle(m_epoch, m_phase, m_updater, virtualGridView);
}

Core::Status PrimaryWindowUITreeUpdater::setVirtualGridViewPaint(
    UI::UINodeId virtualGridView, const UI::UIVirtualGridViewPaint& paint)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setVirtualGridViewPaint");
    return m_state->setVirtualGridViewPaint(m_epoch, m_phase, m_updater, virtualGridView, paint);
}

Core::Result<UI::UIVirtualGridViewPaint>
PrimaryWindowUITreeUpdater::virtualGridViewPaint(UI::UINodeId virtualGridView) const
{
    if (m_state == nullptr)
        return expiredFacade<UI::UIVirtualGridViewPaint>("PrimaryWindowUITreeUpdater::virtualGridViewPaint");
    return m_state->virtualGridViewPaint(m_epoch, m_phase, m_updater, virtualGridView);
}

Core::Result<UI::UIVirtualGridViewMetrics>
PrimaryWindowUITreeUpdater::virtualGridViewMetrics(UI::UINodeId virtualGridView) const
{
    if (m_state == nullptr)
        return expiredFacade<UI::UIVirtualGridViewMetrics>("PrimaryWindowUITreeUpdater::virtualGridViewMetrics");
    return m_state->virtualGridViewMetrics(m_epoch, m_phase, m_updater, virtualGridView);
}

Core::Result<UI::UINodeId>
PrimaryWindowUITreeUpdater::virtualGridViewMaterializedItemNode(
    UI::UINodeId virtualGridView, u64 logicalIndex) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>(
            "PrimaryWindowUITreeUpdater::virtualGridViewMaterializedItemNode");
    }
    return m_state->virtualGridViewMaterializedItemNode(
        m_epoch, m_phase, m_updater, virtualGridView, logicalIndex);
}

Core::Status PrimaryWindowUITreeUpdater::setVirtualGridViewSelectedIndex(
    UI::UINodeId virtualGridView, u64 logicalIndex)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setVirtualGridViewSelectedIndex");
    return m_state->setVirtualGridViewSelectedIndex(m_epoch, m_phase, m_updater, virtualGridView, logicalIndex);
}

Core::Status PrimaryWindowUITreeUpdater::clearVirtualGridViewSelection(UI::UINodeId virtualGridView)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearVirtualGridViewSelection");
    return m_state->clearVirtualGridViewSelection(m_epoch, m_phase, m_updater, virtualGridView);
}

Core::Result<UI::UIVirtualGridViewSelection>
PrimaryWindowUITreeUpdater::virtualGridViewSelection(UI::UINodeId virtualGridView) const
{
    if (m_state == nullptr)
        return expiredFacade<UI::UIVirtualGridViewSelection>("PrimaryWindowUITreeUpdater::virtualGridViewSelection");
    return m_state->virtualGridViewSelection(m_epoch, m_phase, m_updater, virtualGridView);
}

Core::Status PrimaryWindowUITreeUpdater::scrollVirtualGridViewToIndex(
    UI::UINodeId virtualGridView, u64 logicalIndex,
    UI::UIVirtualGridViewScrollAlignment alignment)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::scrollVirtualGridViewToIndex");
    return m_state->scrollVirtualGridViewToIndex(
        m_epoch, m_phase, m_updater, virtualGridView, logicalIndex, alignment);
}

Core::Status PrimaryWindowUITreeUpdater::setDataGridDataSource(
    UI::UINodeId dataGrid, UI::UIDataGridDataSource source)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDataGridDataSource");
    return m_state->setDataGridDataSource(
        m_epoch, m_phase, m_updater, dataGrid, source);
}

Core::Status PrimaryWindowUITreeUpdater::clearDataGridDataSource(
    UI::UINodeId dataGrid)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearDataGridDataSource");
    return m_state->clearDataGridDataSource(
        m_epoch, m_phase, m_updater, dataGrid);
}

Core::Status PrimaryWindowUITreeUpdater::invalidateDataGridItems(
    UI::UINodeId dataGrid)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::invalidateDataGridItems");
    return m_state->invalidateDataGridItems(
        m_epoch, m_phase, m_updater, dataGrid);
}

Core::Status PrimaryWindowUITreeUpdater::setDataGridStyle(
    UI::UINodeId dataGrid, const UI::UIDataGridStyle& style)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDataGridStyle");
    return m_state->setDataGridStyle(
        m_epoch, m_phase, m_updater, dataGrid, style);
}

Core::Result<UI::UIDataGridStyle>
PrimaryWindowUITreeUpdater::dataGridStyle(UI::UINodeId dataGrid) const
{
    if (m_state == nullptr)
        return expiredFacade<UI::UIDataGridStyle>(
            "PrimaryWindowUITreeUpdater::dataGridStyle");
    return m_state->dataGridStyle(
        m_epoch, m_phase, m_updater, dataGrid);
}

Core::Status PrimaryWindowUITreeUpdater::setDataGridPaint(
    UI::UINodeId dataGrid, const UI::UIDataGridPaint& paint)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDataGridPaint");
    return m_state->setDataGridPaint(
        m_epoch, m_phase, m_updater, dataGrid, paint);
}

Core::Result<UI::UIDataGridPaint>
PrimaryWindowUITreeUpdater::dataGridPaint(UI::UINodeId dataGrid) const
{
    if (m_state == nullptr)
        return expiredFacade<UI::UIDataGridPaint>(
            "PrimaryWindowUITreeUpdater::dataGridPaint");
    return m_state->dataGridPaint(
        m_epoch, m_phase, m_updater, dataGrid);
}

Core::Result<UI::UIDataGridMetrics>
PrimaryWindowUITreeUpdater::dataGridMetrics(UI::UINodeId dataGrid) const
{
    if (m_state == nullptr)
        return expiredFacade<UI::UIDataGridMetrics>(
            "PrimaryWindowUITreeUpdater::dataGridMetrics");
    return m_state->dataGridMetrics(
        m_epoch, m_phase, m_updater, dataGrid);
}

Core::Status PrimaryWindowUITreeUpdater::setDataGridSelectedCell(
    UI::UINodeId dataGrid, u64 logicalRow, u32 logicalColumn)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDataGridSelectedCell");
    return m_state->setDataGridSelectedCell(
        m_epoch, m_phase, m_updater, dataGrid, logicalRow, logicalColumn);
}

Core::Status PrimaryWindowUITreeUpdater::clearDataGridSelection(
    UI::UINodeId dataGrid)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearDataGridSelection");
    return m_state->clearDataGridSelection(
        m_epoch, m_phase, m_updater, dataGrid);
}

Core::Result<UI::UIDataGridSelection>
PrimaryWindowUITreeUpdater::dataGridSelection(UI::UINodeId dataGrid) const
{
    if (m_state == nullptr)
        return expiredFacade<UI::UIDataGridSelection>(
            "PrimaryWindowUITreeUpdater::dataGridSelection");
    return m_state->dataGridSelection(
        m_epoch, m_phase, m_updater, dataGrid);
}

Core::Status PrimaryWindowUITreeUpdater::scrollDataGridToCell(
    UI::UINodeId dataGrid, u64 logicalRow, u32 logicalColumn,
    UI::UIDataGridScrollAlignment alignment)
{
    if (m_state == nullptr)
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::scrollDataGridToCell");
    return m_state->scrollDataGridToCell(
        m_epoch, m_phase, m_updater, dataGrid, logicalRow, logicalColumn,
        alignment);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewDataSource(UI::UINodeId treeView,
                                                               UI::UITreeViewDataSource source)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewDataSource");
    }
    return m_state->setTreeViewDataSource(m_epoch, m_phase, m_updater, treeView, source);
}

Core::Status PrimaryWindowUITreeUpdater::clearTreeViewDataSource(UI::UINodeId treeView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearTreeViewDataSource");
    }
    return m_state->clearTreeViewDataSource(m_epoch, m_phase, m_updater, treeView);
}

Core::Status PrimaryWindowUITreeUpdater::invalidateTreeViewItems(UI::UINodeId treeView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::invalidateTreeViewItems");
    }
    return m_state->invalidateTreeViewItems(m_epoch, m_phase, m_updater, treeView);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewStyle(UI::UINodeId treeView,
                                                          const UI::UITreeViewStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewStyle");
    }
    return m_state->setTreeViewStyle(m_epoch, m_phase, m_updater, treeView, style);
}

Core::Result<UI::UITreeViewStyle> PrimaryWindowUITreeUpdater::treeViewStyle(UI::UINodeId treeView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITreeViewStyle>("PrimaryWindowUITreeUpdater::treeViewStyle");
    }
    return m_state->treeViewStyle(m_epoch, m_phase, m_updater, treeView);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewPaint(UI::UINodeId treeView,
                                                          const UI::UITreeViewPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewPaint");
    }
    return m_state->setTreeViewPaint(m_epoch, m_phase, m_updater, treeView, paint);
}

Core::Result<UI::UITreeViewPaint> PrimaryWindowUITreeUpdater::treeViewPaint(UI::UINodeId treeView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITreeViewPaint>("PrimaryWindowUITreeUpdater::treeViewPaint");
    }
    return m_state->treeViewPaint(m_epoch, m_phase, m_updater, treeView);
}

Core::Result<UI::UITreeViewMetrics> PrimaryWindowUITreeUpdater::treeViewMetrics(UI::UINodeId treeView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITreeViewMetrics>("PrimaryWindowUITreeUpdater::treeViewMetrics");
    }
    return m_state->treeViewMetrics(m_epoch, m_phase, m_updater, treeView);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::treeViewMaterializedItemNode(
    UI::UINodeId treeView, u64 logicalIndex) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>(
            "PrimaryWindowUITreeUpdater::treeViewMaterializedItemNode");
    }
    return m_state->treeViewMaterializedItemNode(
        m_epoch, m_phase, m_updater, treeView, logicalIndex);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewSelectedIndex(UI::UINodeId treeView, u64 logicalIndex)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewSelectedIndex");
    }
    return m_state->setTreeViewSelectedIndex(m_epoch, m_phase, m_updater, treeView, logicalIndex);
}

Core::Status PrimaryWindowUITreeUpdater::clearTreeViewSelection(UI::UINodeId treeView)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearTreeViewSelection");
    }
    return m_state->clearTreeViewSelection(m_epoch, m_phase, m_updater, treeView);
}

Core::Result<UI::UITreeViewSelection> PrimaryWindowUITreeUpdater::treeViewSelection(UI::UINodeId treeView) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITreeViewSelection>("PrimaryWindowUITreeUpdater::treeViewSelection");
    }
    return m_state->treeViewSelection(m_epoch, m_phase, m_updater, treeView);
}

Core::Status PrimaryWindowUITreeUpdater::setTreeViewItemExpanded(UI::UINodeId treeView, u64 logicalIndex,
                                                                 bool expanded)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTreeViewItemExpanded");
    }
    return m_state->setTreeViewItemExpanded(m_epoch, m_phase, m_updater, treeView, logicalIndex, expanded);
}

Core::Status PrimaryWindowUITreeUpdater::scrollTreeViewToIndex(UI::UINodeId treeView, u64 logicalIndex,
                                                               UI::UITreeViewScrollAlignment alignment)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::scrollTreeViewToIndex");
    }
    return m_state->scrollTreeViewToIndex(m_epoch, m_phase, m_updater, treeView, logicalIndex, alignment);
}

Core::Status PrimaryWindowUITreeUpdater::setPopupStyle(UI::UINodeId popup, const UI::UIPopupStyle& style)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setPopupStyle");
    }
    return m_state->setPopupStyle(m_epoch, m_phase, m_updater, popup, style);
}

Core::Result<UI::UIPopupStyle> PrimaryWindowUITreeUpdater::popupStyle(UI::UINodeId popup) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIPopupStyle>("PrimaryWindowUITreeUpdater::popupStyle");
    }
    return m_state->popupStyle(m_epoch, m_phase, m_updater, popup);
}

Core::Status PrimaryWindowUITreeUpdater::setPopupOpen(UI::UINodeId popup, bool open)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setPopupOpen");
    }
    return m_state->setPopupOpen(m_epoch, m_phase, m_updater, popup, open);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isPopupOpen(UI::UINodeId popup) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isPopupOpen");
    }
    return m_state->isPopupOpen(m_epoch, m_phase, m_updater, popup);
}

Core::Result<UI::UIPopupMetrics> PrimaryWindowUITreeUpdater::popupMetrics(UI::UINodeId popup) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIPopupMetrics>("PrimaryWindowUITreeUpdater::popupMetrics");
    }
    return m_state->popupMetrics(m_epoch, m_phase, m_updater, popup);
}

Core::Status PrimaryWindowUITreeUpdater::openDialog(UI::UINodeId dialog)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>(
            "PrimaryWindowUITreeUpdater::openDialog");
    }
    return m_state->openDialog(m_epoch, m_phase, m_updater, dialog);
}

Core::Status PrimaryWindowUITreeUpdater::dismissDialog(UI::UINodeId dialog)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>(
            "PrimaryWindowUITreeUpdater::dismissDialog");
    }
    return m_state->dismissDialog(m_epoch, m_phase, m_updater, dialog);
}

Core::Result<bool>
PrimaryWindowUITreeUpdater::isDialogOpen(UI::UINodeId dialog) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>(
            "PrimaryWindowUITreeUpdater::isDialogOpen");
    }
    return m_state->isDialogOpen(m_epoch, m_phase, m_updater, dialog);
}

Core::Status PrimaryWindowUITreeUpdater::setTooltipAnchor(UI::UINodeId tooltip,
                                                          UI::UINodeId anchor)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setTooltipAnchor");
    }
    return m_state->setTooltipAnchor(m_epoch, m_phase, m_updater, tooltip, anchor);
}

Core::Status PrimaryWindowUITreeUpdater::clearTooltipAnchor(UI::UINodeId tooltip)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearTooltipAnchor");
    }
    return m_state->clearTooltipAnchor(m_epoch, m_phase, m_updater, tooltip);
}

Core::Result<UI::UINodeId>
PrimaryWindowUITreeUpdater::tooltipAnchor(UI::UINodeId tooltip) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::tooltipAnchor");
    }
    return m_state->tooltipAnchor(m_epoch, m_phase, m_updater, tooltip);
}

Core::Status PrimaryWindowUITreeUpdater::showTooltip(UI::UINodeId tooltip)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::showTooltip");
    }
    return m_state->showTooltip(m_epoch, m_phase, m_updater, tooltip);
}

Core::Status PrimaryWindowUITreeUpdater::dismissTooltip(UI::UINodeId tooltip)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::dismissTooltip");
    }
    return m_state->dismissTooltip(m_epoch, m_phase, m_updater, tooltip);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isTooltipOpen(UI::UINodeId tooltip) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isTooltipOpen");
    }
    return m_state->isTooltipOpen(m_epoch, m_phase, m_updater, tooltip);
}

Core::Result<UI::UITooltipMetrics>
PrimaryWindowUITreeUpdater::tooltipMetrics(UI::UINodeId tooltip) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITooltipMetrics>(
            "PrimaryWindowUITreeUpdater::tooltipMetrics");
    }
    return m_state->tooltipMetrics(m_epoch, m_phase, m_updater, tooltip);
}

Core::Status PrimaryWindowUITreeUpdater::setMenuAnchor(UI::UINodeId menu, UI::UINodeId anchor)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setMenuAnchor");
    }
    return m_state->setMenuAnchor(m_epoch, m_phase, m_updater, menu, anchor);
}

Core::Status PrimaryWindowUITreeUpdater::clearMenuAnchor(UI::UINodeId menu)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearMenuAnchor");
    }
    return m_state->clearMenuAnchor(m_epoch, m_phase, m_updater, menu);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::menuAnchor(UI::UINodeId menu) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::menuAnchor");
    }
    return m_state->menuAnchor(m_epoch, m_phase, m_updater, menu);
}

Core::Status PrimaryWindowUITreeUpdater::setMenuOpen(UI::UINodeId menu, bool open)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setMenuOpen");
    }
    return m_state->setMenuOpen(m_epoch, m_phase, m_updater, menu, open);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isMenuOpen(UI::UINodeId menu) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isMenuOpen");
    }
    return m_state->isMenuOpen(m_epoch, m_phase, m_updater, menu);
}

Core::Result<UI::UIMenuMetrics> PrimaryWindowUITreeUpdater::menuMetrics(UI::UINodeId menu) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIMenuMetrics>("PrimaryWindowUITreeUpdater::menuMetrics");
    }
    return m_state->menuMetrics(m_epoch, m_phase, m_updater, menu);
}

Core::Status PrimaryWindowUITreeUpdater::setMenuItemSubmenu(
    UI::UINodeId item, UI::UINodeId submenu)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>(
            "PrimaryWindowUITreeUpdater::setMenuItemSubmenu");
    }
    return m_state->setMenuItemSubmenu(
        m_epoch, m_phase, m_updater, item, submenu);
}

Core::Status PrimaryWindowUITreeUpdater::clearMenuItemSubmenu(
    UI::UINodeId item)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>(
            "PrimaryWindowUITreeUpdater::clearMenuItemSubmenu");
    }
    return m_state->clearMenuItemSubmenu(
        m_epoch, m_phase, m_updater, item);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::menuItemSubmenu(
    UI::UINodeId item) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>(
            "PrimaryWindowUITreeUpdater::menuItemSubmenu");
    }
    return m_state->menuItemSubmenu(m_epoch, m_phase, m_updater, item);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::menuParentItem(
    UI::UINodeId menu) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>(
            "PrimaryWindowUITreeUpdater::menuParentItem");
    }
    return m_state->menuParentItem(m_epoch, m_phase, m_updater, menu);
}

Core::Status PrimaryWindowUITreeUpdater::setMenuItemChecked(UI::UINodeId item, bool checked)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setMenuItemChecked");
    }
    return m_state->setMenuItemChecked(m_epoch, m_phase, m_updater, item, checked);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isMenuItemChecked(UI::UINodeId item) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isMenuItemChecked");
    }
    return m_state->isMenuItemChecked(m_epoch, m_phase, m_updater, item);
}

Core::Result<UI::UIMenuCommandResult> PrimaryWindowUITreeUpdater::routeMenuCommand(
    UI::UINodeId menu, UI::UIMenuCommand command)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIMenuCommandResult>(
            "PrimaryWindowUITreeUpdater::routeMenuCommand");
    }
    return m_state->routeMenuCommand(m_epoch, m_phase, m_updater, menu, command);
}

Core::Status PrimaryWindowUITreeUpdater::setDropdownOpen(UI::UINodeId dropdown, bool open)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDropdownOpen");
    }
    return m_state->setDropdownOpen(m_epoch, m_phase, m_updater, dropdown, open);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isDropdownOpen(UI::UINodeId dropdown) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isDropdownOpen");
    }
    return m_state->isDropdownOpen(m_epoch, m_phase, m_updater, dropdown);
}

Core::Status PrimaryWindowUITreeUpdater::setDropdownSelectedItem(UI::UINodeId dropdown, UI::UINodeId item)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDropdownSelectedItem");
    }
    return m_state->setDropdownSelectedItem(m_epoch, m_phase, m_updater, dropdown, item);
}

Core::Result<UI::UINodeId> PrimaryWindowUITreeUpdater::dropdownSelectedItem(UI::UINodeId dropdown) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UINodeId>("PrimaryWindowUITreeUpdater::dropdownSelectedItem");
    }
    return m_state->dropdownSelectedItem(m_epoch, m_phase, m_updater, dropdown);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isDropdownItemSelected(UI::UINodeId item) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isDropdownItemSelected");
    }
    return m_state->isDropdownItemSelected(m_epoch, m_phase, m_updater, item);
}

Core::Status PrimaryWindowUITreeUpdater::setDropdownPaint(UI::UINodeId dropdown,
                                                          const UI::UIDropdownPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setDropdownPaint");
    }
    return m_state->setDropdownPaint(m_epoch, m_phase, m_updater, dropdown, paint);
}

Core::Result<UI::UIDropdownPaint> PrimaryWindowUITreeUpdater::dropdownPaint(UI::UINodeId dropdown) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIDropdownPaint>("PrimaryWindowUITreeUpdater::dropdownPaint");
    }
    return m_state->dropdownPaint(m_epoch, m_phase, m_updater, dropdown);
}

Core::Status PrimaryWindowUITreeUpdater::setProgressBarRange(UI::UINodeId progressBar, float minValue, float maxValue)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProgressBarRange");
    }
    return m_state->setProgressBarRange(m_epoch, m_phase, m_updater, progressBar, minValue, maxValue);
}

Core::Status PrimaryWindowUITreeUpdater::setProgressBarValue(UI::UINodeId progressBar, float value)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProgressBarValue");
    }
    return m_state->setProgressBarValue(m_epoch, m_phase, m_updater, progressBar, value);
}

Core::Result<float> PrimaryWindowUITreeUpdater::progressBarValue(UI::UINodeId progressBar) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<float>("PrimaryWindowUITreeUpdater::progressBarValue");
    }
    return m_state->progressBarValue(m_epoch, m_phase, m_updater, progressBar);
}

Core::Status PrimaryWindowUITreeUpdater::setProgressBarPaint(UI::UINodeId progressBar,
                                                             const UI::UIProgressBarPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setProgressBarPaint");
    }
    return m_state->setProgressBarPaint(m_epoch, m_phase, m_updater, progressBar, paint);
}

Core::Result<UI::UIProgressBarPaint> PrimaryWindowUITreeUpdater::progressBarPaint(UI::UINodeId progressBar) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIProgressBarPaint>("PrimaryWindowUITreeUpdater::progressBarPaint");
    }
    return m_state->progressBarPaint(m_epoch, m_phase, m_updater, progressBar);
}

Core::Status PrimaryWindowUITreeUpdater::setRadioButtonPaint(UI::UINodeId radioButton,
                                                             const UI::UIRadioButtonPaint& paint)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setRadioButtonPaint");
    }
    return m_state->setRadioButtonPaint(m_epoch, m_phase, m_updater, radioButton, paint);
}

Core::Result<UI::UIRadioButtonPaint> PrimaryWindowUITreeUpdater::radioButtonPaint(UI::UINodeId radioButton) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIRadioButtonPaint>("PrimaryWindowUITreeUpdater::radioButtonPaint");
    }
    return m_state->radioButtonPaint(m_epoch, m_phase, m_updater, radioButton);
}

Core::Status PrimaryWindowUITreeUpdater::setRadioButtonAction(UI::UINodeId radioButton,
                                                              UI::UIButtonActionCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setRadioButtonAction");
    }
    return m_state->setRadioButtonAction(m_epoch, m_phase, m_updater, radioButton, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::clearRadioButtonAction(UI::UINodeId radioButton)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::clearRadioButtonAction");
    }
    return m_state->clearRadioButtonAction(m_epoch, m_phase, m_updater, radioButton);
}

Core::Status PrimaryWindowUITreeUpdater::setRadioButtonSelected(UI::UINodeId radioButton, bool selected)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::setRadioButtonSelected");
    }
    return m_state->setRadioButtonSelected(m_epoch, m_phase, m_updater, radioButton, selected);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isRadioButtonSelected(UI::UINodeId radioButton) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isRadioButtonSelected");
    }
    return m_state->isRadioButtonSelected(m_epoch, m_phase, m_updater, radioButton);
}

Core::Result<bool> PrimaryWindowUITreeUpdater::isRadioButtonPressed(UI::UINodeId radioButton) const
{
    if (m_state == nullptr)
    {
        return expiredFacade<bool>("PrimaryWindowUITreeUpdater::isRadioButtonPressed");
    }
    return m_state->isRadioButtonPressed(m_epoch, m_phase, m_updater, radioButton);
}

Core::Result<UI::UIRoutedPointerListenerToken>
PrimaryWindowUITreeUpdater::addRoutedPointerListener(UI::UIRoutedPointerListenerDesc descriptor,
                                                     UI::UIRoutedPointerCallback callback)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIRoutedPointerListenerToken>("PrimaryWindowUITreeUpdater::addRoutedPointerListener");
    }
    return m_state->addRoutedPointerListener(m_epoch, m_phase, m_updater, descriptor, std::move(callback));
}

Core::Status PrimaryWindowUITreeUpdater::destroy(UI::UINodeId node)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUITreeUpdater::destroy");
    }
    return m_state->destroy(m_epoch, m_phase, m_updater, node);
}

PrimaryWindowUIRootBuilder::PrimaryWindowUIRootBuilder(Runtime::Detail::PrimaryWindowUICapabilityState& state,
                                                       u64 epoch) noexcept
    : m_state(&state), m_epoch(epoch)
{
}

PrimaryWindowUIRootBuilder::PrimaryWindowUIRootBuilder(PrimaryWindowUIRootBuilder&& other) noexcept
    : m_state(std::exchange(other.m_state, nullptr)), m_epoch(std::exchange(other.m_epoch, 0))
{
}

PrimaryWindowUIRootBuilder& PrimaryWindowUIRootBuilder::operator=(PrimaryWindowUIRootBuilder&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    m_state = std::exchange(other.m_state, nullptr);
    m_epoch = std::exchange(other.m_epoch, 0);
    return *this;
}

Core::Result<UI::UIStyleClassId> PrimaryWindowUIRootBuilder::registerStyleClass()
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIStyleClassId>(
            "PrimaryWindowUIRootBuilder::registerStyleClass");
    }
    return m_state->registerStyleClass(m_epoch);
}

Core::Result<UI::UIStyleTokenId>
PrimaryWindowUIRootBuilder::registerStyleColorToken(UI::UIStraightSrgba8Color value)
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIStyleTokenId>(
            "PrimaryWindowUIRootBuilder::registerStyleColorToken");
    }
    return m_state->registerStyleColorToken(m_epoch, value);
}

Core::Status PrimaryWindowUIRootBuilder::installStyleSheet(
    std::span<const UI::UIStyleBoxFillRule> rules)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>(
            "PrimaryWindowUIRootBuilder::installStyleSheet");
    }
    return m_state->installStyleSheet(m_epoch, rules);
}

Core::Result<UI::UITheme> PrimaryWindowUIRootBuilder::productTheme() const
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UITheme>("PrimaryWindowUIRootBuilder::productTheme");
    }
    return m_state->rootBuilderProductTheme(m_epoch);
}

Core::Status PrimaryWindowUIRootBuilder::setProductTheme(const UI::UITheme& theme)
{
    if (m_state == nullptr)
    {
        return expiredFacade<void>("PrimaryWindowUIRootBuilder::setProductTheme");
    }
    return m_state->setRootBuilderProductTheme(m_epoch, theme);
}

Core::Result<UI::UIRootOwner> PrimaryWindowUIRootBuilder::createRoot()
{
    if (m_state == nullptr)
    {
        return expiredFacade<UI::UIRootOwner>("PrimaryWindowUIRootBuilder::createRoot");
    }
    return m_state->createRoot(m_epoch);
}

Core::Result<PrimaryWindowUITreeUpdater> PrimaryWindowUIRootBuilder::treeUpdater(UI::UIRootOwner& rootOwner)
{
    if (m_state == nullptr)
    {
        return expiredFacade<PrimaryWindowUITreeUpdater>("PrimaryWindowUIRootBuilder::treeUpdater");
    }
    return m_state->treeUpdater(m_epoch, Runtime::Detail::PrimaryWindowUIPhase::GameStateEnter, rootOwner);
}

Core::Result<PrimaryWindowUIImageResolverRegistration>
PrimaryWindowUIRootBuilder::bindImageResolver(
    UI::UIRootOwner& rootOwner, Render::Texture2DFrameResourceResolver resolver)
{
    if (m_state == nullptr)
    {
        return expiredFacade<PrimaryWindowUIImageResolverRegistration>(
            "PrimaryWindowUIRootBuilder::bindImageResolver");
    }
    return m_state->bindImageResolver(m_epoch, rootOwner, resolver);
}

} // namespace Tina
