#include "detail/UIContextImpl.hpp"

#include <tina/ui/UIAuthoring.hpp>

namespace Tina::UI {

UIRootOwner::UIRootOwner(std::weak_ptr<Detail::UIContextLifetimeControl> lifetime, UINodeId root) noexcept
    : m_lifetime(std::move(lifetime)), m_root(root)
{
}

UIRootOwner::~UIRootOwner() noexcept
{
    reset();
}

UIRootOwner::UIRootOwner(UIRootOwner&& other) noexcept : m_lifetime(std::move(other.m_lifetime)), m_root(other.m_root)
{
    other.m_root = {};
}

UIRootOwner& UIRootOwner::operator=(UIRootOwner&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_root = other.m_root;
    other.m_root = {};
    return *this;
}

void UIRootOwner::reset() noexcept
{
    const UINodeId root = m_root;
    if (!root.hasValue())
    {
        m_lifetime.reset();
        return;
    }

    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime)
    {
        m_root = {};
        m_lifetime.reset();
        return;
    }

    UIContext* context = lifetime->releaseRoot(root);

    if (context != nullptr)
    {
        context->m_impl->destroyRootFromOwner(root);
    }
    m_root = {};
    m_lifetime.reset();
}

UINodeId UIRootOwner::rootNodeId() const noexcept
{
    return m_root;
}

bool UIRootOwner::hasValue() const noexcept
{
    return m_root.hasValue();
}

UIRootOwner::operator bool() const noexcept
{
    return hasValue();
}

UIRootBuilder::UIRootBuilder(UIContext& context) noexcept : m_context(&context)
{
}

Core::Result<UIRootOwner> UIRootBuilder::createRoot()
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->m_impl->createRoot(*m_context);
}

Core::Result<UINodeId> UIRootBuilder::createElement(UINodeId parent, const UIElementDescriptor& descriptor)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->m_impl->createElement(parent, descriptor);
}

UIElementBuildTransaction::UIElementBuildTransaction(UIContext& context, UINodeId updaterRoot,
                                                     UINodeId componentRoot,
                                                     UIComponentBuildBudget remainingBudget) noexcept
    : m_lifetime(context.m_impl->lifetime), m_updaterRoot(updaterRoot), m_componentRoot(componentRoot),
      m_remainingBudget(remainingBudget)
{
}

UIElementBuildTransaction::~UIElementBuildTransaction() noexcept
{
    reset();
}

UIElementBuildTransaction::UIElementBuildTransaction(UIElementBuildTransaction&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)), m_updaterRoot(std::exchange(other.m_updaterRoot, {})),
      m_componentRoot(std::exchange(other.m_componentRoot, {})),
      m_remainingBudget(std::exchange(other.m_remainingBudget, UIComponentBuildBudget{})),
      m_failure(std::move(other.m_failure))
{
    other.m_failure.reset();
}

UIElementBuildTransaction& UIElementBuildTransaction::operator=(UIElementBuildTransaction&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_updaterRoot = std::exchange(other.m_updaterRoot, {});
    m_componentRoot = std::exchange(other.m_componentRoot, {});
    m_remainingBudget = std::exchange(other.m_remainingBudget, UIComponentBuildBudget{});
    m_failure = std::move(other.m_failure);
    other.m_failure.reset();
    return *this;
}

Core::Result<UINodeId> UIElementBuildTransaction::createElement(UINodeId parent,
                                                                const UIElementDescriptor& descriptor)
{
    if (m_failure.has_value())
    {
        return Core::failure(*m_failure);
    }
    if (!m_componentRoot.hasValue())
    {
        return fail(UIErrorCode::InvalidNode, "UI component build transaction is not active");
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    UIContext* context = lifetime ? lifetime->attachedContext() : nullptr;
    if (context == nullptr)
    {
        m_componentRoot = {};
        return fail(UIErrorCode::WrongContext, "UI component build transaction context no longer exists");
    }
    auto created = context->m_impl->createElementFromBuildTransaction(
        m_updaterRoot, m_componentRoot, parent, descriptor, m_remainingBudget);
    if (!created)
    {
        if (created.error().code == UIErrorCode::WrongOwnerThread)
        {
            return Core::failure(created.error());
        }
        m_failure = created.error();
        context->m_impl->rollbackBuildTransaction(m_updaterRoot, m_componentRoot, m_remainingBudget);
        m_componentRoot = {};
        m_remainingBudget = {};
        return Core::failure(*m_failure);
    }
    return created;
}

Core::Result<UINodeId> UIElementBuildTransaction::commit()
{
    if (m_failure.has_value())
    {
        return Core::failure(*m_failure);
    }
    if (!m_componentRoot.hasValue())
    {
        return fail(UIErrorCode::InvalidNode, "UI component build transaction is not active");
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    UIContext* context = lifetime ? lifetime->attachedContext() : nullptr;
    if (context == nullptr)
    {
        m_componentRoot = {};
        return fail(UIErrorCode::WrongContext, "UI component build transaction context no longer exists");
    }
    const UINodeId componentRoot = m_componentRoot;
    if (Core::Status status =
            context->m_impl->commitBuildTransaction(m_updaterRoot, componentRoot, m_remainingBudget);
        !status)
    {
        if (status.error().code == UIErrorCode::WrongOwnerThread)
        {
            return Core::failure(status.error());
        }
        m_failure = status.error();
        m_componentRoot = {};
        m_remainingBudget = {};
        return Core::failure(*m_failure);
    }
    m_lifetime.reset();
    m_updaterRoot = {};
    m_componentRoot = {};
    m_remainingBudget = {};
    return componentRoot;
}

void UIElementBuildTransaction::reset() noexcept
{
    const UINodeId componentRoot = m_componentRoot;
    if (componentRoot.hasValue())
    {
        const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
        UIContext* context = lifetime ? lifetime->attachedContext() : nullptr;
        if (context != nullptr)
        {
            context->m_impl->rollbackBuildTransaction(m_updaterRoot, componentRoot, m_remainingBudget);
        }
    }
    m_lifetime.reset();
    m_updaterRoot = {};
    m_componentRoot = {};
    m_remainingBudget = {};
    m_failure.reset();
}

UINodeId UIElementBuildTransaction::rootNodeId() const noexcept
{
    return m_componentRoot;
}

UIComponentBuildBudget UIElementBuildTransaction::remainingBudget() const noexcept
{
    return isActive() ? m_remainingBudget : UIComponentBuildBudget{};
}

bool UIElementBuildTransaction::isActive() const noexcept
{
    if (!m_componentRoot.hasValue())
    {
        return false;
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime)
    {
        return false;
    }
    UIContext* context = lifetime->attachedContext();
    return context != nullptr && context->m_impl->isBuildTransactionActive(m_componentRoot);
}

UITreeUpdater::UITreeUpdater(UIContext& context, UINodeId root) noexcept : m_context(&context), m_root(root)
{
}

UITreeUpdater::UITreeUpdater(UITreeUpdater&& other) noexcept
    : m_context(std::exchange(other.m_context, nullptr)), m_root(std::exchange(other.m_root, {}))
{
}

UITreeUpdater& UITreeUpdater::operator=(UITreeUpdater&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    m_context = std::exchange(other.m_context, nullptr);
    m_root = std::exchange(other.m_root, {});
    return *this;
}

Core::Result<UINodeId> UITreeUpdater::createElement(UINodeId parent, const UIElementDescriptor& descriptor)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->createElementFromUpdater(m_root, parent, descriptor);
}

Core::Result<UIElementBuildTransaction>
UITreeUpdater::beginBuildTransaction(UINodeId parent, const UIElementDescriptor& rootDescriptor,
                                     UIComponentBuildBudget budget)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->beginBuildTransaction(
        *m_context, m_root, parent, rootDescriptor, budget);
}

