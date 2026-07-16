#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/render/RenderFrame.hpp>

#include <functional>
#include <memory>

namespace Tina::Render {

struct RenderDeviceCreateParams final {
};

struct RenderStatistics final {
    u64 submitted = 0;
    u64 presented = 0;
    u64 liveResources = 0;
};

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    [[nodiscard]] virtual Core::Status submitFrame(const RenderFrame& frame) = 0;
    [[nodiscard]] virtual Core::Status present() = 0;
    [[nodiscard]] virtual RenderStatistics statistics() const noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

using RenderDeviceFactory = std::move_only_function<
    Core::Result<std::unique_ptr<IRenderDevice>>(const RenderDeviceCreateParams&)>;

} // namespace Tina::Render
