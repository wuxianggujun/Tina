#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UIFlow.hpp>
#include <tina/ui/UIMotion.hpp>

namespace Tina::UI {

struct UIStyleStatistics final {
    usize classCapacity = 0;
    usize registeredClassCount = 0;
    usize classHighWater = 0;
    usize tokenCapacity = 0;
    usize registeredTokenCount = 0;
    usize tokenHighWater = 0;
    usize ruleCapacity = 0;
    usize activeRuleCount = 0;
    usize ruleHighWater = 0;
    usize bucketCapacity = 0;
    usize activeBucketCount = 0;
    usize bucketHighWater = 0;
    usize rulesPerBucketCapacity = 0;
    usize bucketCandidateHighWater = 0;
    usize nodeClassLinkCapacity = 0;
    usize activeNodeClassLinkCount = 0;
    usize nodeClassLinkHighWater = 0;
    usize compileFailureCount = 0;
    usize capacityFailureCount = 0;
    u64 revision = 0;
};

struct UIContextStatistics final {
    // Bytes requested from the Context PMR resource. This is Tina-routed UI
    // storage only; it intentionally does not claim process RSS/private bytes
    // or GPU allocations.
    usize pmrCurrentBytes = 0;
    usize pmrPeakBytes = 0;
    u64 pmrAllocationCount = 0;
    u64 pmrDeallocationCount = 0;
    u64 pmrFailedAllocationCount = 0;
    u64 pmrInvalidDeallocationCount = 0;
    usize pmrNodePoolBytes = 0;
    usize pmrStateStorageBytes = 0;
    usize pmrScratchReserveBytes = 0;
    usize pmrIndexAlignedStorageBytes = 0;
    usize pmrSnapshotBufferBytes = 0;
    usize pmrGlyphAtlasBytes = 0;
    usize nodeCapacity = 0;
    usize rootCapacity = 0;
    usize dirtyQueueCapacity = 0;
    usize layoutSnapshotCapacity = 0;
    usize hitSnapshotCapacity = 0;
    usize paintSnapshotCapacity = 0;
    usize canvasCommandCapacity = 0;
    usize activeCanvasCommandCount = 0;
    usize canvasCommandHighWater = 0;
    usize imageContentCapacity = 0;
    usize activeImageContentCount = 0;
    usize imageContentHighWater = 0;
    usize routePathCapacity = 0;
    usize routedPointerListenerCapacity = 0;
    usize activeRoutedPointerListenerCount = 0;
    usize routedPointerListenerHighWater = 0;
    usize buttonActionCapacity = 0;
    usize activeButtonActionCount = 0;
    usize buttonActionHighWater = 0;
    usize activateBehaviorCapacity = 0;
    usize activeActivateBehaviorCount = 0;
    usize activateBehaviorHighWater = 0;
    usize toggleBehaviorCapacity = 0;
    usize activeToggleBehaviorCount = 0;
    usize toggleBehaviorHighWater = 0;
    usize rangeInputBehaviorCapacity = 0;
    usize activeRangeInputBehaviorCount = 0;
    usize rangeInputBehaviorHighWater = 0;
    usize textInputBehaviorCapacity = 0;
    usize activeTextInputBehaviorCount = 0;
    usize textInputBehaviorHighWater = 0;
    usize scrollBehaviorCapacity = 0;
    usize activeScrollBehaviorCount = 0;
    usize scrollBehaviorHighWater = 0;
    usize selectBehaviorCapacity = 0;
    usize activeSelectBehaviorCount = 0;
    usize selectBehaviorHighWater = 0;
    usize textByteCapacity = 0;
    usize textByteUsed = 0;
    usize textByteHighWater = 0;
    usize liveNodeCount = 0;
    usize liveRootCount = 0;
    usize committedNodeCount = 0;
    u64 committedRevision = 0;
    usize committedLayoutNodeCount = 0;
    u64 layoutRevision = 0;
    usize committedHitNodeCount = 0;
    usize committedHitTargetCount = 0;
    u64 hitRevision = 0;
    u64 paintOrderRevision = 0;
    usize committedPaintNodeCount = 0;
    u64 paintRevision = 0;
    usize committedSemanticsNodeCount = 0;
    u64 semanticsRevision = 0;
    // Phase dirty mirrors internal UIDirty phase mask (Structure / layout /
    // HitTest / Paint / Semantics). True means that snapshot still needs a
    // successful commit before it matches live tree state.
    bool structureDirty = false;
    bool layoutDirty = false;
    bool hitDirty = false;
    bool paintDirty = false;
    bool semanticsDirty = false;
    usize lastLayoutPassCount = 0;
    usize lastLayoutMeasuredNodeCount = 0;
    usize lastLayoutArrangedNodeCount = 0;
    // Percent values skipped while an Auto axis lacked a definite Measure
    // basis. Arrange may still resolve them once against the final content box.
    usize lastLayoutPercentMeasureFallbackCount = 0;
    usize lastHitRebuildCount = 0;
    usize lastPaintCacheRebuildCount = 0;
    usize lastPaintSnapshotRebuildCount = 0;
    usize lastStyleInspectedNodeCount = 0;
    usize lastStyleResolvedNodeCount = 0;
    usize lastStyleCandidateRuleCount = 0;
    usize lastStyleTokenUpdateInspectedNodeCount = 0;
    usize lastStyleTokenUpdateResolvedNodeCount = 0;
    usize lastStyleTokenUpdateAffectedNodeCount = 0;
    usize lastStyleTokenUpdateCandidateRuleCount = 0;
    usize dirtyQueuePendingCount = 0;
    usize dirtyQueueHighWater = 0;
    UIStyleStatistics style{};
    UIComponentBuildStatistics componentBuild{};
    UIMotionStatistics motion{};
    UIFlowStatistics flow{};
};


} // namespace Tina::UI
