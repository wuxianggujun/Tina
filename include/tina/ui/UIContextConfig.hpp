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
    static constexpr usize MaxPaintSnapshotCapacity = 8'388'608;
    static constexpr usize MaxImageContentCapacity = MaxNodeCapacity;
    static constexpr usize MaxTextByteCapacity = 64U * 1024U * 1024U;
    static constexpr usize DefaultTextByteCapacity = 64U * 1024U;
    static constexpr usize DefaultTextEditVisualLineCapacity = 4096;
    static constexpr usize MaxTextEditVisualLineCapacity = 1'048'576;
    static constexpr usize DefaultStyleClassCapacity = 256;
    static constexpr usize DefaultStyleTokenCapacity = 256;
    static constexpr usize DefaultStyleRuleCapacity = 256;
    static constexpr usize DefaultStyleBucketCapacity = 256;
    static constexpr usize DefaultStyleRulesPerBucketCapacity = 256;
    static constexpr usize DefaultMotionTrackCapacity = 64;
    static constexpr usize DefaultTimelineCapacity = 16;
    static constexpr usize DefaultTimelineTrackCapacity = 64;
    static constexpr usize DefaultTimelineKeyframeCapacity = 256;
    static constexpr usize DefaultActiveTimelineCapacity = 16;
    static constexpr usize DefaultFlowLayerCapacity = 16;
    static constexpr usize DefaultFlowScreenCapacity = 64;
    static constexpr usize MaxStyleClassCapacity = MaxNodeCapacity;
    static constexpr usize MaxStyleTokenCapacity = MaxNodeCapacity;
    static constexpr usize MaxStyleRuleCapacity = MaxNodeCapacity;
    static constexpr usize MaxStyleBucketCapacity = MaxNodeCapacity;
    static constexpr usize MaxNodeStyleClassLinkCapacity = MaxNodeCapacity * 4U;
    static constexpr usize MaxMotionTrackCapacity = MaxNodeCapacity;
    static constexpr usize MaxTimelineCapacity = MaxNodeCapacity;
    static constexpr usize MaxTimelineTrackCapacity = MaxNodeCapacity * 8U;
    static constexpr usize MaxTimelineKeyframeCapacity = MaxNodeCapacity * 32U;
    static constexpr usize MaxActiveTimelineCapacity = MaxTimelineCapacity;
    static constexpr usize MaxFlowLayerCapacity = MaxNodeCapacity;
    static constexpr usize MaxFlowScreenCapacity = MaxNodeCapacity;

    usize nodeCapacity = DefaultNodeCapacity;
    usize rootCapacity = DefaultRootCapacity;
    // Zero derives from nodeCapacity. Non-zero values are fixed capacities and
    // do not grow at runtime.
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    // Counts every effectively visible route-ancestry entry, including nodes
    // whose pointer policy is Ignore; it is not a targetable-node capacity.
    usize hitSnapshotCapacity = 0;
    // Counts emitted paint entries, not layout nodes. Zero derives from
    // nodeCapacity; a non-zero value is independently bounded because one node
    // can emit multiple glyph, control, Canvas, or NineSlice entries.
    usize paintSnapshotCapacity = 0;
    // Total retained backend-neutral canvas commands across all Elements. Zero
    // derives from nodeCapacity; commands are copied during createElement().
    usize canvasCommandCapacity = 0;
    // Total Elements with retained image content. Zero derives from
    // nodeCapacity and remains fixed for the context lifetime.
    usize imageContentCapacity = 0;
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
    // Fixed visual-line records shared by multiline TextEdits. Zero uses the
    // bounded default and never grows after context creation.
    usize textEditVisualLineCapacity = 0;
    // Stylesheets own fixed token values and compile into fixed rule and
    // (role,class) bucket pools. Node links default to four class slots per node.
    usize styleClassCapacity = DefaultStyleClassCapacity;
    usize styleTokenCapacity = DefaultStyleTokenCapacity;
    usize styleRuleCapacity = DefaultStyleRuleCapacity;
    usize styleBucketCapacity = DefaultStyleBucketCapacity;
    usize styleRulesPerBucketCapacity = DefaultStyleRulesPerBucketCapacity;
    usize nodeStyleClassLinkCapacity = 0;
    // Fixed active transition tracks (UI-MOTION-001). Zero uses DefaultMotionTrackCapacity.
    usize motionTrackCapacity = 0;
    // UI-MOTION-002 retained definitions, definition tracks, encoded
    // keyframes, and compact active-playback index. Zero uses bounded defaults;
    // all four pools are allocated once during Create and never grow later.
    usize timelineCapacity = 0;
    usize timelineTrackCapacity = 0;
    usize timelineKeyframeCapacity = 0;
    usize activeTimelineCapacity = 0;
    // UI-FLOW-001 identities reuse retained nodes. Zero derives a fixed context
    // capacity from min(DefaultFlow*, nodeCapacity); storage never grows later.
    usize flowLayerCapacity = 0;
    usize flowScreenCapacity = 0;
    // When true (product default), Element StyleRole recipes install productTheme
    // chrome. Local setBoxPaint / set*Paint / setTextStyle calls override only
    // their property; clearOverride restores the current role recipe. Unit tests
    // that assert empty paint or exact paint-entry capacity may set this false.
    bool applyDefaultProductChrome = true;
};

[[nodiscard]] Core::Status validateUIContextCapacityConfig(const UIContextCapacityConfig& config);

} // namespace Tina::UI
