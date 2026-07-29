#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIButton.hpp>

#include <limits>
#include <memory_resource>
#include <type_traits>
#include <vector>

namespace Tina::UI::Detail {

inline constexpr u32 InvalidButtonActionIndex = (std::numeric_limits<u32>::max)();

struct UIButtonActionRegistration final {
    UINodeId button{};
    u32 actionIndex = InvalidButtonActionIndex;
    u32 previousActionIndex = InvalidButtonActionIndex;
    u32 previousActionGeneration = 0;
    u32 generation = 0;
    bool replacing = false;

    [[nodiscard]] bool hasValue() const noexcept;
};

struct UIButtonActionInvocation final {
    UINodeId button{};
    u32 actionIndex = InvalidButtonActionIndex;
    u32 generation = 0;

    [[nodiscard]] bool hasValue() const noexcept;
};

class UIButtonActionRegistry final {
public:
    UIButtonActionRegistry(usize nodeCapacity, usize actionCapacity,
                           std::pmr::memory_resource& resource);

    [[nodiscard]] Core::Result<UIButtonActionRegistration>
    stage(UINodeId button, UIButtonActionCallback&& callback, bool deferReclaim);
    [[nodiscard]] bool canCommit(const UIButtonActionRegistration& registration) const noexcept;
    [[nodiscard]] Core::Status commit(const UIButtonActionRegistration& registration,
                                      bool deferReclaim) noexcept;
    void rollback(const UIButtonActionRegistration& registration, bool deferReclaim) noexcept;

    void clear(UINodeId button, u64 routeSerial, bool deferReclaim) noexcept;
    void clearNode(u32 nodeIndex, bool deferReclaim) noexcept;

    [[nodiscard]] UIButtonActionInvocation capture(UINodeId button,
                                                   u64 registrationSerialBoundary) const noexcept;
    void invoke(UIButtonActionInvocation invocation, const UIButtonActionEvent& event,
                u64 routeSerial, bool deferReclaim) noexcept;
    void reclaim(bool deferReclaim) noexcept;

    [[nodiscard]] usize activeCount() const noexcept;
    [[nodiscard]] usize capacity() const noexcept;
    [[nodiscard]] usize highWater() const noexcept;
    [[nodiscard]] u64 registrationSerial() const noexcept;
    [[nodiscard]] bool operationInProgress() const noexcept;

private:
    struct Record final {
        UINodeId node{};
        UIButtonActionCallback callback{};
        u32 generation = 0;
        u32 nextFreeIndex = InvalidButtonActionIndex;
        u64 registrationSerial = 0;
        bool active = false;
        bool queuedForReclaim = false;
        bool invoking = false;
    };

    static_assert(std::is_nothrow_destructible_v<Record>);

    void deactivate(u32 actionIndex, bool deferReclaim) noexcept;
    void recycle(u32 actionIndex) noexcept;

    usize actionCapacity_ = 0;
    std::pmr::vector<u32> actionIndexByNodeIndex_;
    std::pmr::vector<u64> clearRouteSerialByNodeIndex_;
    std::pmr::vector<Record> actions_;
    std::pmr::vector<u32> inactiveActionIndices_;
    u32 freeActionHead_ = InvalidButtonActionIndex;
    usize activeActionCount_ = 0;
    usize highWater_ = 0;
    u64 registrationSerial_ = 0;
    usize callbackOperationDepth_ = 0;
    bool reclaimingInactiveActions_ = false;
};

} // namespace Tina::UI::Detail
