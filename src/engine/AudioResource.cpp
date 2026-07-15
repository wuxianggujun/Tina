//
// AudioResource - miniaudio 实现
//

#include "AudioResource.hpp"
#include "AudioEngine.hpp"
#include "../core/Log.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <miniaudio.h>

namespace Tina::Engine {
namespace {

constexpr std::size_t kOneShotVoiceCount = 8;
constexpr ma_uint32 kSoundFlags =
    MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;

AudioBus busFromTag(const Container::String& tag) noexcept {
    return std::strcmp(tag.c_str(), "music") == 0 ? AudioBus::Music : AudioBus::Sfx;
}

const char* resultDescription(ma_result result) noexcept {
    const char* description = ma_result_description(result);
    return description ? description : "unknown miniaudio error";
}

} // namespace

struct AudioResource::Impl {
    struct Voice {
        ma_audio_buffer buffer{};
        ma_sound sound{};
        bool bufferInitialized = false;
        bool soundInitialized = false;
    };

    AudioEngine* owner = nullptr;
    void* decodedFrames = nullptr;
    ma_uint64 frameCount = 0;
    ma_format format = ma_format_unknown;
    ma_uint32 channels = 0;
    ma_uint32 sampleRate = 0;
    float durationSeconds = 0.0f;

    Voice primaryVoice{};
    std::array<Voice, kOneShotVoiceCount> oneShotVoices{};
    std::size_t nextOneShotVoice = 0;

    bool paused = false;
    bool looping = false;
    float volume = 1.0f;
    Container::String tag;

    bool engineAvailable() const noexcept {
        return owner != nullptr && owner->isInitialized();
    }

    void releaseVoice(Voice& voice) noexcept {
        if (voice.soundInitialized) {
            if (engineAvailable()) {
                ma_sound_stop(&voice.sound);
                ma_sound_uninit(&voice.sound);
            }
            voice.soundInitialized = false;
        }

        if (voice.bufferInitialized) {
            ma_audio_buffer_uninit(&voice.buffer);
            voice.bufferInitialized = false;
        }
    }

    bool initializeVoice(Voice& voice, bool loop) {
        if (!engineAvailable() || decodedFrames == nullptr || frameCount == 0) {
            return false;
        }

        releaseVoice(voice);

        ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
            format, channels, frameCount, decodedFrames, nullptr);
        ma_result result = ma_audio_buffer_init(&bufferConfig, &voice.buffer);
        if (result != MA_SUCCESS) {
            TINA_ERROR("AudioResource - 创建 PCM buffer 失败：{} ({})",
                       resultDescription(result), static_cast<int>(result));
            return false;
        }
        voice.bufferInitialized = true;

        auto* nativeEngine = static_cast<ma_engine*>(owner->nativeEngineHandle());
        auto* nativeGroup = static_cast<ma_sound_group*>(owner->nativeBusHandle(busFromTag(tag)));
        if (nativeEngine == nullptr || nativeGroup == nullptr) {
            releaseVoice(voice);
            return false;
        }

        result = ma_sound_init_from_data_source(
            nativeEngine,
            reinterpret_cast<ma_data_source*>(&voice.buffer),
            kSoundFlags,
            nativeGroup,
            &voice.sound);
        if (result != MA_SUCCESS) {
            TINA_ERROR("AudioResource - 创建播放 voice 失败：{} ({})",
                       resultDescription(result), static_cast<int>(result));
            releaseVoice(voice);
            return false;
        }
        voice.soundInitialized = true;

        ma_sound_set_volume(&voice.sound, volume);
        ma_sound_set_looping(&voice.sound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_reset_stop_time_and_fade(&voice.sound);
        return true;
    }

