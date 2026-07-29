#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UINodeId.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace Tina::UI {

class UIContext;

namespace Detail {

struct DeferredRoutedPointerListenerRelease final {
    u32 slot = 0;
    u32 generation = 0;
};

class UIContextLifetimeControl final {
public:
    UIContextLifetimeControl(std::thread::id ownerThreadId, usize rootCapacity,
                             usize routedPointerListenerCapacity);

    void attach(UIContext& context) noexcept;
    void detach(UIContext& context) noexcept;
    [[nodiscard]] UIContext* attachedContext() const noexcept;

    void publishRoutedPointerListenerState(u32 slot, u32 generation,
                                           bool active) noexcept;
    [[nodiscard]] UIContext*
    releaseRoutedPointerListener(u32 slot, u32 generation) noexcept;
    [[nodiscard]] bool
    isRoutedPointerListenerActive(u32 slot, u32 generation) const noexcept;

    [[nodiscard]] UIContext* releaseRoot(UINodeId root) noexcept;
    void takeDeferredRootDestroys(std::vector<UINodeId>& output) noexcept;
    void takeDeferredRoutedPointerListenerReleases(
        std::vector<DeferredRoutedPointerListenerRelease>& output) noexcept;

private:
    struct RoutedPointerListenerTokenState final {
        u32 generation = 0;
        bool active = false;
    };

    mutable std::mutex mutex_;
    UIContext* context_ = nullptr;
    std::thread::id ownerThreadId_{};
    std::vector<UINodeId> deferredRootDestroys_;
    std::atomic_bool hasDeferredRootDestroys_ = false;
    std::vector<RoutedPointerListenerTokenState> routedPointerListenerStates_;
    std::vector<DeferredRoutedPointerListenerRelease>
        deferredRoutedPointerListenerReleases_;
    std::atomic_bool hasDeferredRoutedPointerListenerReleases_ = false;
};

} // namespace Detail
} // namespace Tina::UI
