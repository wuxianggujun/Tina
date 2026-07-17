#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/platform/PlatformFrame.hpp>
#include <tina/platform/Window.hpp>

#include <functional>
#include <memory>

namespace Tina::Platform {

struct PlatformBackendCreateParams final {
    PrimaryWindowConfig primaryWindow{};
    PlatformFrameCapacityConfig frameCapacities{};
};

class IPlatformBackend {
  public:
    virtual ~IPlatformBackend() = default;

    [[nodiscard]] virtual Core::Result<PlatformPollResult> pollFrame() = 0;
    virtual void shutdown() noexcept = 0;
};

using PlatformBackendFactory =
    std::move_only_function<Core::Result<std::unique_ptr<IPlatformBackend>>(const PlatformBackendCreateParams&)>;

} // namespace Tina::Platform
