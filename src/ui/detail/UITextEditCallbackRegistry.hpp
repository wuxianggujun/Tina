#pragma once

#include <tina/core/base/ScopeExit.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/UITextEdit.hpp>

#include <limits>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::UI::Detail {

inline constexpr u32 InvalidTextEditCallbackIndex = (std::numeric_limits<u32>::max)();

// The two public TextEdit callback types have identical lifetime semantics. A
// single fixed-capacity registry template keeps their generation/reclaim logic
// identical while preserving distinct event signatures at the public boundary.
template <typename Callback, typename Event>
class UITextEditCallbackRegistry final {
  public:
    struct Registration final {
        UINodeId node{};
        u32 callbackIndex = InvalidTextEditCallbackIndex;
        u32 previousCallbackIndex = InvalidTextEditCallbackIndex;
        u32 previousCallbackGeneration = 0;
        u32 generation = 0;
        bool replacing = false;

        [[nodiscard]] bool hasValue() const noexcept
        {
            return node.hasValue() && callbackIndex != InvalidTextEditCallbackIndex && generation != 0;
        }
    };

    struct Invocation final {
        UINodeId node{};
        u32 callbackIndex = InvalidTextEditCallbackIndex;
        u32 generation = 0;

        [[nodiscard]] bool hasValue() const noexcept
        {
            return node.hasValue() && callbackIndex != InvalidTextEditCallbackIndex && generation != 0;
        }
    };

    UITextEditCallbackRegistry(usize nodeCapacity, std::pmr::memory_resource& resource)
        : nodeCapacity_(nodeCapacity), callbackIndexByNodeIndex_(&resource),
          callbacks_(&resource), inactiveCallbackIndices_(&resource)
    {
        callbackIndexByNodeIndex_.resize(nodeCapacity, InvalidTextEditCallbackIndex);
        // One slot per live node plus one transaction slot. The extra slot
        // permits replacing an existing callback atomically.
        const usize storageCapacity = nodeCapacity + 1U;
        callbacks_.resize(storageCapacity);
        for (usize index = 0; index < storageCapacity; ++index)
        {
            callbacks_[index].nextFreeIndex =
                index + 1U < storageCapacity ? static_cast<u32>(index + 1U)
                                              : InvalidTextEditCallbackIndex;
        }
        freeCallbackHead_ = callbacks_.empty() ? InvalidTextEditCallbackIndex : 0U;
        inactiveCallbackIndices_.reserve(storageCapacity);
    }

    [[nodiscard]] Core::Result<Registration> stage(
        UINodeId node, Callback&& callback, bool deferReclaim)
    {
        if (!node.hasValue() || node.index() >= callbackIndexByNodeIndex_.size())
        {
            return Core::failure(UIErrorCode::InvalidNode,
                                 "UI TextEdit callback requires a valid TextEdit node");
        }
        if (!callback.hasValue())
        {
            return Core::failure(UIErrorCode::InvalidText,
                                 "UI TextEdit callback is empty");
        }

        const u32 previousCallbackIndex = callbackIndexByNodeIndex_[node.index()];
        const bool replacing = previousCallbackIndex < callbacks_.size() &&
                               callbacks_[previousCallbackIndex].active &&
                               callbacks_[previousCallbackIndex].node == node;
        const u32 previousCallbackGeneration =
            replacing ? callbacks_[previousCallbackIndex].generation : 0U;
        if (previousCallbackIndex != InvalidTextEditCallbackIndex && !replacing)
        {
            return Core::failure(Core::CoreErrorCode::Internal,
                                 "UI TextEdit callback mapping is inconsistent");
        }
        if (!replacing && activeCallbackCount_ >= nodeCapacity_)
        {
            return Core::failure(UIErrorCode::CapacityExceeded,
                                 "UI TextEdit callback capacity has been exhausted");
        }
        if (freeCallbackHead_ == InvalidTextEditCallbackIndex)
        {
            return Core::failure(UIErrorCode::CapacityExceeded,
                                 "UI TextEdit callback transaction storage has been exhausted");
        }

        const u32 callbackIndex = freeCallbackHead_;
        Record& record = callbacks_[callbackIndex];
        freeCallbackHead_ = record.nextFreeIndex;
        ++record.generation;
        if (record.generation == 0U)
        {
            ++record.generation;
        }
        record.node = node;
        record.nextFreeIndex = InvalidTextEditCallbackIndex;
        record.active = false;
        record.queuedForReclaim = false;
        record.invoking = false;
        {
            ++callbackOperationDepth_;
            auto operation = Core::makeScopeExit(
                [this]() noexcept { --callbackOperationDepth_; });
            record.callback = std::move(callback);
        }
        reclaim(deferReclaim);

        return Registration{
            .node = node,
            .callbackIndex = callbackIndex,
            .previousCallbackIndex = previousCallbackIndex,
            .previousCallbackGeneration = previousCallbackGeneration,
            .generation = record.generation,
            .replacing = replacing,
        };
    }

