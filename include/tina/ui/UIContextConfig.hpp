#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

namespace Tina::UI {

struct UIContextCapacityConfig final {
    static constexpr usize DefaultNodeCapacity = 4096;
    static constexpr usize DefaultRootCapacity = 64;
    static constexpr usize MaxNodeCapacity = 1'048'576;
    static constexpr usize MaxRootCapacity = 4096;
    static constexpr usize MaxRoutedPointerListenerCapacity = 1'048'576;

    usize nodeCapacity = DefaultNodeCapacity;
    usize rootCapacity = DefaultRootCapacity;
    // Zero derives from nodeCapacity. Non-zero values are fixed capacities and
    // do not grow at runtime.
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    // Counts every effectively visible route-ancestry entry, including nodes
    // whose pointer policy is Ignore; it is not a targetable-node capacity.
    usize hitSnapshotCapacity = 0;
    // Counts emitted paint entries, not all layout nodes. Zero derives from
    // nodeCapacity; a non-zero value remains fixed for the context lifetime.
    usize paintSnapshotCapacity = 0;
    // Zero derives from nodeCapacity. The listener capacity may be configured
    // independently because one node can own listeners for several events.
    usize routePathCapacity = 0;
    usize routedPointerListenerCapacity = 0;
};

[[nodiscard]] Core::Status validateUIContextCapacityConfig(const UIContextCapacityConfig& config);

} // namespace Tina::UI
