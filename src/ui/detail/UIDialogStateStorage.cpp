#include "UIDialogStateStorage.hpp"

#include <cassert>
#include <utility>

namespace Tina::UI::Detail {

UIDialogStateStorage::UIDialogStateStorage(
    usize nodeCapacity, std::pmr::memory_resource& resource)
    : statesByNodeIndex_(&resource)
{
    statesByNodeIndex_.resize(nodeCapacity);
}

usize UIDialogStateStorage::capacity() const noexcept
{
    return statesByNodeIndex_.size();
}

bool UIDialogStateStorage::containsDialog(UINodeId dialog) const noexcept
{
    return dialog.hasValue() && dialog.index() < statesByNodeIndex_.size() &&
           statesByNodeIndex_[dialog.index()].node == dialog;
}

DialogState* UIDialogStateStorage::tryState(UINodeId dialog) noexcept
{
    return const_cast<DialogState*>(std::as_const(*this).tryState(dialog));
}

const DialogState* UIDialogStateStorage::tryState(UINodeId dialog) const noexcept
{
    return containsDialog(dialog) ? &statesByNodeIndex_[dialog.index()] : nullptr;
}

void UIDialogStateStorage::initializeDialog(const UIDialogParts& parts) noexcept
{
    assert(parts.modal.hasValue() &&
           parts.modal.index() < statesByNodeIndex_.size());
    resetNode(parts.modal.index());
    statesByNodeIndex_[parts.modal.index()] = DialogState{
        .node = parts.modal,
        .parts = parts,
    };
}

void UIDialogStateStorage::resetNode(u32 nodeIndex) noexcept
{
    if (nodeIndex >= statesByNodeIndex_.size())
    {
        return;
    }
    if (activeDialog_ == statesByNodeIndex_[nodeIndex].node)
    {
        activeDialog_ = {};
    }
    statesByNodeIndex_[nodeIndex] = {};
}

bool UIDialogStateStorage::releaseNode(UINodeId node) noexcept
{
    if (!containsDialog(node))
    {
        return false;
    }
    const bool changed = isOpen(node);
    resetNode(node.index());
    return changed;
}

bool UIDialogStateStorage::isOpen(UINodeId dialog) const noexcept
{
    const DialogState* state = tryState(dialog);
    return state != nullptr && state->open && activeDialog_ == dialog;
}

UINodeId UIDialogStateStorage::activeDialog() const noexcept
{
    return isOpen(activeDialog_) ? activeDialog_ : UINodeId{};
}

void UIDialogStateStorage::openValidated(UINodeId dialog) noexcept
{
    assert(containsDialog(dialog));
    assert(!activeDialog().hasValue() || activeDialog_ == dialog);
    statesByNodeIndex_[dialog.index()].open = true;
    activeDialog_ = dialog;
}

bool UIDialogStateStorage::dismiss(UINodeId dialog) noexcept
{
    DialogState* state = tryState(dialog);
    if (state == nullptr)
    {
        return false;
    }
    const bool changed = state->open || activeDialog_ == dialog;
    state->open = false;
    if (activeDialog_ == dialog)
    {
        activeDialog_ = {};
    }
    return changed;
}

} // namespace Tina::UI::Detail
