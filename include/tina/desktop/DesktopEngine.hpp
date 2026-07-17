#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>

#include <memory>

namespace Tina::Desktop {

[[nodiscard]] Core::Result<std::unique_ptr<EngineHost>> CreateEngine(const EngineConfig& config) noexcept;

} // namespace Tina::Desktop
