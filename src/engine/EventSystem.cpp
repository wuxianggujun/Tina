#include "EventSystem.hpp"

#include "../ui/UINode.hpp"
#include "../core/Log.hpp"
#include "UIEvents.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::Engine {
namespace {

uint32_t nextGeneration(uint32_t generation)
{
    ++generation;
    return generation == 0 ? 1 : generation;
}

} // namespace

// ==================== UI node registry ====================

UI::NodeId EventSystem::attachUINode(UI::UINode* node)
{
    if (!node) return {};

    uint32_t index = 0;
    if (!m_freeUINodeSlots.empty()) {
        index = m_freeUINodeSlots.back();
        m_freeUINodeSlots.pop_back();
        m_uiNodeSlots[index].node = node;
    } else {
        index = static_cast<uint32_t>(m_uiNodeSlots.size());
        UINodeSlot slot;
        slot.node = node;
        m_uiNodeSlots.push_back(slot);
    }

    return UI::NodeId{index, m_uiNodeSlots[index].generation};
}

void EventSystem::detachUINode(UI::NodeId id, UI::UINode* node)
{
    if (!id || id.index >= m_uiNodeSlots.size()) return;

    UINodeSlot& slot = m_uiNodeSlots[id.index];
    if (slot.generation != id.generation || slot.node != node) return;

    m_uiContext.roots.erase(
        std::remove(m_uiContext.roots.begin(), m_uiContext.roots.end(), id),
        m_uiContext.roots.end());

    if (m_uiContext.focusedNode == id) setKeyboardFocus({});
    if (m_uiContext.capturedNode == id) releasePointerCapture(id);
    if (m_uiContext.hoveredNode == id) {
        m_uiContext.hoveredNode = {};
        if (node->isHoverable()) node->onMouseLeave();
    }
    if (m_uiContext.pressedNode == id) m_uiContext.pressedNode = {};

    slot.node = nullptr;
    slot.generation = nextGeneration(slot.generation);
    m_freeUINodeSlots.push_back(id.index);

    // Removing a root also invalidates interaction owned by still-registered
    // descendants. Resolve through generation handles before notifying them.
    sanitizeUIInteractionState();
}

void EventSystem::notifyUINodeStateChanged(UI::NodeId id)
{
    if (isUINodeAlive(id)) sanitizeUIInteractionState();
}

UI::UINode* EventSystem::resolveUINode(UI::NodeId id) const
{
    if (!id || id.index >= m_uiNodeSlots.size()) return nullptr;
    const UINodeSlot& slot = m_uiNodeSlots[id.index];
    return slot.generation == id.generation ? slot.node : nullptr;
}

UI::NodeId EventSystem::idForUINode(const UI::UINode* node) const
{
    if (!node) return {};
    const UI::NodeId id = node->nodeId();
    return resolveUINode(id) == node ? id : UI::NodeId{};
}

void EventSystem::invalidateAllUINodes()
{
    m_freeUINodeSlots.clear();
    m_freeUINodeSlots.reserve(m_uiNodeSlots.size());
    for (uint32_t i = 0; i < m_uiNodeSlots.size(); ++i) {
        UINodeSlot& slot = m_uiNodeSlots[i];
        slot.node = nullptr;
        slot.generation = nextGeneration(slot.generation);
        m_freeUINodeSlots.push_back(i);
    }
}

// ==================== UI roots ====================

void EventSystem::requestUILayoutForRoots()
{
    for (UI::NodeId rootId : m_uiContext.roots) {
        if (UI::UINode* root = resolveUINode(rootId)) root->requestLayout();
    }
}

bool EventSystem::updateUIViewport(int logicalWidth, int logicalHeight,
                                   int framebufferWidth, int framebufferHeight)
{
    const bool changed = m_windowUIContext.updateViewport(
        logicalWidth, logicalHeight, framebufferWidth, framebufferHeight);
    if (changed) requestUILayoutForRoots();
    return changed;
}

void EventSystem::setUITheme(UI::UITheme theme)
{
    m_windowUIContext.setTheme(std::move(theme));
    requestUILayoutForRoots();
}

