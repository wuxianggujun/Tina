#pragma once

#include <string_view>

namespace Tina::Core {

// Identifies the linked archive, not the headers seen by the consumer. Views have
// process lifetime; no allocation, filesystem lookup or global mutable state.
struct BuildInfo final {
    std::string_view version;
    std::string_view buildId;
    std::string_view configuration;
    std::string_view compiler;
    std::string_view compilerVersion;
    std::string_view platform;
    std::string_view features;
};

[[nodiscard]] const BuildInfo& buildInfo() noexcept;

} // namespace Tina::Core
