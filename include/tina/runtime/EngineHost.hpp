#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/RunExitReason.hpp>

#include <memory>
#include <optional>

namespace Tina::Detail {
class EngineHostImplementation;
}

namespace Tina {

struct EngineCompositionFactories;
class IGameApplication;

class EngineHost final {
  public:
    // Create, run, and destruction are one owner-thread lifetime. A native
    // platform backend may require that owner to be the process main/platform
    // thread. run rejects another thread and destruction on another thread
    // terminates before invoking backend APIs.
    ~EngineHost() noexcept;

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;
    EngineHost(EngineHost&&) = delete;
    EngineHost& operator=(EngineHost&&) = delete;

    [[nodiscard]] static Core::Result<std::unique_ptr<EngineHost>>
    Create(const EngineConfig& config, EngineCompositionFactories factories) noexcept;

    [[nodiscard]] Core::Result<RunExitReason> run(IGameApplication& gameApplication) noexcept;

    // Externally driven alternative to run(), for hosts that do not let the caller own
    // the frame loop: iOS delivers frames from a CADisplayLink callback, and the same
    // shape fits an embedded viewport or any other external main loop.
    //
    // start() runs the startup transaction and commits the first game state; tick()
    // then advances exactly one frame. A successful tick returns nullopt while the run
    // continues, and a RunExitReason once it has ended — at which point teardown has
    // already happened and neither call may be made again. A failure is final in the
    // same way; both mirror run()'s result exactly, because both go through the same
    // frame body.
    //
    // start() and run() are mutually exclusive, and every call must be on the thread
    // that created the host.
    [[nodiscard]] Core::Status start(IGameApplication& gameApplication) noexcept;
    [[nodiscard]] Core::Result<std::optional<RunExitReason>> tick(IGameApplication& gameApplication) noexcept;

  private:
    explicit EngineHost(std::unique_ptr<Detail::EngineHostImplementation> implementation) noexcept;

    std::unique_ptr<Detail::EngineHostImplementation> m_implementation;
};

} // namespace Tina
