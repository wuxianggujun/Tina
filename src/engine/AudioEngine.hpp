//
// AudioEngine - 基于 miniaudio 的跨平台音频设备与混音总线
//

#pragma once

#include "../core/Memory.hpp"
#include <cstdint>

namespace Tina::Engine {

enum class AudioBus : std::uint8_t {
    Music,
    Sfx,
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;

    bool initialize();
    void shutdown();
    bool isInitialized() const noexcept;

    void setMasterVolume(float volume);
    float getMasterVolume() const noexcept;

    void setMusicVolume(float volume);
    float getMusicVolume() const noexcept;

    void setSfxVolume(float volume);
    float getSfxVolume() const noexcept;

    void setBusVolume(AudioBus bus, float volume);
    float getBusVolume(AudioBus bus) const noexcept;

    // 仅供音频资源适配层使用，避免在公共头文件中暴露 miniaudio 类型。
    void* nativeEngineHandle() noexcept;
    void* nativeBusHandle(AudioBus bus) noexcept;

private:
    struct Impl;
    Memory::UniquePtr<Impl> m_impl;
};

// AudioResource 通过该绑定取得应用唯一的音频引擎。AudioEngine 初始化成功时会
// 自动绑定自身，shutdown 时会在仍绑定自身的情况下自动解除绑定。
void SetGlobalAudioEngine(AudioEngine* engine) noexcept;
AudioEngine* GetGlobalAudioEngine() noexcept;

} // namespace Tina::Engine
