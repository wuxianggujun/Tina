#pragma once

#include <tina/platform/PlatformBackend.hpp>

namespace Tina::Platform {

[[nodiscard]] Core::Result<std::unique_ptr<IPlatformBackend>> createHeadlessPlatformBackend(
    const PlatformBackendCreateParams& params);

} // namespace Tina::Platform