    void releaseAllVoices() noexcept {
        releaseVoice(primaryVoice);
        for (Voice& voice : oneShotVoices) {
            releaseVoice(voice);
        }
        nextOneShotVoice = 0;
        paused = false;
        looping = false;
    }
};

AudioResource::AudioResource(const Path& path)
    : Resource(path)
    , m_impl(Memory::MakeUnique<Impl>()) {
}

AudioResource::~AudioResource() {
    unload();
}

void AudioResource::SetGlobalAudioEngine(AudioEngine* engine) noexcept {
    Tina::Engine::SetGlobalAudioEngine(engine);
}

AudioEngine* AudioResource::GetGlobalAudioEngine() noexcept {
    return Tina::Engine::GetGlobalAudioEngine();
}

void AudioResource::SetGlobalMixer(void* audioEngine) noexcept {
    SetGlobalAudioEngine(static_cast<AudioEngine*>(audioEngine));
}

void* AudioResource::GetGlobalMixer() noexcept {
    return GetGlobalAudioEngine();
}

bool AudioResource::load(const FileSystem::Content& blob) {
    unload();

    AudioEngine* engine = GetGlobalAudioEngine();
    if (engine == nullptr || !engine->isInitialized()) {
        TINA_ERROR("AudioResource::load - 全局 AudioEngine 未初始化，无法加载 [{}]",
                   getPath().c_str());
        return false;
    }
    if (blob.empty()) {
        TINA_ERROR("AudioResource::load - 音频数据为空 [{}]", getPath().c_str());
        return false;
    }

    auto* nativeEngine = static_cast<ma_engine*>(engine->nativeEngineHandle());
    if (nativeEngine == nullptr) {
        TINA_ERROR("AudioResource::load - AudioEngine 原生句柄不可用 [{}]",
                   getPath().c_str());
        return false;
    }

    ma_decoder_config decoderConfig = ma_decoder_config_init(
        ma_format_f32,
        ma_engine_get_channels(nativeEngine),
        ma_engine_get_sample_rate(nativeEngine));

    ma_uint64 frameCount = 0;
    void* decodedFrames = nullptr;
    const ma_result result = ma_decode_memory(
        blob.data(), static_cast<std::size_t>(blob.size()),
        &decoderConfig, &frameCount, &decodedFrames);
    if (result != MA_SUCCESS || decodedFrames == nullptr || frameCount == 0) {
        if (decodedFrames != nullptr) {
            ma_free(decodedFrames, nullptr);
        }
        TINA_ERROR("AudioResource::load - 解码失败 [{}]：{} ({})",
                   getPath().c_str(), resultDescription(result), static_cast<int>(result));
        return false;
    }

    m_impl->owner = engine;
    m_impl->decodedFrames = decodedFrames;
    m_impl->frameCount = frameCount;
    m_impl->format = decoderConfig.format;
    m_impl->channels = decoderConfig.channels;
    m_impl->sampleRate = decoderConfig.sampleRate;
    m_impl->durationSeconds = decoderConfig.sampleRate > 0
        ? static_cast<float>(static_cast<double>(frameCount) / decoderConfig.sampleRate)
        : 0.0f;

    TINA_INFO("AudioResource::load - 已加载 [{}]，{} Hz / {} channels / {:.2f} 秒",
              getPath().c_str(), m_impl->sampleRate, m_impl->channels,
              m_impl->durationSeconds);
    return true;
}

void AudioResource::unload() {
    if (!m_impl) {
        return;
    }

    const bool hadAudio = m_impl->decodedFrames != nullptr;
    if (hadAudio && m_impl->owner != nullptr && !m_impl->engineAvailable()) {
        TINA_WARN("AudioResource::unload - AudioEngine 已先于资源关闭；请先销毁 AudioManager [{}]",
                  getPath().c_str());
    }

    m_impl->releaseAllVoices();
    if (m_impl->decodedFrames != nullptr) {
        ma_free(m_impl->decodedFrames, nullptr);
        m_impl->decodedFrames = nullptr;
    }

    m_impl->owner = nullptr;
    m_impl->frameCount = 0;
    m_impl->format = ma_format_unknown;
    m_impl->channels = 0;
    m_impl->sampleRate = 0;
    m_impl->durationSeconds = 0.0f;

    if (hadAudio) {
        TINA_INFO("AudioResource::unload - 已卸载 [{}]", getPath().c_str());
    }
}

bool AudioResource::play(bool loop, int fadeInMs) {
    if (getState() != State::READY || m_impl->decodedFrames == nullptr) {
        TINA_WARN("AudioResource::play - 音频未准备好 [{}]", getPath().c_str());
        return false;
    }
    if (!m_impl->engineAvailable()) {
        TINA_ERROR("AudioResource::play - AudioEngine 不可用 [{}]", getPath().c_str());
        return false;
    }
    if (!m_impl->initializeVoice(m_impl->primaryVoice, loop)) {
        return false;
    }

    ma_sound& sound = m_impl->primaryVoice.sound;
    if (fadeInMs > 0) {
        ma_sound_set_fade_in_milliseconds(
            &sound, 0.0f, 1.0f, static_cast<ma_uint64>(fadeInMs));
    }

    const ma_result result = ma_sound_start(&sound);
    if (result != MA_SUCCESS) {
        TINA_ERROR("AudioResource::play - 播放失败 [{}]：{} ({})",
                   getPath().c_str(), resultDescription(result), static_cast<int>(result));
        m_impl->releaseVoice(m_impl->primaryVoice);
        return false;
    }

    m_impl->paused = false;
    m_impl->looping = loop;
    TINA_INFO("AudioResource::play - 开始播放 [{}]，循环: {}", getPath().c_str(), loop);
    return true;
}

bool AudioResource::playOneShot() {
    if (getState() != State::READY || m_impl->decodedFrames == nullptr) {
        TINA_WARN("AudioResource::playOneShot - 音频未准备好 [{}]", getPath().c_str());
        return false;
    }
    if (!m_impl->engineAvailable()) {
        TINA_ERROR("AudioResource::playOneShot - AudioEngine 不可用 [{}]", getPath().c_str());
        return false;
    }

    Impl::Voice& voice = m_impl->oneShotVoices[m_impl->nextOneShotVoice];
    m_impl->nextOneShotVoice = (m_impl->nextOneShotVoice + 1) % kOneShotVoiceCount;
    if (!m_impl->initializeVoice(voice, false)) {
        return false;
    }

    const ma_result result = ma_sound_start(&voice.sound);
    if (result != MA_SUCCESS) {
        TINA_ERROR("AudioResource::playOneShot - 播放失败 [{}]：{} ({})",
                   getPath().c_str(), resultDescription(result), static_cast<int>(result));
        m_impl->releaseVoice(voice);
        return false;
    }
    return true;
}

void AudioResource::pause() {
    if (!m_impl->primaryVoice.soundInitialized || !isPlaying()) {
        return;
    }

    // 暂停优先级高于正在进行的淡出；清除定时停止与淡变，恢复时从当前游标继续。
    ma_sound_reset_stop_time_and_fade(&m_impl->primaryVoice.sound);
    if (ma_sound_stop(&m_impl->primaryVoice.sound) == MA_SUCCESS) {
        m_impl->paused = true;
        TINA_INFO("AudioResource::pause - 已暂停 [{}]", getPath().c_str());
    }
}

void AudioResource::resume() {
    if (!m_impl->primaryVoice.soundInitialized || !m_impl->paused ||
        !m_impl->engineAvailable()) {
        return;
    }

    ma_sound_reset_stop_time_and_fade(&m_impl->primaryVoice.sound);
    const ma_result result = ma_sound_start(&m_impl->primaryVoice.sound);
    if (result == MA_SUCCESS) {
        m_impl->paused = false;
        TINA_INFO("AudioResource::resume - 已恢复 [{}]", getPath().c_str());
    } else {
        TINA_ERROR("AudioResource::resume - 恢复失败 [{}]：{} ({})",
                   getPath().c_str(), resultDescription(result), static_cast<int>(result));
    }
}

void AudioResource::stop(int fadeOutMs) {
    if (!m_impl->primaryVoice.soundInitialized || !m_impl->engineAvailable()) {
        return;
    }

    ma_sound& sound = m_impl->primaryVoice.sound;
    ma_result result = MA_SUCCESS;
    if (fadeOutMs > 0 && ma_sound_is_playing(&sound)) {
        result = ma_sound_stop_with_fade_in_milliseconds(
            &sound, static_cast<ma_uint64>(fadeOutMs));
    } else {
        ma_sound_reset_stop_time_and_fade(&sound);
        result = ma_sound_stop(&sound);
        if (result == MA_SUCCESS) {
            ma_sound_seek_to_pcm_frame(&sound, 0);
        }
    }

    if (result != MA_SUCCESS) {
        TINA_WARN("AudioResource::stop - 停止失败 [{}]：{} ({})",
                  getPath().c_str(), resultDescription(result), static_cast<int>(result));
    }
    m_impl->paused = false;
    TINA_INFO("AudioResource::stop - 已停止 [{}]，淡出: {} ms", getPath().c_str(), fadeOutMs);
}

bool AudioResource::isPlaying() const {
    if (!m_impl->primaryVoice.soundInitialized || !m_impl->engineAvailable()) {
        return false;
    }
    return !m_impl->paused && ma_sound_is_playing(&m_impl->primaryVoice.sound) == MA_TRUE;
}

bool AudioResource::isPaused() const {
    return m_impl->primaryVoice.soundInitialized && m_impl->paused;
}

void AudioResource::setVolume(float volume) {
    m_impl->volume = std::clamp(volume, 0.0f, 1.0f);
    if (m_impl->primaryVoice.soundInitialized) {
        ma_sound_set_volume(&m_impl->primaryVoice.sound, m_impl->volume);
    }
    for (Impl::Voice& voice : m_impl->oneShotVoices) {
        if (voice.soundInitialized) {
            ma_sound_set_volume(&voice.sound, m_impl->volume);
        }
    }
    TINA_INFO("AudioResource::setVolume - [{}]: {:.2f}", getPath().c_str(), m_impl->volume);
}

float AudioResource::getVolume() const {
    return m_impl->volume;
}

void AudioResource::setTag(const char* tag) {
    m_impl->tag = tag ? tag : "";
}

const char* AudioResource::getTag() const {
    return m_impl->tag.c_str();
}

float AudioResource::getDuration() const {
    return m_impl->durationSeconds;
}

} // namespace Tina::Engine