Core::Result<UIFlowLayerId> UITreeUpdater::registerFlowLayer(UINodeId layer)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->registerFlowLayerFromUpdater(m_root, layer);
}

Core::Result<UIFlowScreenId> UITreeUpdater::registerFlowScreen(UIFlowLayerId layer, UINodeId screen)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->registerFlowScreenFromUpdater(m_root, layer, screen);
}

Core::Status UITreeUpdater::pushFlowScreen(UIFlowScreenId screen)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->pushFlowScreenFromUpdater(m_root, screen);
}

Core::Result<UIFlowScreenId> UITreeUpdater::popFlowScreen(UIFlowLayerId layer)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->popFlowScreenFromUpdater(m_root, layer);
}

Core::Result<UIFlowScreenId> UITreeUpdater::replaceFlowScreen(UIFlowScreenId screen)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->replaceFlowScreenFromUpdater(m_root, screen);
}

Core::Result<UIFlowScreenId> UITreeUpdater::activeFlowScreen(UIFlowLayerId layer) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->activeFlowScreenFromUpdater(m_root, layer);
}

Core::Result<bool> UITreeUpdater::isFlowScreenActive(UIFlowScreenId screen) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isFlowScreenActiveFromUpdater(m_root, screen);
}

Core::Status UITreeUpdater::assignFlowGamepad(Platform::GamepadId gamepad,
                                              UIFlowLocalUserId localUser)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    if (!m_context->m_impl->isAliveInRoot(m_root, m_root))
    {
        return fail(UIErrorCode::InvalidNode, "UI tree updater root is stale");
    }
    return m_context->m_impl->assignFlowGamepad(gamepad, localUser);
}

Core::Status UITreeUpdater::clearFlowGamepadAssignment(Platform::GamepadId gamepad)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    if (!m_context->m_impl->isAliveInRoot(m_root, m_root))
    {
        return fail(UIErrorCode::InvalidNode, "UI tree updater root is stale");
    }
    return m_context->m_impl->clearFlowGamepadAssignment(gamepad);
}

Core::Result<UIFlowLocalUserId>
UITreeUpdater::flowLocalUserForGamepad(Platform::GamepadId gamepad) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    if (!m_context->m_impl->isAliveInRoot(m_root, m_root))
    {
        return fail(UIErrorCode::InvalidNode, "UI tree updater root is stale");
    }
    return m_context->m_impl->flowLocalUserForGamepad(gamepad);
}

Core::Result<UIFlowInputDeviceState>
UITreeUpdater::flowInputDeviceState(UIFlowLocalUserId localUser) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    if (!m_context->m_impl->isAliveInRoot(m_root, m_root))
    {
        return fail(UIErrorCode::InvalidNode, "UI tree updater root is stale");
    }
    return m_context->m_impl->flowInputDeviceState(localUser);
}

Core::Status UITreeUpdater::setFlowScreenAction(UIFlowScreenId screen, UIFlowAction action,
                                                UIFlowActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setFlowScreenActionFromUpdater(m_root, screen, action,
                                                      std::move(callback));
}

Core::Status UITreeUpdater::clearFlowScreenAction(UIFlowScreenId screen, UIFlowAction action)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearFlowScreenActionFromUpdater(m_root, screen, action);
}

bool UITreeUpdater::isAlive(UINodeId node) const noexcept
{
    return m_context != nullptr && m_context->m_impl->isAliveInRoot(m_root, node);
}

