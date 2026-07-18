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
        exceedsNodeCapacity(config.hitSnapshotCapacity) || exceedsNodeCapacity(config.paintSnapshotCapacity) ||
        exceedsNodeCapacity(config.routePathCapacity))
    {
        return invalidContextConfig("UI derived capacities cannot exceed node capacity");
    }
    if (config.routedPointerListenerCapacity > UIContextCapacityConfig::MaxRoutedPointerListenerCapacity)
    {
        return invalidContextConfig("UI routed pointer listener capacity exceeds the configured maximum");
    }

    return Core::success();
}

} // namespace Tina::UI