    [[nodiscard]] bool canCommit(const Registration& registration) const noexcept
    {
        if (!registration.hasValue() || registration.callbackIndex >= callbacks_.size() ||
            registration.node.index() >= callbackIndexByNodeIndex_.size() ||
            callbackIndexByNodeIndex_[registration.node.index()] !=
                registration.previousCallbackIndex)
        {
            return false;
        }
        const Record& pending = callbacks_[registration.callbackIndex];
        if (pending.active || pending.node != registration.node ||
            pending.generation != registration.generation || !pending.callback.hasValue())
        {
            return false;
        }
        if (!registration.replacing)
        {
            return registration.previousCallbackIndex == InvalidTextEditCallbackIndex &&
                   registration.previousCallbackGeneration == 0U;
        }
        if (registration.previousCallbackIndex >= callbacks_.size())
        {
            return false;
        }
        const Record& previous = callbacks_[registration.previousCallbackIndex];
        return previous.active && previous.node == registration.node &&
               previous.generation == registration.previousCallbackGeneration;
    }

    [[nodiscard]] Core::Status commit(const Registration& registration,
                                      bool deferReclaim) noexcept
    {
        if (!canCommit(registration))
        {
            return Core::failure(UIErrorCode::InvalidText,
                                 "UI TextEdit callback changed during callback transfer");
        }
        Record& pending = callbacks_[registration.callbackIndex];
        pending.active = true;
        callbackIndexByNodeIndex_[registration.node.index()] = registration.callbackIndex;
        ++activeCallbackCount_;
        if (registration.replacing)
        {
            deactivate(registration.previousCallbackIndex, deferReclaim);
        }
        return Core::success();
    }

    void rollback(const Registration& registration, bool deferReclaim) noexcept
    {
        if (!registration.hasValue() || registration.callbackIndex >= callbacks_.size())
        {
            return;
        }
        const Record& record = callbacks_[registration.callbackIndex];
        if (record.active || record.node != registration.node ||
            record.generation != registration.generation)
        {
            return;
        }
        deactivate(registration.callbackIndex, deferReclaim);
        reclaim(deferReclaim);
    }

    void clear(UINodeId node, bool deferReclaim) noexcept
    {
        if (!node.hasValue() || node.index() >= callbackIndexByNodeIndex_.size())
        {
            return;
        }
        const u32 callbackIndex = callbackIndexByNodeIndex_[node.index()];
        if (callbackIndex >= callbacks_.size() || callbacks_[callbackIndex].node != node)
        {
            return;
        }
        deactivate(callbackIndex, deferReclaim);
    }

    void clearNode(u32 nodeIndex, bool deferReclaim) noexcept
    {
        if (nodeIndex >= callbackIndexByNodeIndex_.size())
        {
            return;
        }
        const u32 callbackIndex = callbackIndexByNodeIndex_[nodeIndex];
        callbackIndexByNodeIndex_[nodeIndex] = InvalidTextEditCallbackIndex;
        if (callbackIndex != InvalidTextEditCallbackIndex)
        {
            deactivate(callbackIndex, deferReclaim);
        }
    }

    [[nodiscard]] Invocation capture(UINodeId node) const noexcept
    {
        if (!node.hasValue() || node.index() >= callbackIndexByNodeIndex_.size())
        {
            return {};
        }
        const u32 callbackIndex = callbackIndexByNodeIndex_[node.index()];
        if (callbackIndex >= callbacks_.size())
        {
            return {};
        }
        const Record& callback = callbacks_[callbackIndex];
        if (!callback.active || callback.node != node || !callback.callback.hasValue())
        {
            return {};
        }
        return Invocation{
            .node = node,
            .callbackIndex = callbackIndex,
            .generation = callback.generation,
        };
    }

    void invoke(Invocation invocation, const Event& event, bool deferReclaim) noexcept
    {
        if (!invocation.hasValue() || invocation.callbackIndex >= callbacks_.size())
        {
            return;
        }
        Record& callback = callbacks_[invocation.callbackIndex];
        if (!callback.active || callback.generation != invocation.generation ||
            callback.node != invocation.node || !callback.callback.hasValue())
        {
            return;
        }

        callback.invoking = true;
        ++callbackOperationDepth_;
        callback.callback(event);
        --callbackOperationDepth_;
        if (invocation.callbackIndex < callbacks_.size())
        {
            Record& current = callbacks_[invocation.callbackIndex];
            if (current.generation == invocation.generation)
            {
                current.invoking = false;
            }
        }
        reclaim(deferReclaim);
    }

