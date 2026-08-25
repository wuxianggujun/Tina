#include <tina/ui/UIContextConfig.hpp>

#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <string_view>

namespace Tina::UI {
namespace {

[[nodiscard]] Core::Status invalidContextConfig(std::string_view message)
{
    return Core::failure(UIErrorCode::InvalidContextConfig, message);
}

} // namespace

Core::Status validateUIContextCapacityConfig(const UIContextCapacityConfig& config)
{
    if (config.nodeCapacity == 0 || config.rootCapacity == 0)
    {
        return invalidContextConfig("UI context capacities must be greater than zero");
    }
    if (config.nodeCapacity > UIContextCapacityConfig::MaxNodeCapacity ||
        config.rootCapacity > UIContextCapacityConfig::MaxRootCapacity)
    {
        return invalidContextConfig("UI context capacity exceeds the configured maximum");
    }
    if (config.rootCapacity > config.nodeCapacity)
    {
        return invalidContextConfig("UI root capacity cannot exceed node capacity");
    }

    const auto exceedsNodeCapacity = [&config](usize configuredCapacity) noexcept {
        return configuredCapacity != 0 && configuredCapacity > config.nodeCapacity;
    };
    if (exceedsNodeCapacity(config.dirtyQueueCapacity) || exceedsNodeCapacity(config.layoutSnapshotCapacity) ||
        exceedsNodeCapacity(config.hitSnapshotCapacity) || exceedsNodeCapacity(config.routePathCapacity) ||
        exceedsNodeCapacity(config.imageContentCapacity) ||
        exceedsNodeCapacity(config.layoutDebuggerSnapshotCapacity))
    {
        return invalidContextConfig("UI derived capacities cannot exceed node capacity");
    }
    if (config.paintSnapshotCapacity > UIContextCapacityConfig::MaxPaintSnapshotCapacity)
    {
        return invalidContextConfig("UI paint snapshot capacity exceeds the configured maximum");
    }
    if (config.routedPointerListenerCapacity > UIContextCapacityConfig::MaxRoutedPointerListenerCapacity)
    {
        return invalidContextConfig("UI routed pointer listener capacity exceeds the configured maximum");
    }
    if (config.buttonActionCapacity > UIContextCapacityConfig::MaxButtonActionCapacity
        || exceedsNodeCapacity(config.buttonActionCapacity))
    {
        return invalidContextConfig("UI Button action capacity exceeds the configured maximum");
    }
    if (config.canvasCommandCapacity > UIContextCapacityConfig::MaxCanvasCommandCapacity)
    {
        return invalidContextConfig("UI canvas command capacity exceeds the configured maximum");
    }
    if (config.imageContentCapacity > UIContextCapacityConfig::MaxImageContentCapacity)
    {
        return invalidContextConfig("UI image content capacity exceeds the configured maximum");
    }
    if (config.textByteCapacity > UIContextCapacityConfig::MaxTextByteCapacity)
    {
        return invalidContextConfig("UI text byte capacity exceeds the configured maximum");
    }
    if (config.textEditVisualLineCapacity > UIContextCapacityConfig::MaxTextEditVisualLineCapacity)
    {
        return invalidContextConfig("UI TextEdit visual-line capacity exceeds the configured maximum");
    }
    if (config.styleClassCapacity == 0 || config.styleTokenCapacity == 0 ||
        config.styleRuleCapacity == 0 ||
        config.styleBucketCapacity == 0 || config.styleRulesPerBucketCapacity == 0)
    {
        return invalidContextConfig("UI style capacities must be greater than zero");
    }
    if (config.styleClassCapacity > UIContextCapacityConfig::MaxStyleClassCapacity ||
        config.styleTokenCapacity > UIContextCapacityConfig::MaxStyleTokenCapacity ||
        config.styleRuleCapacity > UIContextCapacityConfig::MaxStyleRuleCapacity ||
        config.styleBucketCapacity > UIContextCapacityConfig::MaxStyleBucketCapacity)
    {
        return invalidContextConfig("UI style capacity exceeds the configured maximum");
    }
    if (config.styleBucketCapacity > config.styleRuleCapacity ||
        config.styleRulesPerBucketCapacity > config.styleRuleCapacity)
    {
        return invalidContextConfig("UI style bucket capacities cannot exceed rule capacity");
    }
    const usize maxNodeStyleClassLinks = config.nodeCapacity * 4U;
    if (config.nodeStyleClassLinkCapacity > maxNodeStyleClassLinks)
    {
        return invalidContextConfig("UI node style class link capacity exceeds four links per node");
    }
    if (config.motionTrackCapacity > UIContextCapacityConfig::MaxMotionTrackCapacity)
    {
        return invalidContextConfig("UI motion track capacity exceeds the configured maximum");
    }
    if (config.timelineCapacity > UIContextCapacityConfig::MaxTimelineCapacity ||
        config.timelineTrackCapacity > UIContextCapacityConfig::MaxTimelineTrackCapacity ||
        config.timelineKeyframeCapacity > UIContextCapacityConfig::MaxTimelineKeyframeCapacity ||
        config.activeTimelineCapacity > UIContextCapacityConfig::MaxActiveTimelineCapacity)
    {
        return invalidContextConfig("UI timeline capacity exceeds the configured maximum");
    }
    const usize effectiveTimelineCapacity =
        config.timelineCapacity == 0
            ? UIContextCapacityConfig::DefaultTimelineCapacity
            : config.timelineCapacity;
    const usize effectiveActiveTimelineCapacity =
        config.activeTimelineCapacity == 0
            ? (std::min)(UIContextCapacityConfig::DefaultActiveTimelineCapacity,
                         effectiveTimelineCapacity)
            : config.activeTimelineCapacity;
    if (effectiveActiveTimelineCapacity > effectiveTimelineCapacity)
    {
        return invalidContextConfig("UI active timeline capacity cannot exceed definition capacity");
    }
    if (config.flowLayerCapacity > UIContextCapacityConfig::MaxFlowLayerCapacity ||
        config.flowScreenCapacity > UIContextCapacityConfig::MaxFlowScreenCapacity ||
        exceedsNodeCapacity(config.flowLayerCapacity) || exceedsNodeCapacity(config.flowScreenCapacity))
    {
        return invalidContextConfig("UI Flow capacities cannot exceed node capacity");
    }

    const UIComponentStateCapacityConfig& components = config.componentStates;
    const usize componentCapacities[] = {
        components.tooltipCapacity,
        components.dialogCapacity,
        components.splitViewCapacity,
        components.splitterCapacity,
        components.tabViewCapacity,
        components.tabCapacity,
        components.menuCapacity,
        components.menuItemCapacity,
        components.virtualGridViewCapacity,
        components.virtualGridItemCapacity,
        components.dataGridCapacity,
        components.dataGridColumnCapacity,
        components.dataGridRowCapacity,
        components.dataGridCellCapacity,
    };
    if (std::any_of(std::begin(componentCapacities), std::end(componentCapacities),
                    exceedsNodeCapacity))
    {
        return invalidContextConfig(
            "UI component state capacities cannot exceed node capacity");
    }

    return Core::success();
}

} // namespace Tina::UI
