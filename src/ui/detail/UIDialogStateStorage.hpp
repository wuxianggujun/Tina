#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIDialog.hpp>

#include "UIBoundedNodeStateTable.hpp"

#include <memory_resource>
#include <vector>

namespace Tina::UI::Detail {

struct DialogState final {
    UINodeId node{};
    UIDialogParts parts{};
    bool open = false;
};

// Bounded sparse presentation state owned by one UIContext.
// UIContext validates root ownership and preflights dirty publication before
// mutating this storage.
class UIDialogStateStorage final {
  public:
    UIDialogStateStorage(usize capacity, std::pmr::memory_resource& resource);

    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize availableCount() const noexcept;
    [[nodiscard]] bool containsDialog(UINodeId dialog) const noexcept;
    [[nodiscard]] DialogState* tryState(UINodeId dialog) noexcept;
    [[nodiscard]] const DialogState* tryState(UINodeId dialog) const noexcept;

    [[nodiscard]] bool initializeDialog(const UIDialogParts& parts) noexcept;
    void resetNode(u32 nodeIndex) noexcept;
    [[nodiscard]] bool releaseNode(UINodeId node) noexcept;

    [[nodiscard]] bool isOpen(UINodeId dialog) const noexcept;
    [[nodiscard]] UINodeId activeDialog() const noexcept;
    void openValidated(UINodeId dialog) noexcept;
    [[nodiscard]] bool dismiss(UINodeId dialog) noexcept;

  private:
    UIBoundedNodeStateTable<DialogState> states_;
    UINodeId activeDialog_{};
};

} // namespace Tina::UI::Detail
