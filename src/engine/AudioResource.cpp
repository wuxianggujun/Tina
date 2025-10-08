//
// AudioResource 实现（基于 SDL3_mixer 3.x）
//

#include "AudioResource.hpp"
#include "../core/Log.hpp"
#include <SDL3/SDL_properties.h>
#include <algorithm>

namespace Tina::Engine {

// ==================== Resource 接口实现 ====================

bool AudioResource::load(const FileSystem::Content& blob) {
    // 需要有效的全局 Mixer
    MIX_Mixer* mixer = GetGlobalMixer();
    if (!mixer) {
        TINA_ERROR("AudioResource::load - 全局 Mixer 未初始化，无法加载音频 [{}]", getPath().c_str());
        return false;
    }

    // 使用 MIX_LoadAudio_IO 从内存加载音频
    SDL_IOStream* io = SDL_IOFromConstMem(blob.data(), static_cast<size_t>(blob.size()));
    if (!io) {
        TINA_ERROR("AudioResource::load - 无法创建 IOStream：{}", SDL_GetError());
        return false;
    }
    
    // 预解码到内存（predecode=true），并在返回后自动关闭 IO
    m_audio = MIX_LoadAudio_IO(mixer, io, /*predecode=*/true, /*closeio=*/true);
    if (!m_audio) {
        TINA_ERROR("AudioResource::load - 无法加载音频文件 [{}]：{}", getPath().c_str(), SDL_GetError());
        return false;
    }
    
    TINA_INFO("AudioResource::load - 成功加载音频 [{}]", getPath().c_str());

    // 可选：打印初始格式与时长
    SDL_AudioSpec spec{};
    if (MIX_GetAudioFormat(m_audio, &spec)) {
        TINA_INFO("  格式: freq={} Hz, channels={}, format=0x{:X}", spec.freq, spec.channels, static_cast<unsigned int>(spec.format));
    }
    Sint64 frames = MIX_GetAudioDuration(m_audio);
    if (frames >= 0) {
        const Sint64 ms = MIX_AudioFramesToMS(m_audio, frames);
        if (ms >= 0) {
            TINA_INFO("  时长: {:.2f} 秒", double(ms) / 1000.0);
        }
    }
    
    return true;
}

void AudioResource::unload() {
    // 停止播放
    if (m_isPlaying) {
        stop();
    }
    
    // 释放轨道
    if (m_track) {
        // 停止并销毁轨道
        MIX_StopTrack(m_track, 0);
        MIX_DestroyTrack(m_track);
        m_track = nullptr;
    }

    // 释放音频资源
    if (m_audio) {
        MIX_DestroyAudio(m_audio);
        m_audio = nullptr;
    }
    
    m_isPlaying = false;
    m_looping = false;
    
    TINA_INFO("AudioResource::unload - 卸载音频 [{}]", getPath().c_str());
}

// ==================== 播放控制 ====================

bool AudioResource::play(bool loop) {
    if (getState() != State::READY || !m_audio) {
        TINA_WARN("AudioResource::play - 音频未准备好 [{}]", getPath().c_str());
        return false;
    }
    
    MIX_Mixer* mixer = GetGlobalMixer();
    if (!mixer) {
        TINA_ERROR("AudioResource::play - 全局 Mixer 未初始化");
        return false;
    }

    // 懒创建并复用轨道
    if (!m_track) {
        m_track = MIX_CreateTrack(mixer);
        if (!m_track) {
            TINA_ERROR("AudioResource::play - 创建播放轨道失败：{}", SDL_GetError());
            return false;
        }
    }

    if (!MIX_SetTrackAudio(m_track, m_audio)) {
        TINA_ERROR("AudioResource::play - 绑定音频到轨道失败：{}", SDL_GetError());
        return false;
    }

    // 播放参数（循环）
    SDL_PropertiesID opts = SDL_CreateProperties();
    if (opts) {
        const Sint64 loops = loop ? -1 : 0; // -1 = 无限循环，0 = 播放一次
        SDL_SetNumberProperty(opts, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
    }

    if (!MIX_PlayTrack(m_track, opts)) {
        if (opts) SDL_DestroyProperties(opts);
        TINA_ERROR("AudioResource::play - 播放失败：{}", SDL_GetError());
        return false;
    }

    if (opts) SDL_DestroyProperties(opts);

    // 设置音量（增益 0.0~1.0）
    MIX_SetTrackGain(m_track, std::max(0.0f, std::min(m_volume, 1.0f)));

    m_isPlaying = true;
    m_looping = loop;
    TINA_INFO("AudioResource::play - 开始播放 [{}]，循环: {}", getPath().c_str(), loop);
    return true;
}

void AudioResource::pause() {
    if (!m_isPlaying) {
        return;
    }
    
    if (m_track) {
        MIX_PauseTrack(m_track);
    }
    
    TINA_INFO("AudioResource::pause - 暂停播放 [{}]", getPath().c_str());
}

void AudioResource::resume() {
    if (!m_track) return;
    if (!MIX_TrackPaused(m_track)) return;

    MIX_ResumeTrack(m_track);
    
    TINA_INFO("AudioResource::resume - 恢复播放 [{}]", getPath().c_str());
}

void AudioResource::stop() {
    if (!m_isPlaying) {
        return;
    }
    
    if (m_track) {
        MIX_StopTrack(m_track, 0);
    }
    m_isPlaying = false;
    
    TINA_INFO("AudioResource::stop - 停止播放 [{}]", getPath().c_str());
}

bool AudioResource::isPlaying() const {
    if (!m_track) return false;
    return MIX_TrackPlaying(m_track);
}

bool AudioResource::isPaused() const {
    if (!m_track) return false;
    return MIX_TrackPaused(m_track);
}

// ==================== 音量控制 ====================

void AudioResource::setVolume(float volume) {
    // 限制音量范围 [0.0, 1.0]
    m_volume = std::max(0.0f, std::min(volume, 1.0f));
    
    // 应用增益（0.0 ~ 1.0）
    if (m_track) {
        MIX_SetTrackGain(m_track, m_volume);
    }
    
    TINA_INFO("AudioResource::setVolume - 设置音量 [{}]: {:.2f}", getPath().c_str(), m_volume);
}

// ==================== 音频信息 ====================

float AudioResource::getDuration() const {
    if (!m_audio) {
        return 0.0f;
    }
    
    // 使用 SDL_mixer 3.0 接口获取帧数并换算为秒
    const Sint64 frames = MIX_GetAudioDuration(m_audio);
    if (frames < 0) return 0.0f; // 未知或无限
    const Sint64 ms = MIX_AudioFramesToMS(m_audio, frames);
    if (ms < 0) return 0.0f;
    return static_cast<float>(double(ms) / 1000.0);
}

} // namespace Tina::Engine