Core::Result<UILogicalRect> UITreeUpdater::committedLayoutRect(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return Core::failure(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    if (!m_context->m_impl->isAliveInRoot(m_root, node))
    {
        return Core::failure(UIErrorCode::InvalidNode,
                             "UI committed layout query requires a live node in the updater root");
    }
    for (const UICommittedLayoutEntry& entry : m_context->m_impl->committedLayout().entries())
    {
        if (entry.node == node)
        {
            return entry.worldRect;
        }
    }
    return Core::failure(UIErrorCode::InvalidNode,
                         "UI node is absent from the committed layout snapshot");
}

Core::Status UITreeUpdater::setLayoutStyle(UINodeId node, const UILayoutStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setLayoutStyleFromUpdater(m_root, node, style);
}

Core::Status UITreeUpdater::setPointerHitPolicy(UINodeId node, UIPointerHitPolicy policy)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setPointerHitPolicyFromUpdater(m_root, node, policy);
}

Core::Status UITreeUpdater::setEnabled(UINodeId node, bool enabled)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setEnabledFromUpdater(m_root, node, enabled);
}

Core::Result<bool> UITreeUpdater::isEnabled(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isEnabledFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setFocusScopeMode(UINodeId node, UIFocusScopeMode mode)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setFocusScopeModeFromUpdater(m_root, node, mode);
}

Core::Result<UIFocusScopeMode> UITreeUpdater::focusScopeMode(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->focusScopeModeFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::requestFocus(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->requestFocusFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::clearFocus()
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearFocusFromUpdater(m_root);
}

Core::Result<UINodeId> UITreeUpdater::focusedNode() const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->focusedNodeFromUpdater(m_root);
}

Core::Status UITreeUpdater::setStyleRole(UINodeId node, UIStyleRoleId role)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setStyleRoleFromUpdater(m_root, node, role);
}

Core::Result<UIStyleRoleId> UITreeUpdater::styleRole(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->styleRoleFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::clearOverride(UINodeId node, UIStyleOverride properties)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearOverrideFromUpdater(m_root, node, properties);
}

Core::Status UITreeUpdater::setBoxPaint(UINodeId node, const UIBoxPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setBoxPaintFromUpdater(m_root, node, paint);
}

Core::Status UITreeUpdater::setImageTint(UINodeId node, UIStraightSrgba8Color tint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setImageTintFromUpdater(m_root, node, tint);
}

Core::Result<UIStraightSrgba8Color> UITreeUpdater::imageTint(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->imageTintFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setButtonPaint(UINodeId button, const UIButtonPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setButtonPaintFromUpdater(m_root, button, paint);
}

Core::Result<UIButtonPaint> UITreeUpdater::buttonPaint(UINodeId button) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->buttonPaintFromUpdater(m_root, button);
}

Core::Status UITreeUpdater::setText(UINodeId node, std::string_view utf8)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTextFromUpdater(m_root, node, utf8);
}

Core::Status UITreeUpdater::setTextStyle(UINodeId node, const UITextStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTextStyleFromUpdater(m_root, node, style);
}

Core::Status UITreeUpdater::setContentAlignment(UINodeId node, UIContentAlignment alignment)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setContentAlignmentFromUpdater(m_root, node, alignment);
}

Core::Status UITreeUpdater::setTextOverflow(UINodeId node, UITextOverflow overflow)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTextOverflowFromUpdater(m_root, node, overflow);
}

Core::Result<UITextOverflow> UITreeUpdater::textOverflow(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->textOverflowFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setTextWrapMode(UINodeId node, UITextWrapMode wrapMode)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTextWrapModeFromUpdater(m_root, node, wrapMode);
}

Core::Result<UITextWrapMode> UITreeUpdater::textWrapMode(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->textWrapModeFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setTextLineClamp(
    UINodeId node, UITextLineClamp lineClamp)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTextLineClampFromUpdater(
        m_root, node, lineClamp);
}

Core::Result<UITextLineClamp> UITreeUpdater::textLineClamp(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->textLineClampFromUpdater(m_root, node);
}

Core::Result<std::string_view> UITreeUpdater::text(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->textFromUpdater(m_root, node);
}

Core::Result<UITextStyle> UITreeUpdater::textStyle(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->textStyleFromUpdater(m_root, node);
}

Core::Result<UIContentAlignment> UITreeUpdater::contentAlignment(UINodeId node) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->contentAlignmentFromUpdater(m_root, node);
}

Core::Status UITreeUpdater::setTextSelection(UINodeId textEdit, UITextSelection selection)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTextSelectionFromUpdater(m_root, textEdit, selection);
}

Core::Result<UITextSelection> UITreeUpdater::textSelection(UINodeId textEdit) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->textSelectionFromUpdater(m_root, textEdit);
}

Core::Status UITreeUpdater::setTextEditPaint(UINodeId textEdit, const UITextEditPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTextEditPaintFromUpdater(m_root, textEdit, paint);
}

Core::Result<UITextEditPaint> UITreeUpdater::textEditPaint(UINodeId textEdit) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->textEditPaintFromUpdater(m_root, textEdit);
}

Core::Status UITreeUpdater::setButtonAction(UINodeId button, UIButtonActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setButtonActionFromUpdater(m_root, button, std::move(callback));
}

Core::Status UITreeUpdater::clearButtonAction(UINodeId button)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearButtonActionFromUpdater(m_root, button);
}

Core::Result<bool> UITreeUpdater::isButtonPressed(UINodeId button) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isButtonPressedFromUpdater(m_root, button);
}

Core::Status UITreeUpdater::setCheckboxAction(UINodeId checkbox, UIButtonActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setCheckboxActionFromUpdater(m_root, checkbox, std::move(callback));
}

Core::Status UITreeUpdater::clearCheckboxAction(UINodeId checkbox)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearCheckboxActionFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setCheckboxPaint(UINodeId checkbox, const UICheckboxPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setCheckboxPaintFromUpdater(m_root, checkbox, paint);
}

Core::Result<UICheckboxPaint> UITreeUpdater::checkboxPaint(UINodeId checkbox) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->checkboxPaintFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setChecked(UINodeId checkbox, bool checked)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setCheckedFromUpdater(m_root, checkbox, checked);
}

