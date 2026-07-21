#pragma once

#include <tina/audio/AudioEngine.hpp>
#include <tina/audio/AudioErrors.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory_resource>

namespace Tina::Audio {

// Private adapter config. miniaudio types never appear here.
struct MiniaudioDeviceConfig final {
    // true: ma_backend_null (hermetic CI / Null graph). false: OS default backends.
    bool useNullBackend = true;
    Core::u32 sampleRate = 48000;
    Core::u32 channels = 2;
    Core::u32 periodFrames = 480;
};

// M11-A9: private miniaudio playback device wrapper. Owns context+device;
// start/stop/shutdown are owner-thread. dataCallback is allocation-free
// (atomic counter + silence until real mixer lands).
class MiniaudioDevice final {
  public:
    [[nodiscard]] static Core::Result<MiniaudioDevice> Create(MiniaudioDeviceConfig config = {});

    ~MiniaudioDevice() noexcept;

    MiniaudioDevice(const MiniaudioDevice&) = delete;
    MiniaudioDevice& operator=(const MiniaudioDevice&) = delete;
    MiniaudioDevice(MiniaudioDevice&& other) noexcept;
    MiniaudioDevice& operator=(MiniaudioDevice&&) = delete;

    [[nodiscard]] Core::Status start() noexcept;
    void stop() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isNullBackend() const noexcept;
    [[nodiscard]] Core::u32 sampleRate() const noexcept;
    [[nodiscard]] Core::u32 channels() const noexcept;
    // Number of dataCallback invocations (test/diagnostics).
    [[nodiscard]] Core::u64 callbackInvocations() const noexcept;

  private:
    struct Impl;
    explicit MiniaudioDevice(Impl* impl) noexcept;
    Impl* m_impl = nullptr;
};

// Convenience: AudioEngine (commands/bus) + miniaudio device.
// Device failure returns structured error without leaking a half-started engine.
struct MiniaudioAudioBundle final {
    AudioEngine engine;
    MiniaudioDevice device;
};

[[nodiscard]] Core::Result<MiniaudioAudioBundle> createMiniaudioAudioBundle(
    AudioEngineConfig engineConfig = {},
    MiniaudioDeviceConfig deviceConfig = {},
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource());

} // namespace Tina::Audio