    void reclaim(bool deferReclaim) noexcept
    {
        if (deferReclaim || callbackOperationDepth_ != 0U || reclaimingInactiveCallbacks_)
        {
            return;
        }
        reclaimingInactiveCallbacks_ = true;
        auto guard = Core::makeScopeExit(
            [this]() noexcept { reclaimingInactiveCallbacks_ = false; });
        while (!inactiveCallbackIndices_.empty())
        {
            const u32 callbackIndex = inactiveCallbackIndices_.back();
            inactiveCallbackIndices_.pop_back();
            if (callbackIndex >= callbacks_.size())
            {
                continue;
            }
            Record& callback = callbacks_[callbackIndex];
            callback.queuedForReclaim = false;
            if (!callback.active && !callback.invoking && callback.node.hasValue())
            {
                recycle(callbackIndex);
            }
        }
    }

    [[nodiscard]] usize activeCount() const noexcept { return activeCallbackCount_; }
    [[nodiscard]] usize capacity() const noexcept { return nodeCapacity_; }
    [[nodiscard]] bool operationInProgress() const noexcept
    {
        return callbackOperationDepth_ != 0U;
    }

  private:
    struct Record final {
        UINodeId node{};
        Callback callback{};
        u32 generation = 0;
        u32 nextFreeIndex = InvalidTextEditCallbackIndex;
        bool active = false;
        bool queuedForReclaim = false;
        bool invoking = false;
    };

    static_assert(std::is_nothrow_destructible_v<Record>);

    void deactivate(u32 callbackIndex, bool deferReclaim) noexcept
    {
        if (callbackIndex >= callbacks_.size())
        {
            return;
        }
        Record& callback = callbacks_[callbackIndex];
        if (!callback.node.hasValue())
        {
            return;
        }
        if (callback.node.index() < callbackIndexByNodeIndex_.size() &&
            callbackIndexByNodeIndex_[callback.node.index()] == callbackIndex)
        {
            callbackIndexByNodeIndex_[callback.node.index()] = InvalidTextEditCallbackIndex;
        }
        if (callback.active)
        {
            callback.active = false;
            if (activeCallbackCount_ > 0U)
            {
                --activeCallbackCount_;
            }
        }
        if (deferReclaim || callbackOperationDepth_ != 0U || callback.invoking ||
            reclaimingInactiveCallbacks_)
        {
            if (!callback.queuedForReclaim)
            {
                callback.queuedForReclaim = true;
                inactiveCallbackIndices_.push_back(callbackIndex);
            }
            return;
        }
        recycle(callbackIndex);
        reclaim(deferReclaim);
    }

    void recycle(u32 callbackIndex) noexcept
    {
        if (callbackIndex >= callbacks_.size())
        {
            return;
        }
        Record& callback = callbacks_[callbackIndex];
        if (callback.node.hasValue() && callback.node.index() < callbackIndexByNodeIndex_.size() &&
            callbackIndexByNodeIndex_[callback.node.index()] == callbackIndex)
        {
            callbackIndexByNodeIndex_[callback.node.index()] = InvalidTextEditCallbackIndex;
        }
        callback.node = {};
        callback.active = false;
        callback.queuedForReclaim = false;
        callback.invoking = false;
        callback.nextFreeIndex = InvalidTextEditCallbackIndex;

        ++callbackOperationDepth_;
        auto operation = Core::makeScopeExit(
            [this]() noexcept { --callbackOperationDepth_; });
        Callback detachedCallback(std::move(callback.callback));
        callback.nextFreeIndex = freeCallbackHead_;
        freeCallbackHead_ = callbackIndex;
        detachedCallback.reset();
    }

    usize nodeCapacity_ = 0;
    std::pmr::vector<u32> callbackIndexByNodeIndex_;
    std::pmr::vector<Record> callbacks_;
    std::pmr::vector<u32> inactiveCallbackIndices_;
    u32 freeCallbackHead_ = InvalidTextEditCallbackIndex;
    usize activeCallbackCount_ = 0;
    usize callbackOperationDepth_ = 0;
    bool reclaimingInactiveCallbacks_ = false;
};

using UITextChangedCallbackRegistry =
    UITextEditCallbackRegistry<UITextChangedCallback, UITextChangedEvent>;
using UITextSubmitCallbackRegistry =
    UITextEditCallbackRegistry<UITextSubmitCallback, UITextSubmitEvent>;

} // namespace Tina::UI::Detail
