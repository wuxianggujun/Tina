//
// AudioEngine - miniaudio 实现
//

#include "AudioEngine.hpp"
#include "../core/Log.hpp"

#include <algorithm>
#include <atomic>
#include <miniaudio.h>

namespace Tina::Engine {
namespace {

std::atomic<AudioEngine*> g_audioEngine{nullptr};

float clampVolume(float volume) noexcept {
    return std::clamp(volume, 0.0f, 1.0f);
}

} // namespace

struct AudioEngine::Impl {
    ma_engine engine{};
    ma_sound_group musicGroup{};
    ma_sound_group sfxGroup{};

    bool engineInitialized = false;
    bool musicGroupInitialized = false;
    bool sfxGroupInitialized = false;

    float masterVolume = 1.0f;
    float musicVolume = 1.0f;
    float sfxVolume = 1.0f;
};

AudioEngine::AudioEngine()
    : m_impl(Memory::MakeUnique<Impl>()) {
}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::initialize() {
    if (m_impl->engineInitialized) {
        return true;
    }

    ma_engine_config config = ma_engine_config_init();
    const ma_result engineResult = ma_engine_init(&config, &m_impl->engine);
    if (engineResult != MA_SUCCESS) {
        TINA_ERROR("AudioEngine::initialize - 初始化 miniaudio 失败：{} ({})",
                   ma_result_description(engineResult), static_cast<int>(engineResult));
        return false;
    }
    m_impl->engineInitialized = true;

    const ma_result musicResult = ma_sound_group_init(
        &m_impl->engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &m_impl->musicGroup);
    if (musicResult != MA_SUCCESS) {
        TINA_ERROR("AudioEngine::initialize - 创建 Music 总线失败：{} ({})",
                   ma_result_description(musicResult), static_cast<int>(musicResult));
        shutdown();
        return false;
    }
    m_impl->musicGroupInitialized = true;

    const ma_result sfxResult = ma_sound_group_init(
        &m_impl->engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &m_impl->sfxGroup);
    if (sfxResult != MA_SUCCESS) {
        TINA_ERROR("AudioEngine::initialize - 创建 SFX 总线失败：{} ({})",
                   ma_result_description(sfxResult), static_cast<int>(sfxResult));
        shutdown();
        return false;
    }
    m_impl->sfxGroupInitialized = true;

    ma_engine_set_volume(&m_impl->engine, m_impl->masterVolume);
    ma_sound_group_set_volume(&m_impl->musicGroup, m_impl->musicVolume);
    ma_sound_group_set_volume(&m_impl->sfxGroup, m_impl->sfxVolume);

    AudioEngine* expected = nullptr;
    if (!g_audioEngine.compare_exchange_strong(expected, this) && expected != this) {
        TINA_ERROR("AudioEngine::initialize - 已存在另一个全局 AudioEngine");
        shutdown();
        return false;
    }

    TINA_INFO("AudioEngine 初始化成功 - {} Hz, {} channels",
              ma_engine_get_sample_rate(&m_impl->engine),
              ma_engine_get_channels(&m_impl->engine));
    return true;
}

void AudioEngine::shutdown() {
    AudioEngine* expected = this;
    g_audioEngine.compare_exchange_strong(expected, nullptr);

    if (m_impl->sfxGroupInitialized) {
        ma_sound_group_uninit(&m_impl->sfxGroup);
        m_impl->sfxGroupInitialized = false;
    }
    if (m_impl->musicGroupInitialized) {
        ma_sound_group_uninit(&m_impl->musicGroup);
        m_impl->musicGroupInitialized = false;
    }
    if (m_impl->engineInitialized) {
        ma_engine_uninit(&m_impl->engine);
        m_impl->engineInitialized = false;
        TINA_INFO("AudioEngine 已关闭");
    }
}

bool AudioEngine::isInitialized() const noexcept {
    return m_impl->engineInitialized;
}

void AudioEngine::setMasterVolume(float volume) {
    m_impl->masterVolume = clampVolume(volume);
    if (m_impl->engineInitialized) {
        const ma_result result = ma_engine_set_volume(&m_impl->engine, m_impl->masterVolume);
        if (result != MA_SUCCESS) {
            TINA_WARN("AudioEngine::setMasterVolume - 设置失败：{} ({})",
                      ma_result_description(result), static_cast<int>(result));
        }
    }
}

float AudioEngine::getMasterVolume() const noexcept {
    return m_impl->masterVolume;
}

void AudioEngine::setMusicVolume(float volume) {
    setBusVolume(AudioBus::Music, volume);
}

float AudioEngine::getMusicVolume() const noexcept {
    return getBusVolume(AudioBus::Music);
}

void AudioEngine::setSfxVolume(float volume) {
    setBusVolume(AudioBus::Sfx, volume);
}

float AudioEngine::getSfxVolume() const noexcept {
    return getBusVolume(AudioBus::Sfx);
}

void AudioEngine::setBusVolume(AudioBus bus, float volume) {
    const float clamped = clampVolume(volume);
    if (bus == AudioBus::Music) {
        m_impl->musicVolume = clamped;
        if (m_impl->musicGroupInitialized) {
            ma_sound_group_set_volume(&m_impl->musicGroup, clamped);
        }
        return;
    }

    m_impl->sfxVolume = clamped;
    if (m_impl->sfxGroupInitialized) {
        ma_sound_group_set_volume(&m_impl->sfxGroup, clamped);
    }
}

float AudioEngine::getBusVolume(AudioBus bus) const noexcept {
    return bus == AudioBus::Music ? m_impl->musicVolume : m_impl->sfxVolume;
}

void* AudioEngine::nativeEngineHandle() noexcept {
    return m_impl->engineInitialized ? &m_impl->engine : nullptr;
}

void* AudioEngine::nativeBusHandle(AudioBus bus) noexcept {
    if (bus == AudioBus::Music) {
        return m_impl->musicGroupInitialized ? &m_impl->musicGroup : nullptr;
    }
    return m_impl->sfxGroupInitialized ? &m_impl->sfxGroup : nullptr;
}

void SetGlobalAudioEngine(AudioEngine* engine) noexcept {
    g_audioEngine.store(engine);
}

AudioEngine* GetGlobalAudioEngine() noexcept {
    return g_audioEngine.load();
}

} // namespace Tina::Engine
