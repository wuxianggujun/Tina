//
// AudioResource 实现（基于 SDL3_mixer 3.x）
// 使用 Pimpl 模式隐藏 SDL 依赖
//

#include "AudioResource.hpp"
#include "../core/Log.hpp"
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <SDL3/SDL_properties.h>
#include <algorithm>

namespace Tina::Engine {

// ==================== Pimpl 实现类 ====================

struct AudioResource::Impl {
    // 音频数据与播放轨道
    MIX_Audio* audio = nullptr;
    MIX_Track* track = nullptr;
    
    // 播放状态
    bool isPlaying = false;
    bool looping = false;
    
    // 音量控制（0.0 ~ 1.0）
    float volume = 1.0f;
    
    // 分组标签（用于 MIX_TagTrack），空表示不打标签
    Container::String tag;
    
    // 全局 MIX_Mixer 由应用生命周期管理
    static inline MIX_Mixer* s_mixer = nullptr;
};

// ==================== 构造/析构 ====================

AudioResource::AudioResource(const Path& path)
    : Resource(path)
    , m_impl(Memory::MakeUnique<Impl>()) {
}

AudioResource::~AudioResource() {
    unload();
}

// ==================== 静态全局 Mixer 管理 ====================

void AudioResource::SetGlobalMixer(void* mixer) {
    Impl::s_mixer = static_cast<MIX_Mixer*>(mixer);
}

void* AudioResource::GetGlobalMixer() {
    return Impl::s_mixer;
}

// ==================== Resource 接口实现 ====================

bool AudioResource::load(const FileSystem::Content& blob) {
    // 需要有效的全局 Mixer
    MIX_Mixer* mixer = Impl::s_mixer;
    if (!mixer) {
        TINA_ERROR("AudioResource::load - 全局 Mixer 未初始化，无法加载音频 [{}]", getPath().c_str());
        return false;
    }

    // 使用指定解码器优先策略加载，避免多解码器探测时第三方库在 stderr 打印噪声
    SDL_IOStream* io = SDL_IOFromConstMem(blob.data(), static_cast<size_t>(blob.size()));
    if (!io) {
        TINA_ERROR("AudioResource::load - 无法创建 IOStream：{}", SDL_GetError());
        return false;
    }

    auto try_load_with_decoder = [&](const char* decoder) -> MIX_Audio* {
        SDL_PropertiesID props = SDL_CreateProperties();
        if (!props) return nullptr;
        SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER, io);
        SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN, true);
        SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_PREDECODE_BOOLEAN, true);
        SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_PREFERRED_MIXER_POINTER, mixer);
        if (decoder && *decoder) {
            SDL_SetStringProperty(props, MIX_PROP_AUDIO_DECODER_STRING, decoder);
        }
        MIX_Audio* audio = MIX_LoadAudioWithProperties(props);
        SDL_DestroyProperties(props);
        return audio;
    };

    // 优先尝试 DRMP3（多数情况下内置且安静），失败再尝试 MPG123，最后回退通用路径（可能触发多解码器探测）
    m_impl->audio = try_load_with_decoder("DRMP3");
    if (!m_impl->audio) {
        m_impl->audio = try_load_with_decoder("MPG123");
    }
    if (!m_impl->audio) {
        // 回退：由 SDL_mixer 自行选择解码器（可能在个别平台打印外部库提示）
        m_impl->audio = MIX_LoadAudio_IO(mixer, io, /*predecode=*/true, /*closeio=*/true);
    }
    if (!m_impl->audio) {
        TINA_ERROR("AudioResource::load - 无法加载音频文件 [{}]：{}", getPath().c_str(), SDL_GetError());
        return false;
    }
    
    TINA_INFO("AudioResource::load - 成功加载音频 [{}]", getPath().c_str());

    // 可选：打印初始格式与时长
    SDL_AudioSpec spec{};
    if (MIX_GetAudioFormat(m_impl->audio, &spec)) {
        TINA_INFO("  格式: freq={} Hz, channels={}, format=0x{:X}", spec.freq, spec.channels, static_cast<unsigned int>(spec.format));
    }
    Sint64 frames = MIX_GetAudioDuration(m_impl->audio);
    if (frames >= 0) {
        const Sint64 ms = MIX_AudioFramesToMS(m_impl->audio, frames);
        if (ms >= 0) {
            TINA_INFO("  时长: {:.2f} 秒", double(ms) / 1000.0);
        }
    }
    
    return true;
}

void AudioResource::unload() {
    if (!m_impl) return;
    
    // 停止播放
    if (m_impl->isPlaying) {
        stop();
    }
    
    // 释放轨道
    if (m_impl->track) {
        // 停止并销毁轨道
        MIX_StopTrack(m_impl->track, 0);
        MIX_DestroyTrack(m_impl->track);
        m_impl->track = nullptr;
    }

    // 释放音频资源
    if (m_impl->audio) {
        MIX_DestroyAudio(m_impl->audio);
        m_impl->audio = nullptr;
    }
    
    m_impl->isPlaying = false;
    m_impl->looping = false;
    
    TINA_INFO("AudioResource::unload - 卸载音频 [{}]", getPath().c_str());
}

// ==================== 播放控制 ====================