Core::Result<bool> UITreeUpdater::isChecked(UINodeId checkbox) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isCheckedFromUpdater(m_root, checkbox);
}

Core::Result<bool> UITreeUpdater::isCheckboxPressed(UINodeId checkbox) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isCheckboxPressedFromUpdater(m_root, checkbox);
}

Core::Status UITreeUpdater::setSliderRange(UINodeId slider, float minValue, float maxValue, float step)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setSliderRangeFromUpdater(m_root, slider, minValue, maxValue, step);
}

Core::Status UITreeUpdater::setSliderValue(UINodeId slider, float value)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setSliderValueFromUpdater(m_root, slider, value);
}

Core::Result<float> UITreeUpdater::sliderValue(UINodeId slider) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->sliderValueFromUpdater(m_root, slider);
}

Core::Status UITreeUpdater::setSliderPaint(UINodeId slider, const UISliderPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setSliderPaintFromUpdater(m_root, slider, paint);
}

Core::Result<UISliderPaint> UITreeUpdater::sliderPaint(UINodeId slider) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->sliderPaintFromUpdater(m_root, slider);
}

Core::Status UITreeUpdater::setSliderChangeCallback(UINodeId slider, UISliderChangeCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setSliderChangeCallbackFromUpdater(m_root, slider, std::move(callback));
}

Core::Status UITreeUpdater::clearSliderChangeCallback(UINodeId slider)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearSliderChangeCallbackFromUpdater(m_root, slider);
}

Core::Result<bool> UITreeUpdater::isSliderDragging(UINodeId slider) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isSliderDraggingFromUpdater(m_root, slider);
}

Core::Status UITreeUpdater::setSplitViewParts(
    UINodeId splitView, UINodeId primaryPane, UINodeId splitter,
    UINodeId secondaryPane)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setSplitViewPartsFromUpdater(
        m_root, splitView, primaryPane, splitter, secondaryPane);
}

Core::Status UITreeUpdater::clearSplitViewParts(UINodeId splitView)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearSplitViewPartsFromUpdater(m_root, splitView);
}

Core::Result<UISplitViewParts> UITreeUpdater::splitViewParts(UINodeId splitView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->splitViewPartsFromUpdater(m_root, splitView);
}

Core::Status UITreeUpdater::setSplitViewFraction(UINodeId splitView, float fraction)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setSplitViewFractionFromUpdater(m_root, splitView, fraction);
}

Core::Result<float> UITreeUpdater::splitViewFraction(UINodeId splitView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->splitViewFractionFromUpdater(m_root, splitView);
}

Core::Result<UISplitViewMetrics> UITreeUpdater::splitViewMetrics(UINodeId splitView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->splitViewMetricsFromUpdater(m_root, splitView);
}

Core::Result<bool> UITreeUpdater::isSplitterDragging(UINodeId splitter) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isSplitterDraggingFromUpdater(m_root, splitter);
}

Core::Status UITreeUpdater::setSplitterPaint(
    UINodeId splitter, const UISplitterPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setSplitterPaintFromUpdater(m_root, splitter, paint);
}

Core::Result<UISplitterPaint> UITreeUpdater::splitterPaint(
    UINodeId splitter) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->splitterPaintFromUpdater(m_root, splitter);
}

Core::Status UITreeUpdater::setTabViewItems(
    UINodeId tabView, std::span<const UITabViewItem> items, u32 activeIndex)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTabViewItemsFromUpdater(m_root, tabView, items, activeIndex);
}

Core::Status UITreeUpdater::clearTabViewItems(UINodeId tabView)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearTabViewItemsFromUpdater(m_root, tabView);
}

Core::Result<u32> UITreeUpdater::tabViewItemCount(UINodeId tabView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->tabViewItemCountFromUpdater(m_root, tabView);
}

Core::Result<UITabViewItem> UITreeUpdater::tabViewItemAt(UINodeId tabView, u32 index) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->tabViewItemAtFromUpdater(m_root, tabView, index);
}

Core::Status UITreeUpdater::setTabViewActiveTab(UINodeId tabView, UINodeId tab)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTabViewActiveTabFromUpdater(m_root, tabView, tab);
}

Core::Result<UINodeId> UITreeUpdater::tabViewActiveTab(UINodeId tabView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->tabViewActiveTabFromUpdater(m_root, tabView);
}

Core::Result<UINodeId> UITreeUpdater::tabViewActivePanel(UINodeId tabView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->tabViewActivePanelFromUpdater(m_root, tabView);
}

Core::Result<UITabViewMetrics> UITreeUpdater::tabViewMetrics(UINodeId tabView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->tabViewMetricsFromUpdater(m_root, tabView);
}

Core::Result<UITabViewCommandResult> UITreeUpdater::routeTabViewCommand(
    UINodeId tabView, UITabViewCommand command)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->routeTabViewCommandFromUpdater(m_root, tabView, command);
}

Core::Status UITreeUpdater::setTabPaint(UINodeId tab, const UITabPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTabPaintFromUpdater(m_root, tab, paint);
}

Core::Result<UITabPaint> UITreeUpdater::tabPaint(UINodeId tab) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->tabPaintFromUpdater(m_root, tab);
}

Core::Status UITreeUpdater::setScrollViewStyle(UINodeId scrollView, const UIScrollViewStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setScrollViewStyleFromUpdater(m_root, scrollView, style);
}

Core::Result<UIScrollViewStyle> UITreeUpdater::scrollViewStyle(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->scrollViewStyleFromUpdater(m_root, scrollView);
}

Core::Status UITreeUpdater::setScrollViewOffset(UINodeId scrollView, UIScrollOffset offset)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setScrollViewOffsetFromUpdater(m_root, scrollView, offset);
}

Core::Result<UIScrollOffset> UITreeUpdater::scrollViewOffset(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->scrollViewOffsetFromUpdater(m_root, scrollView);
}

