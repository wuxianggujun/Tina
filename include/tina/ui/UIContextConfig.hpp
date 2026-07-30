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
    static constexpr usize MaxButtonActionCapacity = 1'048'576;
    static constexpr usize MaxCanvasCommandCapacity = 8'388'608;
    static constexpr usize MaxTextByteCapacity = 64U * 1024U * 1024U;
    static constexpr usize DefaultTextByteCapacity = 64U * 1024U;

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
    // Total retained backend-neutral canvas commands across all Elements. Zero
    // derives from nodeCapacity; commands are copied during createElement().
    usize canvasCommandCapacity = 0;
    // Zero derives from nodeCapacity. The listener capacity may be configured
    // independently because one node can own listeners for several events.
    usize routePathCapacity = 0;
    usize routedPointerListenerCapacity = 0;
    // Zero derives from nodeCapacity. One additional internal transaction slot
    // is reserved so an action can be replaced while this published capacity
    // is full without exposing a partial property update.
    usize buttonActionCapacity = 0;
    // Total retained UTF-8 bytes for intrinsic text and authored semantics text
    // across the context. Zero uses DefaultTextByteCapacity. Storage is
    // pre-reserved at Create.
    usize textByteCapacity = 0;
    // When true (product default), Element StyleRole recipes install productTheme
    // chrome. Local setBoxPaint / set*Paint / setTextStyle calls override only
    // their property; clearOverride restores the current role recipe. Unit tests
    // that assert empty paint or exact paint-entry capacity may set this false.
    bool applyDefaultProductChrome = true;
};

[[nodiscard]] Core::Status validateUIContextCapacityConfig(const UIContextCapacityConfig& config);

} // namespace Tina::UI
