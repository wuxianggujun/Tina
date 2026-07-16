#pragma once

#include <tina/core/error/Result.hpp>

#include <functional>
#include <memory>
#include <string_view>

namespace Tina::Platform {

struct PlatformBackendCreateParams final {
    std::string_view applicationName;
};

struct PlatformPollResult final {
    bool exitRequested = false;
    bool surfaceSuspended = false;
};

class IPlatformBackend {
public:
    virtual ~IPlatformBackend() = default;

    [[nodiscard]] virtual Core::Result<PlatformPollResult> pollEvents() = 0;
    virtual void shutdown() noexcept = 0;
};

using PlatformBackendFactory = std::move_only_function<
    Core::Result<std::unique_ptr<IPlatformBackend>>(const PlatformBackendCreateParams&)>;

} // namespace Tina::Platform