Core::Result<UIScrollViewMetrics> UITreeUpdater::scrollViewMetrics(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->scrollViewMetricsFromUpdater(m_root, scrollView);
}

Core::Status UITreeUpdater::setScrollViewPaint(UINodeId scrollView, const UIScrollViewPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setScrollViewPaintFromUpdater(m_root, scrollView, paint);
}

Core::Result<UIScrollViewPaint> UITreeUpdater::scrollViewPaint(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->scrollViewPaintFromUpdater(m_root, scrollView);
}

Core::Result<bool> UITreeUpdater::isScrollViewDragging(UINodeId scrollView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isScrollViewDraggingFromUpdater(m_root, scrollView);
}

Core::Status UITreeUpdater::setPopupStyle(UINodeId popup, const UIPopupStyle& style)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setPopupStyleFromUpdater(m_root, popup, style);
}

Core::Result<UIPopupStyle> UITreeUpdater::popupStyle(UINodeId popup) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->popupStyleFromUpdater(m_root, popup);
}

Core::Status UITreeUpdater::setPopupOpen(UINodeId popup, bool open)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setPopupOpenFromUpdater(m_root, popup, open);
}

Core::Result<bool> UITreeUpdater::isPopupOpen(UINodeId popup) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isPopupOpenFromUpdater(m_root, popup);
}

Core::Result<UIPopupMetrics> UITreeUpdater::popupMetrics(UINodeId popup) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->popupMetricsFromUpdater(m_root, popup);
}

Core::Status UITreeUpdater::openDialog(UINodeId dialog)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->openDialogFromUpdater(m_root, dialog);
}

Core::Status UITreeUpdater::dismissDialog(UINodeId dialog)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->dismissDialogFromUpdater(m_root, dialog);
}

Core::Result<bool> UITreeUpdater::isDialogOpen(UINodeId dialog) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isDialogOpenFromUpdater(m_root, dialog);
}

Core::Status UITreeUpdater::setTooltipAnchor(UINodeId tooltip, UINodeId anchor)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setTooltipAnchorFromUpdater(m_root, tooltip, anchor);
}

Core::Status UITreeUpdater::clearTooltipAnchor(UINodeId tooltip)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearTooltipAnchorFromUpdater(m_root, tooltip);
}

Core::Result<UINodeId> UITreeUpdater::tooltipAnchor(UINodeId tooltip) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->tooltipAnchorFromUpdater(m_root, tooltip);
}

Core::Status UITreeUpdater::showTooltip(UINodeId tooltip)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->showTooltipFromUpdater(m_root, tooltip);
}

Core::Status UITreeUpdater::dismissTooltip(UINodeId tooltip)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->dismissTooltipFromUpdater(m_root, tooltip);
}

Core::Result<bool> UITreeUpdater::isTooltipOpen(UINodeId tooltip) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isTooltipOpenFromUpdater(m_root, tooltip);
}

Core::Result<UITooltipMetrics> UITreeUpdater::tooltipMetrics(UINodeId tooltip) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->tooltipMetricsFromUpdater(m_root, tooltip);
}

Core::Status UITreeUpdater::setMenuAnchor(UINodeId menu, UINodeId anchor)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setMenuAnchorFromUpdater(m_root, menu, anchor);
}

Core::Status UITreeUpdater::clearMenuAnchor(UINodeId menu)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearMenuAnchorFromUpdater(m_root, menu);
}

Core::Result<UINodeId> UITreeUpdater::menuAnchor(UINodeId menu) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->menuAnchorFromUpdater(m_root, menu);
}

Core::Status UITreeUpdater::setMenuOpen(UINodeId menu, bool open)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setMenuOpenFromUpdater(m_root, menu, open);
}

Core::Result<bool> UITreeUpdater::isMenuOpen(UINodeId menu) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isMenuOpenFromUpdater(m_root, menu);
}

Core::Result<UIMenuMetrics> UITreeUpdater::menuMetrics(UINodeId menu) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->menuMetricsFromUpdater(m_root, menu);
}

Core::Status UITreeUpdater::setMenuItemSubmenu(
    UINodeId item, UINodeId submenu)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setMenuItemSubmenuFromUpdater(m_root, item, submenu);
}

Core::Status UITreeUpdater::clearMenuItemSubmenu(UINodeId item)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearMenuItemSubmenuFromUpdater(m_root, item);
}

Core::Result<UINodeId> UITreeUpdater::menuItemSubmenu(UINodeId item) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->menuItemSubmenuFromUpdater(m_root, item);
}

Core::Result<UINodeId> UITreeUpdater::menuParentItem(UINodeId menu) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->menuParentItemFromUpdater(m_root, menu);
}

Core::Status UITreeUpdater::setMenuItemChecked(UINodeId item, bool checked)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setMenuItemCheckedFromUpdater(m_root, item, checked);
}

Core::Result<bool> UITreeUpdater::isMenuItemChecked(UINodeId item) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isMenuItemCheckedFromUpdater(m_root, item);
}

Core::Result<UIMenuCommandResult>
UITreeUpdater::routeMenuCommand(UINodeId menu, UIMenuCommand command)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->routeMenuCommandFromUpdater(m_root, menu, command);
}

Core::Status UITreeUpdater::setDropdownOpen(UINodeId dropdown, bool open)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setDropdownOpenFromUpdater(m_root, dropdown, open);
}

Core::Result<bool> UITreeUpdater::isDropdownOpen(UINodeId dropdown) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isDropdownOpenFromUpdater(m_root, dropdown);
}

Core::Status UITreeUpdater::setDropdownSelectedItem(UINodeId dropdown, UINodeId item)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setDropdownSelectedItemFromUpdater(m_root, dropdown, item);
}

