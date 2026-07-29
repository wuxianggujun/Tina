#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIEventRouting.hpp>

#include <limits>
#include <memory_resource>
#include <type_traits>
#include <vector>

namespace Tina::UI::Detail {

inline constexpr u32 InvalidRoutedPointerListenerIndex = (std::numeric_limits<u32>::max)();

struct UIRoutedPointerListenerRegistration final {
    UINodeId node{};
    u32 listenerIndex = InvalidRoutedPointerListenerIndex;
    u32 generation = 0;

    [[nodiscard]] bool hasValue() const noexcept;
};

struct UIRoutedPointerListenerStatePublisher final {
    using PublishOperation = void (*)(void* context, u32 slot, u32 generation, bool active) noexcept;

    void* context = nullptr;
    PublishOperation publish = nullptr;

    void operator()(u32 slot, u32 generation, bool active) const noexcept;
};

class UIRoutedPointerListenerRegistry final {
public:
    UIRoutedPointerListenerRegistry(usize nodeCapacity, usize listenerCapacity,
                                    std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Result<UIRoutedPointerListenerRegistration>
    stage(UIRoutedPointerListenerDesc descriptor, UIRoutedPointerCallback&& callback,
          bool deferReclaim);
    [[nodiscard]] bool canCommit(const UIRoutedPointerListenerRegistration& registration) const noexcept;
    [[nodiscard]] Core::Status
    commit(const UIRoutedPointerListenerRegistration& registration,
           UIRoutedPointerListenerStatePublisher statePublisher, bool deferReclaim) noexcept;
    void rollback(const UIRoutedPointerListenerRegistration& registration, bool deferReclaim) noexcept;

    [[nodiscard]] bool deactivate(u32 listenerIndex, u32 generation,
                                  UIRoutedPointerListenerStatePublisher statePublisher,
                                  bool deferReclaim) noexcept;
    void clearNode(u32 nodeIndex, UIRoutedPointerListenerStatePublisher statePublisher) noexcept;
    void resetNodeSlot(u32 nodeIndex) noexcept;

    [[nodiscard]] usize dispatch(UINodeId node, UIRoutedPointerEventKind kind,
                                 UIEventPhaseMask requiredPhase, u64 registrationSerialBoundary,
                                 UIRoutedPointerEvent& event) noexcept;
    void reclaim(bool deferReclaim) noexcept;

    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize highWater() const noexcept;
    [[nodiscard]] u64 registrationSerial() const noexcept;
    [[nodiscard]] bool operationInProgress() const noexcept;

private:
    struct Record final {
        UINodeId node{};
        UIRoutedPointerEventKind kind = UIRoutedPointerEventKind::Move;
        UIEventPhaseMask phases = UIEventPhaseMask::None;
        UIRoutedPointerCallback callback{};
        u32 generation = 0;
        u32 previousNodeListenerIndex = InvalidRoutedPointerListenerIndex;
        u32 nextNodeListenerIndex = InvalidRoutedPointerListenerIndex;
        u32 nextFreeIndex = InvalidRoutedPointerListenerIndex;
        u64 registrationSerial = 0;
        bool active = false;
        bool queuedForReclaim = false;
    };

    static_assert(std::is_nothrow_destructible_v<Record>);

    void unlink(u32 listenerIndex) noexcept;
    void recycle(u32 listenerIndex) noexcept;

    usize listenerCapacity_ = 0;
    std::pmr::vector<u32> headByNodeIndex_;
    std::pmr::vector<u32> tailByNodeIndex_;
    std::pmr::vector<Record> listeners_;
    std::pmr::vector<u32> inactiveListenerIndices_;
    u32 freeListenerHead_ = InvalidRoutedPointerListenerIndex;
    usize activeListenerCount_ = 0;
    usize highWater_ = 0;
    u64 registrationSerial_ = 0;
    usize dispatchDepth_ = 0;
    usize callbackOperationDepth_ = 0;
    bool reclaimingInactiveListeners_ = false;
};

} // namespace Tina::UI::Detail