bool AudioResource::play(bool loop, int fadeInMs) {
    if (getState() != State::READY || !m_impl->audio) {
        TINA_WARN("AudioResource::play - 音频未准备好 [{}]", getPath().c_str());
        return false;
    }
    
    MIX_Mixer* mixer = Impl::s_mixer;
    if (!mixer) {
        TINA_ERROR("AudioResource::play - 全局 Mixer 未初始化");
        return false;
    }

    // 懒创建并复用轨道
    if (!m_impl->track) {
        m_impl->track = MIX_CreateTrack(mixer);
        if (!m_impl->track) {
            TINA_ERROR("AudioResource::play - 创建播放轨道失败：{}", SDL_GetError());
            return false;
        }
    }

    // 若设置了分组标签，则为轨道打上标签（用于分组音量控制）
    if (!m_impl->tag.empty()) {
        MIX_TagTrack(m_impl->track, m_impl->tag.c_str());
    }

    if (!MIX_SetTrackAudio(m_impl->track, m_impl->audio)) {
        TINA_ERROR("AudioResource::play - 绑定音频到轨道失败：{}", SDL_GetError());
        return false;
    }

    // 播放参数（循环/淡入）
    SDL_PropertiesID opts = SDL_CreateProperties();
    if (opts) {
        const Sint64 loops = loop ? -1 : 0; // -1 = 无限循环，0 = 播放一次
        SDL_SetNumberProperty(opts, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
        if (fadeInMs > 0) {
            SDL_SetNumberProperty(opts, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, (Sint64)fadeInMs);
        }
    }

    if (!MIX_PlayTrack(m_impl->track, opts)) {
        if (opts) SDL_DestroyProperties(opts);
        TINA_ERROR("AudioResource::play - 播放失败：{}", SDL_GetError());
        return false;
    }

    if (opts) SDL_DestroyProperties(opts);

    // 设置音量（增益 0.0~1.0）
    MIX_SetTrackGain(m_impl->track, std::max(0.0f, std::min(m_impl->volume, 1.0f)));

    m_impl->isPlaying = true;
    m_impl->looping = loop;
    TINA_INFO("AudioResource::play - 开始播放 [{}]，循环: {}", getPath().c_str(), loop);
    return true;
}

bool AudioResource::playOneShot() {
    if (getState() != State::READY || !m_impl->audio) {
        TINA_WARN("AudioResource::playOneShot - 音频未准备好 [{}]", getPath().c_str());
        return false;
    }
    MIX_Mixer* mixer = Impl::s_mixer;
    if (!mixer) {
        TINA_ERROR("AudioResource::playOneShot - 全局 Mixer 未初始化");
        return false;
    }
    if (!MIX_PlayAudio(mixer, m_impl->audio)) {
        TINA_ERROR("AudioResource::playOneShot - 播放失败：{}", SDL_GetError());
        return false;
    }
    return true;
}

void AudioResource::pause() {
    if (!m_impl->isPlaying) {
        return;
    }
    
    if (m_impl->track) {
        MIX_PauseTrack(m_impl->track);
    }
    
    TINA_INFO("AudioResource::pause - 暂停播放 [{}]", getPath().c_str());
}

void AudioResource::resume() {
    if (!m_impl->track) return;
    if (!MIX_TrackPaused(m_impl->track)) return;

    MIX_ResumeTrack(m_impl->track);
    
    TINA_INFO("AudioResource::resume - 恢复播放 [{}]", getPath().c_str());
}

void AudioResource::stop(int fadeOutMs) {
    if (!m_impl->isPlaying) {
        return;
    }
    if (m_impl->track) {
        if (fadeOutMs > 0) {
            SDL_AudioSpec spec{};
            Sint64 frames = 0;
            if (m_impl->audio && MIX_GetAudioFormat(m_impl->audio, &spec)) {
                frames = MIX_MSToFrames(spec.freq, (Sint64)fadeOutMs);
            }
            MIX_StopTrack(m_impl->track, frames > 0 ? frames : 0);
        } else {
            MIX_StopTrack(m_impl->track, 0);
        }
    }
    m_impl->isPlaying = false;
    
    TINA_INFO("AudioResource::stop - 停止播放 [{}]", getPath().c_str());
}

bool AudioResource::isPlaying() const {
    if (!m_impl->track) return false;
    return MIX_TrackPlaying(m_impl->track);
}

bool AudioResource::isPaused() const {
    if (!m_impl->track) return false;
    return MIX_TrackPaused(m_impl->track);
}

// ==================== 音量控制 ====================

void AudioResource::setVolume(float volume) {
    // 限制音量范围 [0.0, 1.0]
    m_impl->volume = std::max(0.0f, std::min(volume, 1.0f));
    
    // 应用增益（0.0 ~ 1.0）
    if (m_impl->track) {
        MIX_SetTrackGain(m_impl->track, m_impl->volume);
    }
    
    TINA_INFO("AudioResource::setVolume - 设置音量 [{}]: {:.2f}", getPath().c_str(), m_impl->volume);
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

// ==================== 音频信息 ====================

float AudioResource::getDuration() const {
    if (!m_impl->audio) {
        return 0.0f;
    }
    
    // 使用 SDL_mixer 3.0 接口获取帧数并换算为秒
    const Sint64 frames = MIX_GetAudioDuration(m_impl->audio);
    if (frames < 0) return 0.0f; // 未知或无限
    const Sint64 ms = MIX_AudioFramesToMS(m_impl->audio, frames);
    if (ms < 0) return 0.0f;
    return static_cast<float>(double(ms) / 1000.0);
}

} // namespace Tina::Engine