Core::Result<UINodeId> UITreeUpdater::dropdownSelectedItem(UINodeId dropdown) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->dropdownSelectedItemFromUpdater(m_root, dropdown);
}

Core::Result<bool> UITreeUpdater::isDropdownItemSelected(UINodeId item) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isDropdownItemSelectedFromUpdater(m_root, item);
}

Core::Status UITreeUpdater::setDropdownPaint(UINodeId dropdown, const UIDropdownPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setDropdownPaintFromUpdater(m_root, dropdown, paint);
}

Core::Result<UIDropdownPaint> UITreeUpdater::dropdownPaint(UINodeId dropdown) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->dropdownPaintFromUpdater(m_root, dropdown);
}

Core::Status UITreeUpdater::setListViewDataSource(UINodeId listView, UIListViewDataSource source)
{
    return m_context != nullptr
               ? m_context->m_impl->setListViewDataSourceFromUpdater(m_root, listView, source)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearListViewDataSource(UINodeId listView)
{
    return m_context != nullptr
               ? m_context->m_impl->clearListViewDataSourceFromUpdater(m_root, listView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::invalidateListViewItems(UINodeId listView)
{
    return m_context != nullptr
               ? m_context->m_impl->invalidateListViewItemsFromUpdater(m_root, listView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setListViewStyle(UINodeId listView, const UIListViewStyle& style)
{
    return m_context != nullptr
               ? m_context->m_impl->setListViewStyleFromUpdater(m_root, listView, style)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIListViewStyle> UITreeUpdater::listViewStyle(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->listViewStyleFromUpdater(m_root, listView);
}

Core::Status UITreeUpdater::setListViewPaint(UINodeId listView, const UIListViewPaint& paint)
{
    return m_context != nullptr
               ? m_context->m_impl->setListViewPaintFromUpdater(m_root, listView, paint)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIListViewPaint> UITreeUpdater::listViewPaint(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->listViewPaintFromUpdater(m_root, listView);
}

Core::Result<UIListViewMetrics> UITreeUpdater::listViewMetrics(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->listViewMetricsFromUpdater(m_root, listView);
}

Core::Status UITreeUpdater::setListViewSelectedIndex(UINodeId listView, u64 logicalIndex)
{
    return m_context != nullptr
               ? m_context->m_impl->setListViewSelectedIndexFromUpdater(m_root, listView, logicalIndex)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearListViewSelection(UINodeId listView)
{
    return m_context != nullptr
               ? m_context->m_impl->clearListViewSelectionFromUpdater(m_root, listView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIListViewSelection> UITreeUpdater::listViewSelection(UINodeId listView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->listViewSelectionFromUpdater(m_root, listView);
}

Core::Status UITreeUpdater::scrollListViewToIndex(UINodeId listView, u64 logicalIndex,
                                                 UIListViewScrollAlignment alignment)
{
    return m_context != nullptr ? m_context->m_impl->scrollListViewToIndexFromUpdater(m_root, listView, logicalIndex, alignment)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setVirtualGridViewDataSource(
    UINodeId virtualGridView, UIVirtualGridViewDataSource source)
{
    return m_context != nullptr
               ? m_context->m_impl->setVirtualGridViewDataSourceFromUpdater(m_root, virtualGridView, source)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearVirtualGridViewDataSource(UINodeId virtualGridView)
{
    return m_context != nullptr
               ? m_context->m_impl->clearVirtualGridViewDataSourceFromUpdater(m_root, virtualGridView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::invalidateVirtualGridViewItems(UINodeId virtualGridView)
{
    return m_context != nullptr
               ? m_context->m_impl->invalidateVirtualGridViewItemsFromUpdater(m_root, virtualGridView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setVirtualGridViewStyle(
    UINodeId virtualGridView, const UIVirtualGridViewStyle& style)
{
    return m_context != nullptr
               ? m_context->m_impl->setVirtualGridViewStyleFromUpdater(m_root, virtualGridView, style)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIVirtualGridViewStyle>
UITreeUpdater::virtualGridViewStyle(UINodeId virtualGridView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->virtualGridViewStyleFromUpdater(m_root, virtualGridView);
}

Core::Status UITreeUpdater::setVirtualGridViewPaint(
    UINodeId virtualGridView, const UIVirtualGridViewPaint& paint)
{
    return m_context != nullptr
               ? m_context->m_impl->setVirtualGridViewPaintFromUpdater(m_root, virtualGridView, paint)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIVirtualGridViewPaint>
UITreeUpdater::virtualGridViewPaint(UINodeId virtualGridView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->virtualGridViewPaintFromUpdater(m_root, virtualGridView);
}

Core::Result<UIVirtualGridViewMetrics>
UITreeUpdater::virtualGridViewMetrics(UINodeId virtualGridView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->virtualGridViewMetricsFromUpdater(m_root, virtualGridView);
}

Core::Result<UINodeId>
UITreeUpdater::virtualGridViewMaterializedItemNode(
    UINodeId virtualGridView, u64 logicalIndex) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->virtualGridViewMaterializedItemNodeFromUpdater(
        m_root, virtualGridView, logicalIndex);
}

Core::Status UITreeUpdater::setVirtualGridViewSelectedIndex(
    UINodeId virtualGridView, u64 logicalIndex)
{
    return m_context != nullptr
               ? m_context->m_impl->setVirtualGridViewSelectedIndexFromUpdater(m_root, virtualGridView, logicalIndex)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearVirtualGridViewSelection(UINodeId virtualGridView)
{
    return m_context != nullptr
               ? m_context->m_impl->clearVirtualGridViewSelectionFromUpdater(m_root, virtualGridView)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UIVirtualGridViewSelection>
UITreeUpdater::virtualGridViewSelection(UINodeId virtualGridView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->virtualGridViewSelectionFromUpdater(m_root, virtualGridView);
}

Core::Status UITreeUpdater::scrollVirtualGridViewToIndex(
    UINodeId virtualGridView, u64 logicalIndex,
    UIVirtualGridViewScrollAlignment alignment)
{
    return m_context != nullptr
               ? m_context->m_impl->scrollVirtualGridViewToIndexFromUpdater(
                     m_root, virtualGridView, logicalIndex, alignment)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setDataGridDataSource(
    UINodeId dataGrid, UIDataGridDataSource source)
{
    return m_context != nullptr
               ? m_context->m_impl->setDataGridDataSourceFromUpdater(
                     m_root, dataGrid, source)
               : fail(UIErrorCode::WrongContext,
                      "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearDataGridDataSource(UINodeId dataGrid)
{
    return m_context != nullptr
               ? m_context->m_impl->clearDataGridDataSourceFromUpdater(m_root, dataGrid)
               : fail(UIErrorCode::WrongContext,
                      "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::invalidateDataGridItems(UINodeId dataGrid)
{
    return m_context != nullptr
               ? m_context->m_impl->invalidateDataGridItemsFromUpdater(m_root, dataGrid)
               : fail(UIErrorCode::WrongContext,
                      "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setDataGridStyle(
    UINodeId dataGrid, const UIDataGridStyle& style)
{
    return m_context != nullptr
               ? m_context->m_impl->setDataGridStyleFromUpdater(m_root, dataGrid, style)
               : fail(UIErrorCode::WrongContext,
                      "UI tree updater is not bound to a context");
}

Core::Result<UIDataGridStyle>
UITreeUpdater::dataGridStyle(UINodeId dataGrid) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->dataGridStyleFromUpdater(m_root, dataGrid);
}

Core::Status UITreeUpdater::setDataGridPaint(
    UINodeId dataGrid, const UIDataGridPaint& paint)
{
    return m_context != nullptr
               ? m_context->m_impl->setDataGridPaintFromUpdater(m_root, dataGrid, paint)
               : fail(UIErrorCode::WrongContext,
                      "UI tree updater is not bound to a context");
}

Core::Result<UIDataGridPaint>
UITreeUpdater::dataGridPaint(UINodeId dataGrid) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->dataGridPaintFromUpdater(m_root, dataGrid);
}

Core::Result<UIDataGridMetrics>
UITreeUpdater::dataGridMetrics(UINodeId dataGrid) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->dataGridMetricsFromUpdater(m_root, dataGrid);
}

Core::Status UITreeUpdater::setDataGridSelectedCell(
    UINodeId dataGrid, u64 logicalRow, u32 logicalColumn)
{
    return m_context != nullptr
               ? m_context->m_impl->setDataGridSelectedCellFromUpdater(
                     m_root, dataGrid, logicalRow, logicalColumn)
               : fail(UIErrorCode::WrongContext,
                      "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearDataGridSelection(UINodeId dataGrid)
{
    return m_context != nullptr
               ? m_context->m_impl->clearDataGridSelectionFromUpdater(m_root, dataGrid)
               : fail(UIErrorCode::WrongContext,
                      "UI tree updater is not bound to a context");
}

Core::Result<UIDataGridSelection>
UITreeUpdater::dataGridSelection(UINodeId dataGrid) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->dataGridSelectionFromUpdater(m_root, dataGrid);
}

Core::Status UITreeUpdater::scrollDataGridToCell(
    UINodeId dataGrid, u64 logicalRow, u32 logicalColumn,
    UIDataGridScrollAlignment alignment)
{
    return m_context != nullptr
               ? m_context->m_impl->scrollDataGridToCellFromUpdater(
                     m_root, dataGrid, logicalRow, logicalColumn, alignment)
               : fail(UIErrorCode::WrongContext,
                      "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setTreeViewDataSource(UINodeId treeView, UITreeViewDataSource source)
{
    return m_context != nullptr ? m_context->m_impl->setTreeViewDataSourceFromUpdater(m_root, treeView, source)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearTreeViewDataSource(UINodeId treeView)
{
    return m_context != nullptr ? m_context->m_impl->clearTreeViewDataSourceFromUpdater(m_root, treeView)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::invalidateTreeViewItems(UINodeId treeView)
{
    return m_context != nullptr ? m_context->m_impl->invalidateTreeViewItemsFromUpdater(m_root, treeView)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setTreeViewStyle(UINodeId treeView, const UITreeViewStyle& style)
{
    return m_context != nullptr ? m_context->m_impl->setTreeViewStyleFromUpdater(m_root, treeView, style)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UITreeViewStyle> UITreeUpdater::treeViewStyle(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->treeViewStyleFromUpdater(m_root, treeView);
}

Core::Status UITreeUpdater::setTreeViewPaint(UINodeId treeView, const UITreeViewPaint& paint)
{
    return m_context != nullptr ? m_context->m_impl->setTreeViewPaintFromUpdater(m_root, treeView, paint)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UITreeViewPaint> UITreeUpdater::treeViewPaint(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->treeViewPaintFromUpdater(m_root, treeView);
}

Core::Result<UITreeViewMetrics> UITreeUpdater::treeViewMetrics(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->treeViewMetricsFromUpdater(m_root, treeView);
}

Core::Result<UINodeId> UITreeUpdater::treeViewMaterializedItemNode(
    UINodeId treeView, u64 logicalIndex) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext,
                    "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->treeViewMaterializedItemNodeFromUpdater(
        m_root, treeView, logicalIndex);
}

Core::Status UITreeUpdater::setTreeViewSelectedIndex(UINodeId treeView, u64 logicalIndex)
{
    return m_context != nullptr ? m_context->m_impl->setTreeViewSelectedIndexFromUpdater(m_root, treeView, logicalIndex)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::clearTreeViewSelection(UINodeId treeView)
{
    return m_context != nullptr ? m_context->m_impl->clearTreeViewSelectionFromUpdater(m_root, treeView)
                                : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Result<UITreeViewSelection> UITreeUpdater::treeViewSelection(UINodeId treeView) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->treeViewSelectionFromUpdater(m_root, treeView);
}

Core::Status UITreeUpdater::setTreeViewItemExpanded(UINodeId treeView, u64 logicalIndex, bool expanded)
{
    return m_context != nullptr
               ? m_context->m_impl->setTreeViewItemExpandedFromUpdater(m_root, treeView, logicalIndex, expanded)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::scrollTreeViewToIndex(UINodeId treeView, u64 logicalIndex,
                                                  UITreeViewScrollAlignment alignment)
{
    return m_context != nullptr ? m_context->m_impl->scrollTreeViewToIndexFromUpdater(m_root, treeView, logicalIndex, alignment)
               : fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
}

Core::Status UITreeUpdater::setProgressBarRange(UINodeId progressBar, float minValue, float maxValue)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setProgressBarRangeFromUpdater(m_root, progressBar, minValue, maxValue);
}

Core::Status UITreeUpdater::setProgressBarValue(UINodeId progressBar, float value)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setProgressBarValueFromUpdater(m_root, progressBar, value);
}

Core::Result<float> UITreeUpdater::progressBarValue(UINodeId progressBar) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->progressBarValueFromUpdater(m_root, progressBar);
}

Core::Status UITreeUpdater::setProgressBarPaint(UINodeId progressBar, const UIProgressBarPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setProgressBarPaintFromUpdater(m_root, progressBar, paint);
}

Core::Result<UIProgressBarPaint> UITreeUpdater::progressBarPaint(UINodeId progressBar) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->progressBarPaintFromUpdater(m_root, progressBar);
}

Core::Status UITreeUpdater::setRadioButtonPaint(UINodeId radioButton, const UIRadioButtonPaint& paint)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setRadioButtonPaintFromUpdater(m_root, radioButton, paint);
}

Core::Result<UIRadioButtonPaint> UITreeUpdater::radioButtonPaint(UINodeId radioButton) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->radioButtonPaintFromUpdater(m_root, radioButton);
}

Core::Status UITreeUpdater::setRadioButtonAction(UINodeId radioButton, UIButtonActionCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setRadioButtonActionFromUpdater(m_root, radioButton, std::move(callback));
}

Core::Status UITreeUpdater::clearRadioButtonAction(UINodeId radioButton)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->clearRadioButtonActionFromUpdater(m_root, radioButton);
}

Core::Status UITreeUpdater::setRadioButtonSelected(UINodeId radioButton, bool selected)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->setRadioButtonSelectedFromUpdater(m_root, radioButton, selected);
}

Core::Result<bool> UITreeUpdater::isRadioButtonSelected(UINodeId radioButton) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isRadioButtonSelectedFromUpdater(m_root, radioButton);
}

Core::Result<bool> UITreeUpdater::isRadioButtonPressed(UINodeId radioButton) const
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->isRadioButtonPressedFromUpdater(m_root, radioButton);
}

Core::Result<UIRoutedPointerListenerToken>
UITreeUpdater::addRoutedPointerListener(UIRoutedPointerListenerDesc descriptor, UIRoutedPointerCallback callback)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    auto registration = m_context->m_impl->addRoutedPointerListenerFromUpdater(
        m_root, descriptor, std::move(callback));
    if (!registration)
    {
        return Core::failure(registration.error());
    }
    return UIRoutedPointerListenerToken{
        m_context->m_impl->lifetime,
        registration->first,
        registration->second,
    };
}

Core::Status UITreeUpdater::destroy(UINodeId node)
{
    if (m_context == nullptr)
    {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->m_impl->destroyFromUpdater(m_root, node);
}

UIRootBuilder UIAuthoring::rootBuilder() noexcept
{
    return UIRootBuilder(*m_context);
}

Core::Result<UITreeUpdater> UIAuthoring::treeUpdater(UIRootOwner& rootOwner)
{
    if (Core::Status ownerThread = m_context->m_impl->ensureOwnerThread(); !ownerThread)
    {
        return Core::failure(ownerThread.error());
    }
    m_context->m_impl->drainDeferredRootDestroys();
    if (!rootOwner.hasValue())
    {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a root owner");
    }
    if (rootOwner.rootNodeId().ownerWindow() != m_context->m_impl->ownerWindow)
    {
        return fail(UIErrorCode::WrongOwnerWindow, "UI root owner belongs to another owner window");
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = rootOwner.m_lifetime.lock();
    UIContext* attachedContext = lifetime ? lifetime->attachedContext() : nullptr;
    if (attachedContext == nullptr)
    {
        return fail(UIErrorCode::RootRequired, "UI root owner is detached");
    }
    if (attachedContext != m_context)
    {
        return fail(UIErrorCode::WrongContext, "UI root owner belongs to another context");
    }
    if (!m_context->m_impl->contains(rootOwner.rootNodeId()))
    {
        return fail(UIErrorCode::RootRequired, "UI root owner is no longer alive");
    }
    return UITreeUpdater(*m_context, rootOwner.rootNodeId());
}

void UIContext::Impl::destroyRootFromOwner(UINodeId root) noexcept
{
    if (!isOwnerThread())
    {
        return;
    }
    drainDeferredRootDestroys();
    destroyRootImmediately(root);
}

bool UIContext::Impl::isAliveInRoot(UINodeId updaterRoot, UINodeId node) const noexcept
{
    return isOwnerThread() && updaterRoot.hasValue() &&
           isNodeWithinRoot(updaterRoot, node);
}

void UIContext::Impl::releaseRoutedPointerListenerFromToken(
    u32 slot, u32 generation) noexcept
{
    if (isOwnerThread())
    {
        deactivateRoutedPointerListener(slot, generation, false);
    }
}

} // namespace Tina::UI
