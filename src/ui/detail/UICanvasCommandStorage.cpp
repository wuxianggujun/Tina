#include "UICanvasCommandStorage.hpp"
#include "UIPropertyNormalization.hpp"

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] bool hasValidBounds(const UILogicalRect& bounds) noexcept
{
    return std::isfinite(bounds.x) && std::isfinite(bounds.y) &&
           std::isfinite(bounds.width) && bounds.width >= 0.0F &&
           std::isfinite(bounds.height) && bounds.height >= 0.0F;
}

[[nodiscard]] bool hasValidDestinationInsets(const UIEdgeSpacing& insets) noexcept
{
    return std::isfinite(insets.left) && insets.left >= 0.0F &&
           std::isfinite(insets.top) && insets.top >= 0.0F &&
           std::isfinite(insets.right) && insets.right >= 0.0F &&
           std::isfinite(insets.bottom) && insets.bottom >= 0.0F;
}

[[nodiscard]] bool hasValidSourceInsets(const UIImageSource& source,
                                        const UIImagePixelInsets& insets) noexcept
{
    return insets.left <= source.sourcePixels.width &&
           insets.right <= source.sourcePixels.width - insets.left &&
           insets.top <= source.sourcePixels.height &&
           insets.bottom <= source.sourcePixels.height - insets.top;
}

[[nodiscard]] bool hasZeroInsets(const UIImagePixelInsets& sourceInsets,
                                 const UIEdgeSpacing& destinationInsets) noexcept
{
    return sourceInsets == UIImagePixelInsets{} && destinationInsets == UIEdgeSpacing{};
}

[[nodiscard]] bool isValidCanvasCommand(const UICanvasCommand& command) noexcept
{
    if (!hasValidBounds(command.bounds) || !std::isfinite(command.cornerRadius) ||
        command.cornerRadius < 0.0F)
    {
        return false;
    }

    switch (command.kind)
    {
    case UICanvasCommandKind::SolidRect:
        return true;
    case UICanvasCommandKind::Image:
        return command.cornerRadius == 0.0F && isValidImageSource(command.imageSource) &&
               isValidImageSampling(command.imageSampling) &&
               hasZeroInsets(command.imageSourceInsets, command.imageDestinationInsets);
    case UICanvasCommandKind::NineSlice:
        return command.cornerRadius == 0.0F && isValidImageSource(command.imageSource) &&
               isValidImageSampling(command.imageSampling) &&
               hasValidSourceInsets(command.imageSource, command.imageSourceInsets) &&
               hasValidDestinationInsets(command.imageDestinationInsets);
    }
    return false;
}

} // namespace

UICanvasCommandStorage::UICanvasCommandStorage(usize nodeCapacity, usize commandCapacity,
                                               std::pmr::memory_resource& resource)
    : statesByNodeIndex_(&resource), slots_(&resource)
{
    statesByNodeIndex_.resize(nodeCapacity);
    slots_.resize(commandCapacity);
    for (usize commandIndex = 0; commandIndex < commandCapacity; ++commandIndex)
    {
        slots_[commandIndex].next =
            commandIndex + 1U < commandCapacity ? static_cast<u32>(commandIndex + 1U) : InvalidCommandIndex;
    }
    freeHead_ = commandCapacity == 0 ? InvalidCommandIndex : 0U;
}

Core::Status UICanvasCommandStorage::assign(u32 nodeIndex, std::span<const UICanvasCommand> commands)
{
    if (nodeIndex >= statesByNodeIndex_.size())
    {
        return Core::failure(Core::CoreErrorCode::Internal, "UI canvas state index is out of range");
    }
    if (activeCount_ > slots_.size() || commands.size() > (std::numeric_limits<u32>::max)() ||
        commands.size() > slots_.size() - activeCount_)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "UI canvas command capacity has been exhausted");
    }
    for (const UICanvasCommand& command : commands)
    {
        if (!isValidCanvasCommand(command))
        {
            return Core::failure(UIErrorCode::InvalidElementDescriptor,
                                 "UI canvas command kind, geometry, image source, sampling, and insets must be valid");
        }
    }

    NodeState nextState{};
    u32 previous = InvalidCommandIndex;
    for (const UICanvasCommand& command : commands)
    {
        const u32 commandIndex = freeHead_;
        if (commandIndex == InvalidCommandIndex || commandIndex >= slots_.size())
        {
            return Core::failure(Core::CoreErrorCode::Internal, "UI canvas free-list is inconsistent");
        }
        CommandSlot& slot = slots_[commandIndex];
        freeHead_ = slot.next;
        slot.command = command;
        slot.next = InvalidCommandIndex;
        if (previous == InvalidCommandIndex)
        {
            nextState.first = commandIndex;
        } else
        {
            slots_[previous].next = commandIndex;
        }
        previous = commandIndex;
        ++nextState.count;
    }
    statesByNodeIndex_[nodeIndex] = nextState;
    activeCount_ += commands.size();
    highWater_ = (std::max)(highWater_, activeCount_);
    return Core::success();
}

void UICanvasCommandStorage::release(u32 nodeIndex) noexcept
{
    if (nodeIndex >= statesByNodeIndex_.size())
    {
        return;
    }
    NodeState& state = statesByNodeIndex_[nodeIndex];
    u32 commandIndex = state.first;
    u32 releasedCount = 0;
    while (commandIndex != InvalidCommandIndex && releasedCount < state.count && commandIndex < slots_.size())
    {
        CommandSlot& slot = slots_[commandIndex];
        const u32 next = slot.next;
        slot = {};
        slot.next = freeHead_;
        freeHead_ = commandIndex;
        commandIndex = next;
        ++releasedCount;
    }
    activeCount_ = releasedCount <= activeCount_ ? activeCount_ - releasedCount : 0;
    state = {};
}

usize UICanvasCommandStorage::capacity() const noexcept
{
    return slots_.size();
}

usize UICanvasCommandStorage::activeCount() const noexcept
{
    return activeCount_;
}

usize UICanvasCommandStorage::highWater() const noexcept
{
    return highWater_;
}

} // namespace Tina::UI::Detail
