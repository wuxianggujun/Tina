#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UISlider.hpp>

#include <limits>
#include <memory_resource>
#include <type_traits>
#include <vector>

namespace Tina::UI::Detail {

inline constexpr u32 InvalidSliderChangeCallbackIndex = (std::numeric_limits<u32>::max)();

struct UISliderChangeCallbackRegistration final {
    UINodeId slider{};
    u32 callbackIndex = InvalidSliderChangeCallbackIndex;
    u32 previousCallbackIndex = InvalidSliderChangeCallbackIndex;
    u32 previousCallbackGeneration = 0;
    u32 generation = 0;
    bool replacing = false;

    [[nodiscard]] bool hasValue() const noexcept;
};

struct UISliderChangeCallbackInvocation final {
    UINodeId slider{};
    u32 callbackIndex = InvalidSliderChangeCallbackIndex;
    u32 generation = 0;

    [[nodiscard]] bool hasValue() const noexcept;
};

class UISliderChangeCallbackRegistry final {
public:
    UISliderChangeCallbackRegistry(usize nodeCapacity, std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Result<UISliderChangeCallbackRegistration>
    stage(UINodeId slider, UISliderChangeCallback&& callback, bool deferReclaim);
    [[nodiscard]] bool canCommit(const UISliderChangeCallbackRegistration& registration) const noexcept;
    void commit(const UISliderChangeCallbackRegistration& registration, bool deferReclaim) noexcept;
    void rollback(const UISliderChangeCallbackRegistration& registration, bool deferReclaim) noexcept;

    void clear(UINodeId slider, bool deferReclaim) noexcept;
    void clearNode(u32 nodeIndex, bool deferReclaim) noexcept;

    [[nodiscard]] UISliderChangeCallbackInvocation capture(UINodeId slider) const noexcept;
    void invoke(UISliderChangeCallbackInvocation invocation, const UISliderChangeEvent& event,
                bool deferReclaim) noexcept;
    void reclaim(bool deferReclaim) noexcept;

    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] bool operationInProgress() const noexcept;

private:
    struct Record final {
        UINodeId node{};
        UISliderChangeCallback callback{};
        u32 generation = 0;
        u32 nextFreeIndex = InvalidSliderChangeCallbackIndex;
        bool active = false;
        bool queuedForReclaim = false;
        bool invoking = false;
    };

    static_assert(std::is_nothrow_destructible_v<Record>);

    void deactivate(u32 callbackIndex, bool deferReclaim) noexcept;
    void recycle(u32 callbackIndex) noexcept;

    usize nodeCapacity_ = 0;
    std::pmr::vector<u32> callbackIndexByNodeIndex_;
    std::pmr::vector<Record> callbacks_;
    std::pmr::vector<u32> inactiveCallbackIndices_;
    u32 freeCallbackHead_ = InvalidSliderChangeCallbackIndex;
    usize activeCallbackCount_ = 0;
    usize callbackOperationDepth_ = 0;
    bool reclaimingInactiveCallbacks_ = false;
};

} // namespace Tina::UI::Detail
