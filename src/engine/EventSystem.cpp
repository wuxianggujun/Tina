#include "EventSystem.hpp"

#include "../ui/UINode.hpp"
#include "../core/Log.hpp"
#include "UIEvents.hpp"

#include <algorithm>

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

// ==================== focus and pointer capture ====================

bool EventSystem::setKeyboardFocus(UI::NodeId id)
{
    UI::UINode* next = resolveUINode(id);
    if (next && (!isInActiveUITree(id) || !next->isVisible() || !next->isEnabled() ||
                 !next->isFocusable())) {
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

bool EventSystem::focusNext(bool reverse)
{
    Vector<UI::NodeId> focusable;
    for (UI::NodeId rootId : m_uiContext.roots) {
        collectFocusableNodes(resolveUINode(rootId), focusable);
    }
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

    const bool handled = focused->onKeyPressed(key, isRepeat, shift, ctrl, alt);
    if (handled) return true;

    focused = resolveUINode(focusedId);
    if (!focused || m_uiContext.focusedNode != focusedId) return true;

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
    auto isActive = [this](UI::NodeId id) {
        UI::UINode* node = resolveUINode(id);
        return node && isInActiveUITree(id) && node->isVisible() && node->isEnabled();
    };

    if (m_uiContext.focusedNode) {
        UI::UINode* node = resolveUINode(m_uiContext.focusedNode);
        if (!isActive(m_uiContext.focusedNode) || !node->isFocusable()) setKeyboardFocus({});
    }
    if (m_uiContext.capturedNode && !isActive(m_uiContext.capturedNode)) {
        releasePointerCapture();
    }
    if (m_uiContext.pressedNode) {
        UI::UINode* node = resolveUINode(m_uiContext.pressedNode);
        if (!isActive(m_uiContext.pressedNode) || !node->isClickable()) {
            m_uiContext.pressedNode = {};
        }
    }
    if (m_uiContext.hoveredNode) {
        UI::UINode* node = resolveUINode(m_uiContext.hoveredNode);
        if (!isActive(m_uiContext.hoveredNode) || !node->isHoverable() || !node->isInteractable()) {
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