bool EventSystem::setUIUserScale(float scale)
{
    const bool changed = m_windowUIContext.setUserScale(scale);
    if (changed) requestUILayoutForRoots();
    return changed;
}

void EventSystem::setUIRoot(Memory::SharedPtr<UI::UINode> root)
{
    Vector<UI::UINode*> roots;
    if (root) roots.push_back(root.get());
    setUIRoots(roots);
    m_uiContext.rootOwner = root;
    m_uiContext.rootOwnerRequired = static_cast<bool>(root);
}

void EventSystem::setUIRoot(UI::UINode* root)
{
    Vector<UI::UINode*> roots;
    if (root) roots.push_back(root);
    setUIRoots(roots);
}

void EventSystem::setUIRoots(const Vector<UI::UINode*>& roots)
{
    Vector<UI::NodeId> rootIds;
    rootIds.reserve(roots.size());
    for (UI::UINode* root : roots) {
        if (!root) continue;
        if (root->eventSystem() != this) root->setEventSystem(this);
        if (resolveUINode(root->nodeId()) == root) rootIds.push_back(root->nodeId());
    }

    bool changed = rootIds.size() != m_uiContext.roots.size();
    if (!changed) {
        for (size_t i = 0; i < rootIds.size(); ++i) {
            if (rootIds[i] != m_uiContext.roots[i]) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        m_uiContext.roots = Container::Move(rootIds);
        sanitizeUIInteractionState();
    }

    m_uiContext.rootOwner.reset();
    m_uiContext.rootOwnerRequired = false;
}

bool EventSystem::isInActiveUITree(UI::NodeId id) const
{
    UI::UINode* node = resolveUINode(id);
    if (!node) return false;

    UI::UINode* root = node;
    while (root->getParent()) root = root->getParent();
    const UI::NodeId rootId = root->nodeId();
    for (UI::NodeId activeRoot : m_uiContext.roots) {
        if (activeRoot == rootId && resolveUINode(activeRoot) == root) return true;
    }
    return false;
}

bool EventSystem::isNodeAvailableForInteraction(UI::NodeId id) const
{
    UI::UINode* node = resolveUINode(id);
    if (!node || !isInActiveUITree(id)) return false;

    for (UI::UINode* current = node; current; current = current->getParent()) {
        if (resolveUINode(current->nodeId()) != current ||
            !current->isVisible() || !current->isEnabled()) {
            return false;
        }
    }
    return true;
}

bool EventSystem::isNodeWithinSubtree(UI::NodeId id, UI::NodeId root) const
{
    UI::UINode* node = resolveUINode(id);
    UI::UINode* scopeRoot = resolveUINode(root);
    if (!node || !scopeRoot) return false;

    for (UI::UINode* current = node; current; current = current->getParent()) {
        if (resolveUINode(current->nodeId()) != current) return false;
        if (current == scopeRoot) return true;
    }
    return false;
}

bool EventSystem::isNodeWithinActiveFocusScope(UI::NodeId id) const
{
    return m_focusScopes.empty() || isNodeWithinSubtree(id, m_focusScopes.back().root);
}

// ==================== focus and pointer capture ====================

bool EventSystem::setKeyboardFocus(UI::NodeId id)
{
    UI::UINode* next = resolveUINode(id);
    if (next && (!isNodeAvailableForInteraction(id) || !next->isFocusable() ||
                 !isNodeWithinActiveFocusScope(id))) {
        return false;
    }
    if (!next) id = {};

    const UI::NodeId previousId = m_uiContext.focusedNode;
    if (previousId == id) return true;

    m_uiContext.focusedNode = id;

    if (UI::UINode* previous = resolveUINode(previousId)) {
        previous->onFocusLost();
        if (resolveUINode(previousId) == previous) {
            FocusLostEvent event;
            triggerUIEvent(event, previous);
        }
    }

    // A focus callback may have made a nested focus request.
    if (m_uiContext.focusedNode != id) return true;

    if (UI::UINode* current = resolveUINode(id)) {
        current->onFocusGained();
        if (resolveUINode(id) == current) {
            FocusGainedEvent event;
            triggerUIEvent(event, current);
        }
    }
    return true;
}

bool EventSystem::beginFocusScope(UI::NodeId root, UI::NodeId initialFocus)
{
    sanitizeUIInteractionState();
    UI::UINode* scopeRoot = resolveUINode(root);
    if (!scopeRoot || !isNodeAvailableForInteraction(root)) return false;

    if (!m_focusScopes.empty() && !isNodeWithinSubtree(root, m_focusScopes.back().root)) {
        return false;
    }
    for (const UIFocusScopeEntry& scope : m_focusScopes) {
        if (scope.root == root) return scope.root == activeFocusScopeId();
    }

    const UI::NodeId previousFocus = m_uiContext.focusedNode;
    m_focusScopes.push_back({root, previousFocus});

    auto isFocusableInScope = [this, root](UI::NodeId id) {
        UI::UINode* node = resolveUINode(id);
        return node && node->isFocusable() && isNodeAvailableForInteraction(id) &&
               isNodeWithinSubtree(id, root);
    };

    UI::NodeId nextFocus = initialFocus;
    if (!isFocusableInScope(nextFocus)) {
        nextFocus = isFocusableInScope(previousFocus) ? previousFocus : UI::NodeId{};
    }
    if (!nextFocus) {
        Vector<UI::NodeId> focusable;
        collectFocusableNodes(scopeRoot, focusable);
        if (!focusable.empty()) nextFocus = focusable.front();
    }

    return nextFocus ? setKeyboardFocus(nextFocus) : setKeyboardFocus({});
}

bool EventSystem::endFocusScope(UI::NodeId root)
{
    if (!root || m_focusScopes.empty()) return false;

    for (size_t index = m_focusScopes.size(); index > 0; --index) {
        if (m_focusScopes[index - 1].root != root) continue;

        const UI::NodeId restoreFocus = m_focusScopes[index - 1].restoreFocus;
        m_focusScopes.resize(index - 1);
        restoreFocusAfterScopeChange(restoreFocus);
        return true;
    }
    return false;
}

void EventSystem::collectFocusableNodes(UI::UINode* node, Vector<UI::NodeId>& nodes) const
{
    if (!node || !node->isVisible() || !node->isEnabled()) return;
    if (node->isFocusable() && resolveUINode(node->nodeId()) == node) {
        nodes.push_back(node->nodeId());
    }
    for (const auto& child : node->getChildren()) {
        if (child) collectFocusableNodes(child.get(), nodes);
    }
}

void EventSystem::collectFocusTraversalNodes(Vector<UI::NodeId>& nodes) const
{
    if (!m_focusScopes.empty()) {
        collectFocusableNodes(resolveUINode(m_focusScopes.back().root), nodes);
        return;
    }
    for (UI::NodeId rootId : m_uiContext.roots) {
        collectFocusableNodes(resolveUINode(rootId), nodes);
    }
}

void EventSystem::restoreFocusAfterScopeChange(UI::NodeId preferred)
{
    UI::UINode* preferredNode = resolveUINode(preferred);
    if (preferredNode && preferredNode->isFocusable() &&
        isNodeAvailableForInteraction(preferred) &&
        isNodeWithinActiveFocusScope(preferred) && setKeyboardFocus(preferred)) {
        return;
    }

    Vector<UI::NodeId> focusable;
    collectFocusTraversalNodes(focusable);
    if (!focusable.empty()) {
        setKeyboardFocus(focusable.front());
    } else {
        setKeyboardFocus({});
    }
}

bool EventSystem::focusNext(bool reverse)
{
    sanitizeUIInteractionState();
    Vector<UI::NodeId> focusable;
    collectFocusTraversalNodes(focusable);
    if (focusable.empty()) return false;

    int currentIndex = -1;
    for (size_t i = 0; i < focusable.size(); ++i) {
        if (focusable[i] == m_uiContext.focusedNode) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    size_t nextIndex = 0;
    if (reverse) {
        nextIndex = currentIndex < 0
            ? focusable.size() - 1
            : (static_cast<size_t>(currentIndex) + focusable.size() - 1) % focusable.size();
    } else {
        nextIndex = static_cast<size_t>(currentIndex + 1) % focusable.size();
    }
    return setKeyboardFocus(focusable[nextIndex]);
}

bool EventSystem::focusDirectional(UIFocusDirection direction)
{
    sanitizeUIInteractionState();

    Vector<UI::NodeId> focusable;
    collectFocusTraversalNodes(focusable);
    if (focusable.empty()) return false;

    UI::UINode* current = resolveUINode(m_uiContext.focusedNode);
    if (!current) return setKeyboardFocus(focusable.front());

    const Math::Vec2 currentPosition = current->getWorldPosition();
    const Math::Vec2 currentSize = current->getSize();
    const float currentLeft = currentPosition.x;
    const float currentRight = currentPosition.x + currentSize.x;
    const float currentTop = currentPosition.y;
    const float currentBottom = currentPosition.y + currentSize.y;
    const float currentCenterX = (currentLeft + currentRight) * 0.5f;
    const float currentCenterY = (currentTop + currentBottom) * 0.5f;

    auto intervalGap = [](float firstMin, float firstMax,
                          float secondMin, float secondMax) {
        if (secondMin > firstMax) return secondMin - firstMax;
        if (firstMin > secondMax) return firstMin - secondMax;
        return 0.0f;
    };

    UI::NodeId bestId;
    bool bestOutsideBeam = true;
    float bestScore = std::numeric_limits<float>::max();
    size_t bestOrder = std::numeric_limits<size_t>::max();

    for (size_t order = 0; order < focusable.size(); ++order) {
        const UI::NodeId candidateId = focusable[order];
        if (candidateId == m_uiContext.focusedNode) continue;

        UI::UINode* candidate = resolveUINode(candidateId);
        if (!candidate) continue;

        const Math::Vec2 candidatePosition = candidate->getWorldPosition();
        const Math::Vec2 candidateSize = candidate->getSize();
        const float candidateLeft = candidatePosition.x;
        const float candidateRight = candidatePosition.x + candidateSize.x;
        const float candidateTop = candidatePosition.y;
        const float candidateBottom = candidatePosition.y + candidateSize.y;
        const float candidateCenterX = (candidateLeft + candidateRight) * 0.5f;
        const float candidateCenterY = (candidateTop + candidateBottom) * 0.5f;

        float primaryDistance = 0.0f;
        float crossDistance = 0.0f;
        float crossGap = 0.0f;
        switch (direction) {
            case UIFocusDirection::Left:
                primaryDistance = currentCenterX - candidateCenterX;
                crossDistance = candidateCenterY - currentCenterY;
                crossGap = intervalGap(currentTop, currentBottom,
                                       candidateTop, candidateBottom);
                break;
            case UIFocusDirection::Right:
                primaryDistance = candidateCenterX - currentCenterX;
                crossDistance = candidateCenterY - currentCenterY;
                crossGap = intervalGap(currentTop, currentBottom,
                                       candidateTop, candidateBottom);
                break;
            case UIFocusDirection::Up:
                primaryDistance = currentCenterY - candidateCenterY;
                crossDistance = candidateCenterX - currentCenterX;
                crossGap = intervalGap(currentLeft, currentRight,
                                       candidateLeft, candidateRight);
                break;
            case UIFocusDirection::Down:
                primaryDistance = candidateCenterY - currentCenterY;
                crossDistance = candidateCenterX - currentCenterX;
                crossGap = intervalGap(currentLeft, currentRight,
                                       candidateLeft, candidateRight);
                break;
        }
        if (primaryDistance <= 0.0f) continue;

        // Candidates that overlap the perpendicular beam are preferred even
        // when a diagonal candidate is slightly closer. Within each class,
        // use a deterministic weighted distance and finally tree order.
        const bool outsideBeam = crossGap > 0.0f;
        const float crossWeight = outsideBeam ? 4.0f : 0.25f;
        const float score = primaryDistance * primaryDistance +
                            crossDistance * crossDistance * crossWeight;
        const bool betterBeam = bestId && bestOutsideBeam && !outsideBeam;
        const bool sameBeam = !bestId || outsideBeam == bestOutsideBeam;
        const bool betterScore = sameBeam &&
            (score < bestScore ||
             (std::abs(score - bestScore) <= 0.001f && order < bestOrder));
        if (!bestId || betterBeam || betterScore) {
            bestId = candidateId;
            bestOutsideBeam = outsideBeam;
            bestScore = score;
            bestOrder = order;
        }
    }

    return bestId ? setKeyboardFocus(bestId) : false;
}

bool EventSystem::dispatchUINavigationAction(UINavigationAction action,
                                             UINavigationPhase phase)
{
    if (phase != UINavigationPhase::Released) {
        switch (action) {
            case UINavigationAction::Left:
                return focusDirectional(UIFocusDirection::Left);
            case UINavigationAction::Right:
                return focusDirectional(UIFocusDirection::Right);
            case UINavigationAction::Up:
                return focusDirectional(UIFocusDirection::Up);
            case UINavigationAction::Down:
                return focusDirectional(UIFocusDirection::Down);
            case UINavigationAction::Accept:
            case UINavigationAction::Cancel:
                break;
        }
    }

    if (action != UINavigationAction::Accept &&
        action != UINavigationAction::Cancel) {
        return false;
    }

    const KeyCode key = action == UINavigationAction::Accept
        ? KeyCode::Enter
        : KeyCode::Escape;
    if (phase == UINavigationPhase::Released) {
        return dispatchKeyReleasedToFocused(key);
    }
    return dispatchKeyPressedToFocused(
        key, phase == UINavigationPhase::Repeated);
}

bool EventSystem::dispatchKeyPressedToFocused(KeyCode key, bool isRepeat,
                                              bool shift, bool ctrl, bool alt)
{
    sanitizeUIInteractionState();
    const UI::NodeId focusedId = m_uiContext.focusedNode;
    UI::UINode* focused = resolveUINode(focusedId);
    if (!focused || !isInActiveUITree(focusedId)) return false;

    UIKeyPressedEvent routedEvent(key, isRepeat, shift, ctrl, alt);
    triggerUIEvent(routedEvent, focused);
    if (routedEvent.defaultPrevented) return true;

    // A routed-event callback may remove the target or move focus. Never run a
    // default action through the stale raw pointer captured above.
    focused = resolveUINode(focusedId);
    if (!focused || m_uiContext.focusedNode != focusedId) return true;

    Vector<UI::NodeId> defaultPath;
    buildEventPath(focused, defaultPath);
    for (size_t index = defaultPath.size(); index > 0; --index) {
        UI::UINode* handler = resolveUINode(defaultPath[index - 1]);
        if (!handler) return true;
        if (handler->onKeyPressed(key, isRepeat, shift, ctrl, alt)) return true;

        focused = resolveUINode(focusedId);
        if (!focused || m_uiContext.focusedNode != focusedId) return true;
    }

    const bool isActivationKey = key == KeyCode::Enter ||
                                 key == KeyCode::NumpadEnter ||
                                 key == KeyCode::Space;
    if (!isRepeat && !ctrl && !alt && isActivationKey &&
        focused->supportsKeyboardActivation() && focused->isEnabled() &&
        focused->isInteractable()) {
        focused->onClick();
        return true;
    }
    return false;
}

bool EventSystem::dispatchKeyReleasedToFocused(KeyCode key, bool shift,
                                               bool ctrl, bool alt)
{
    sanitizeUIInteractionState();
    const UI::NodeId focusedId = m_uiContext.focusedNode;
    UI::UINode* focused = resolveUINode(focusedId);
    if (!focused || !isInActiveUITree(focusedId)) return false;

    UIKeyReleasedEvent routedEvent(key, shift, ctrl, alt);
    triggerUIEvent(routedEvent, focused);

    // Key release is a lifecycle cleanup point. Propagation controls do not
    // suppress the focused control's local cleanup, but generation and focus
    // are revalidated before calling it.
    focused = resolveUINode(focusedId);
    if (!focused || m_uiContext.focusedNode != focusedId) return true;
    return focused->onKeyReleased(key, shift, ctrl, alt);
}

bool EventSystem::setPointerCapture(UI::NodeId id)
{
    UI::UINode* next = resolveUINode(id);
    if (!next || !isInActiveUITree(id) || !next->isVisible() || !next->isEnabled()) {
        return false;
    }

    const UI::NodeId previousId = m_uiContext.capturedNode;
    if (previousId == id) return true;
    m_uiContext.capturedNode = id;

    if (UI::UINode* previous = resolveUINode(previousId)) {
        previous->onPointerCaptureChanged(false);
        if (resolveUINode(previousId) == previous) {
            PointerCaptureChangedEvent event;
            event.previousCapture = previousId;
            event.nextCapture = id;
            event.captured = false;
            triggerUIEvent(event, previous);
        }
    }

    if (m_uiContext.capturedNode != id) return true;
    if (UI::UINode* current = resolveUINode(id)) {
        current->onPointerCaptureChanged(true);
        if (resolveUINode(id) == current) {
            PointerCaptureChangedEvent event;
            event.previousCapture = previousId;
            event.nextCapture = id;
            event.captured = true;
            triggerUIEvent(event, current);
        }
    }
    return true;
}

void EventSystem::releasePointerCapture(UI::NodeId requester)
{
    const UI::NodeId previousId = m_uiContext.capturedNode;
    if (!previousId || (requester && requester != previousId)) return;

    m_uiContext.capturedNode = {};
    if (UI::UINode* previous = resolveUINode(previousId)) {
        previous->onPointerCaptureChanged(false);
        if (resolveUINode(previousId) == previous) {
            PointerCaptureChangedEvent event;
            event.previousCapture = previousId;
            event.nextCapture = {};
            event.captured = false;
            triggerUIEvent(event, previous);
        }
    }
}

void EventSystem::sanitizeUIInteractionState()
{
    UI::NodeId restoreFocus;
    bool removedFocusScope = false;
    while (!m_focusScopes.empty()) {
        const size_t last = m_focusScopes.size() - 1;
        const UIFocusScopeEntry& scope = m_focusScopes[last];
        bool valid = isNodeAvailableForInteraction(scope.root);
        if (valid && last > 0) {
            valid = isNodeWithinSubtree(scope.root, m_focusScopes[last - 1].root);
        }
        if (valid) break;

        restoreFocus = scope.restoreFocus;
        m_focusScopes.pop_back();
        removedFocusScope = true;
    }
    if (removedFocusScope) restoreFocusAfterScopeChange(restoreFocus);

    if (m_uiContext.focusedNode) {
        UI::UINode* node = resolveUINode(m_uiContext.focusedNode);
        if (!isNodeAvailableForInteraction(m_uiContext.focusedNode) ||
            !node->isFocusable() ||
            !isNodeWithinActiveFocusScope(m_uiContext.focusedNode)) {
            restoreFocusAfterScopeChange({});
        }
    }
    if (m_uiContext.capturedNode &&
        !isNodeAvailableForInteraction(m_uiContext.capturedNode)) {
        releasePointerCapture();
    }
    if (m_uiContext.pressedNode) {
        UI::UINode* node = resolveUINode(m_uiContext.pressedNode);
        if (!isNodeAvailableForInteraction(m_uiContext.pressedNode) ||
            !node->isClickable()) {
            m_uiContext.pressedNode = {};
        }
    }
    if (m_uiContext.hoveredNode) {
        UI::UINode* node = resolveUINode(m_uiContext.hoveredNode);
        if (!isNodeAvailableForInteraction(m_uiContext.hoveredNode) ||
            !node->isHoverable() || !node->isInteractable()) {
            const UI::NodeId oldId = m_uiContext.hoveredNode;
            m_uiContext.hoveredNode = {};
            if (UI::UINode* old = resolveUINode(oldId)) old->onMouseLeave();
        }
    }
}

void EventSystem::resetUIInteractionState(bool notifyNodes)
{
    const UI::NodeId hovered = m_uiContext.hoveredNode;
    const UI::NodeId focused = m_uiContext.focusedNode;
    const UI::NodeId captured = m_uiContext.capturedNode;

    m_uiContext.hoveredNode = {};
    m_uiContext.pressedNode = {};
    m_uiContext.focusedNode = {};
    m_uiContext.capturedNode = {};
    m_focusScopes.clear();

    if (!notifyNodes) return;
    if (UI::UINode* node = resolveUINode(hovered)) node->onMouseLeave();
    if (UI::UINode* node = resolveUINode(focused)) node->onFocusLost();
    if (UI::UINode* node = resolveUINode(captured)) node->onPointerCaptureChanged(false);
}

// ==================== input update and routed dispatch ====================

void EventSystem::updateUIInput(float mouseX, float mouseY, bool mouseDown)
{
    updateUIInput(mouseX, mouseY, mouseDown, 0.0f);
}

void EventSystem::updateUIInput(float mouseX, float mouseY, bool mouseDown, float wheelDeltaY)
{
    const bool pointerMoved = m_uiContext.hasPointerPosition &&
        (mouseX != m_uiContext.mouseX || mouseY != m_uiContext.mouseY);

    m_uiContext.previousMouseX = m_uiContext.hasPointerPosition ? m_uiContext.mouseX : mouseX;
    m_uiContext.previousMouseY = m_uiContext.hasPointerPosition ? m_uiContext.mouseY : mouseY;
    m_uiContext.mouseX = mouseX;
    m_uiContext.mouseY = mouseY;
    m_uiContext.mouseDownPrev = m_uiContext.mouseDown;
    m_uiContext.mouseDown = mouseDown;
    m_uiContext.hasPointerPosition = true;

    handleMouseInput(wheelDeltaY, pointerMoved);
}

void EventSystem::updateUIInputLogical(float mouseX, float mouseY, bool mouseDown,
                                       float wheelDeltaY)
{
    const UI::UIViewportMetrics& viewport = m_windowUIContext.viewport();
    updateUIInput(
        viewport.toFramebufferX(mouseX),
        viewport.toFramebufferY(mouseY),
        mouseDown,
        wheelDeltaY);
}

void EventSystem::buildEventPath(UI::UINode* target, Vector<UI::NodeId>& path)
{
    UI::UINode* current = target;
    while (current) {
        const UI::NodeId id = current->nodeId();
        if (resolveUINode(id) != current) {
            path.clear();
            return;
        }
        path.push_back(id);
        current = current->getParent();
    }
    Container::Reverse(path.begin(), path.end());
}

UI::NodeId EventSystem::findNodeUnderMouse(UI::UINode* node, float x, float y)
{
    if (!node || resolveUINode(node->nodeId()) != node ||
        !node->isVisible() || !node->isEnabled()) {
        return {};
    }

    const bool insideNode = node->containsPoint(x, y);
    if (!node->clipsChildren() || insideNode) {
        const auto& children = node->getChildren();
        Vector<UI::UINode*> sortedChildren;
        sortedChildren.reserve(children.size());
        for (const auto& child : children) {
            if (child) sortedChildren.push_back(child.get());
        }
        Container::Sort(sortedChildren.begin(), sortedChildren.end(),
            [](UI::UINode* lhs, UI::UINode* rhs) { return lhs->zIndex() > rhs->zIndex(); });

        for (UI::UINode* child : sortedChildren) {
            UI::NodeId found = findNodeUnderMouse(child, x, y);
            if (found) return found;
        }
    }

    if (node->isInteractable() && insideNode) return node->nodeId();
    return {};
}

void EventSystem::handleMouseInput(float wheelDeltaY, bool pointerMoved)
{
    if (m_uiContext.rootOwnerRequired && m_uiContext.rootOwner.expired()) {
        m_uiContext.roots.clear();
        resetUIInteractionState(true);
        m_uiContext.rootOwnerRequired = false;
        return;
    }

    sanitizeUIInteractionState();
    if (m_uiContext.roots.empty()) return;

    const float mouseX = m_uiContext.mouseX;
    const float mouseY = m_uiContext.mouseY;

    UI::NodeId hitId;
    for (size_t i = m_uiContext.roots.size(); i > 0 && !hitId; --i) {
        hitId = findNodeUnderMouse(resolveUINode(m_uiContext.roots[i - 1]), mouseX, mouseY);
    }

    if (hitId != m_uiContext.hoveredNode) {
        const UI::NodeId oldHover = m_uiContext.hoveredNode;
        m_uiContext.hoveredNode = hitId;

        if (UI::UINode* old = resolveUINode(oldHover); old && old->isHoverable()) {
            MouseLeaveEvent event;
            event.mouseX = mouseX;
            event.mouseY = mouseY;
            triggerUIEvent(event, old);
            if (resolveUINode(oldHover) == old) old->onMouseLeave();
        }
        if (UI::UINode* current = resolveUINode(hitId); current && current->isHoverable()) {
            MouseEnterEvent event;
            event.mouseX = mouseX;
            event.mouseY = mouseY;
            triggerUIEvent(event, current);
            if (resolveUINode(hitId) == current) current->onMouseEnter();
        }
    }

    const UI::NodeId routedPointer = m_uiContext.capturedNode
        ? m_uiContext.capturedNode
        : hitId;
    if (pointerMoved) {
        if (UI::UINode* target = resolveUINode(routedPointer)) {
            PointerMoveEvent event;
            event.mouseX = mouseX;
            event.mouseY = mouseY;
            event.deltaX = mouseX - m_uiContext.previousMouseX;
            event.deltaY = mouseY - m_uiContext.previousMouseY;
            triggerUIEvent(event, target);
            if (resolveUINode(routedPointer) == target) target->onPointerMove(mouseX, mouseY);
        }
    }

    if (m_uiContext.mouseDown && !m_uiContext.mouseDownPrev) {
        if (UI::UINode* target = resolveUINode(hitId)) {
            if (target->isFocusable()) setKeyboardFocus(hitId);
            else clearKeyboardFocus();

            if (target->isClickable()) {
                m_uiContext.pressedNode = hitId;
                setPointerCapture(hitId);
            }

            if (UI::UINode* liveTarget = resolveUINode(hitId)) {
                PointerDownEvent event;
                event.mouseX = mouseX;
                event.mouseY = mouseY;
                triggerUIEvent(event, liveTarget);
                if (resolveUINode(hitId) == liveTarget && liveTarget->isClickable()) {
                    liveTarget->onMouseDown(mouseX, mouseY);
                }
            }
        } else {
            clearKeyboardFocus();
        }
    }

    if (!m_uiContext.mouseDown && m_uiContext.mouseDownPrev) {
        const UI::NodeId pressedId = m_uiContext.pressedNode;
        const UI::NodeId releaseTargetId = m_uiContext.capturedNode
            ? m_uiContext.capturedNode
            : hitId;

        if (UI::UINode* target = resolveUINode(releaseTargetId)) {
            PointerUpEvent event;
            event.mouseX = mouseX;
            event.mouseY = mouseY;
            triggerUIEvent(event, target);
            if (resolveUINode(releaseTargetId) == target) target->onMouseUp(mouseX, mouseY);
        }

        if (pressedId && pressedId == hitId) {
            if (UI::UINode* target = resolveUINode(pressedId)) {
                MouseClickEvent event;
                event.mouseX = mouseX;
                event.mouseY = mouseY;
                triggerUIEvent(event, target);
                if (resolveUINode(pressedId) == target) target->onClick();
            }
        }

        m_uiContext.pressedNode = {};
        releasePointerCapture();
    }

    if (wheelDeltaY != 0.0f) {
        UI::UINode* wheelTarget = resolveUINode(hitId);
        while (wheelTarget && !wheelTarget->acceptsMouseWheel()) {
            wheelTarget = wheelTarget->getParent();
        }
        if (wheelTarget) {
            const UI::NodeId wheelTargetId = wheelTarget->nodeId();
            UIMouseWheelEvent event;
            event.deltaY = wheelDeltaY;
            event.mouseX = mouseX;
            event.mouseY = mouseY;
            triggerUIEvent(event, wheelTarget);
            if (resolveUINode(wheelTargetId) == wheelTarget) {
                wheelTarget->onMouseWheel(0.0f, wheelDeltaY);
            }
        }
    }
}

} // namespace Tina::Engine
