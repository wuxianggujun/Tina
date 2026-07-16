#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <memory>

namespace Tina::Detail {
class EngineHostImplementation;
}

namespace Tina {

struct EngineFactories;
class IGameApplication;

class EngineHost final {
public:
    ~EngineHost() noexcept;

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;
    EngineHost(EngineHost&&) = delete;
    EngineHost& operator=(EngineHost&&) = delete;

    [[nodiscard]] static Core::Result<std::unique_ptr<EngineHost>> Create(
        const EngineConfig& config,
        EngineFactories factories) noexcept;

    [[nodiscard]] Core::Result<RunExitReason> run(IGameApplication& gameApplication) noexcept;

private:
    explicit EngineHost(std::unique_ptr<Detail::EngineHostImplementation> implementation) noexcept;

    std::unique_ptr<Detail::EngineHostImplementation> m_implementation;
};

} // namespace Tina
