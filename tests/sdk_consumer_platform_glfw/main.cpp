#include <tina/platform/PlatformBackend.hpp>
#include <tina/platform/glfw/GlfwPlatformFactory.hpp>

#include <iostream>

int main()
{
    Tina::Platform::PlatformBackendCreateParams params;
    params.primaryWindow.title = "Tina installed PlatformGlfw consumer";
    params.primaryWindow.initialLogicalExtent = {320, 180};
    params.primaryWindow.resizable = false;
    params.primaryWindow.initiallyVisible = false;

    auto backend = Tina::Platform::createGlfwPlatformBackend(params);
    if (!backend)
    {
        return 1;
    }

    auto startupMetrics = (*backend)->initialPrimaryWindowMetrics();
    if (!startupMetrics || !startupMetrics->has_value())
    {
        (*backend)->shutdown();
        return 1;
    }

    auto poll = (*backend)->pollFrame();
    if (!poll || !poll->isContinueFrame() || poll->frame() == nullptr || poll->frame()->primaryWindow() == nullptr)
    {
        (*backend)->shutdown();
        return 1;
    }

    const Tina::Platform::WindowMetricsSnapshot& metrics = **startupMetrics;
    const Tina::Platform::WindowFrameSnapshot& window = *poll->frame()->primaryWindow();
    if (!metrics.window.hasValue() || window.metrics.window != metrics.window || metrics.logicalExtent.width == 0 ||
        metrics.logicalExtent.height == 0)
    {
        (*backend)->shutdown();
        return 1;
    }

    const auto logicalWidth = metrics.logicalExtent.width;
    const auto logicalHeight = metrics.logicalExtent.height;
    const auto frameId = poll->frame()->id().value;
    (*backend)->shutdown();
    std::cout << "{\"status\":\"ok\",\"consumer\":\"installed-tina-platform-glfw\","
                 "\"logicalWidth\":"
              << logicalWidth << ",\"logicalHeight\":" << logicalHeight << ",\"frameId\":" << frameId << "}\n";
    return 0;
}
