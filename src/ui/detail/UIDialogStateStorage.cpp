#include "UIDialogStateStorage.hpp"

#include <cassert>
#include <utility>

namespace Tina::UI::Detail {

UIDialogStateStorage::UIDialogStateStorage(
    usize capacity, std::pmr::memory_resource& resource)
    : states_(capacity, resource)
{}

usize UIDialogStateStorage::capacity() const noexcept
{
    return states_.capacity();
}

usize UIDialogStateStorage::activeCount() const noexcept
{
    return states_.size();
}

usize UIDialogStateStorage::availableCount() const noexcept
{
    return states_.availableCount();
}

bool UIDialogStateStorage::containsDialog(UINodeId dialog) const noexcept
{
    return states_.contains(dialog);
}

DialogState* UIDialogStateStorage::tryState(UINodeId dialog) noexcept
{
    return const_cast<DialogState*>(std::as_const(*this).tryState(dialog));
}

const DialogState* UIDialogStateStorage::tryState(UINodeId dialog) const noexcept
{
    return states_.tryGet(dialog);
}

bool UIDialogStateStorage::initializeDialog(const UIDialogParts& parts) noexcept
{
    assert(parts.modal.hasValue());
    resetNode(parts.modal.index());
    return states_.insertOrAssign(DialogState{
        .node = parts.modal,
        .parts = parts,
    });
}

void UIDialogStateStorage::resetNode(u32 nodeIndex) noexcept
{
    DialogState* state = states_.tryGetByIndex(nodeIndex);
    if (state == nullptr)
    {
        return;
    }
    if (activeDialog_ == state->node)
    {
        activeDialog_ = {};
    }
    static_cast<void>(states_.eraseByIndex(nodeIndex));
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
    tryState(dialog)->open = true;
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
