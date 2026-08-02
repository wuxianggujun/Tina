#include <tina/ui/UIContextConfig.hpp>

#include <tina/ui/UIErrors.hpp>

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
        exceedsNodeCapacity(config.imageContentCapacity))
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
    if (config.styleClassCapacity == 0 || config.styleRuleCapacity == 0 ||
        config.styleBucketCapacity == 0 || config.styleRulesPerBucketCapacity == 0)
    {
        return invalidContextConfig("UI style capacities must be greater than zero");
    }
    if (config.styleClassCapacity > UIContextCapacityConfig::MaxStyleClassCapacity ||
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

    return Core::success();
}

} // namespace Tina::UI
