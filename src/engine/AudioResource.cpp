//
// AudioResource 实现
//

#include "AudioResource.hpp"
#include "../core/Log.hpp"
#include <cstring>

namespace Tina::Engine {

// ==================== Resource 接口实现 ====================

bool AudioResource::load(const FileSystem::Content& blob) {
    // 使用 SDL_LoadWAV_IO 从内存加载 WAV 文件
    SDL_IOStream* io = SDL_IOFromConstMem(blob.data(), static_cast<size_t>(blob.size()));
    if (!io) {
        TINA_ERROR("AudioResource::load - 无法创建 IOStream：{}", SDL_GetError());
        return false;
    }
    
    // 加载 WAV 数据
    bool success = SDL_LoadWAV_IO(io, true, &m_spec, &m_audioData, &m_audioLen);
    if (!success) {
        TINA_ERROR("AudioResource::load - 无法加载 WAV 文件 [{}]：{}", getPath().c_str(), SDL_GetError());
        return false;
    }
    
    TINA_INFO("AudioResource::load - 成功加载音频 [{}]", getPath().c_str());
    TINA_INFO("  采样率: {} Hz", m_spec.freq);
    TINA_INFO("  声道数: {}", m_spec.channels);
    TINA_INFO("  格式: {}", SDL_GetAudioFormatName(m_spec.format));
    TINA_INFO("  数据大小: {} 字节", m_audioLen);
    TINA_INFO("  时长: {:.2f} 秒", getDuration());
    
    return true;
}

void AudioResource::unload() {
    // 停止播放并销毁音频流
    stop();
    destroyStream();
    
    // 释放音频数据
    if (m_audioData) {
        SDL_free(m_audioData);
        m_audioData = nullptr;
        m_audioLen = 0;
    }
    
    TINA_INFO("AudioResource::unload - 卸载音频 [{}]", getPath().c_str());
}

// ==================== 播放控制 ====================

bool AudioResource::play(bool loop) {
    if (getState() != State::READY) {
        TINA_WARN("AudioResource::play - 音频未准备好 [{}]", getPath().c_str());
        return false;
    }
    
    // 如果已在播放，先停止
    if (m_stream) {
        stop();
    }
    
    // 创建音频流
    if (!createStream()) {
        return false;
    }
    
    m_looping = loop;
    m_paused = false;
    m_playbackPosition = 0;
    
    // 填充初始数据
    fillStream();
    
    // 开始播放
    SDL_ResumeAudioStreamDevice(m_stream);
    
    TINA_INFO("AudioResource::play - 开始播放 [{}]，循环: {}", getPath().c_str(), loop);
    return true;
}

void AudioResource::pause() {
    if (!m_stream || m_paused) {
        return;
    }
    
    SDL_PauseAudioStreamDevice(m_stream);
    m_paused = true;
    
    TINA_INFO("AudioResource::pause - 暂停播放 [{}]", getPath().c_str());
}

void AudioResource::resume() {
    if (!m_stream || !m_paused) {
        return;
    }
    
    SDL_ResumeAudioStreamDevice(m_stream);
    m_paused = false;
    
    TINA_INFO("AudioResource::resume - 恢复播放 [{}]", getPath().c_str());
}

void AudioResource::stop() {
    if (!m_stream) {
        return;
    }
    
    // 暂停并清空流
    SDL_PauseAudioStreamDevice(m_stream);
    SDL_ClearAudioStream(m_stream);
    
    m_paused = false;
    m_playbackPosition = 0;
    
    TINA_INFO("AudioResource::stop - 停止播放 [{}]", getPath().c_str());
}

bool AudioResource::isPlaying() const {
    if (!m_stream) {
        return false;
    }
    
    // 检查设备是否在播放状态
    return !SDL_AudioStreamDevicePaused(m_stream) && !m_paused;
}

bool AudioResource::isPaused() const {
    return m_paused;
}

// ==================== 音量控制 ====================

void AudioResource::setVolume(float volume) {
    // 限制音量范围 [0.0, 1.0]
    m_volume = std::max(0.0f, std::min(volume, 1.0f));
    
    // 如果音频流存在，应用音量
    if (m_stream) {
        SDL_SetAudioStreamGain(m_stream, m_volume);
    }
    
    TINA_INFO("AudioResource::setVolume - 设置音量 [{}]: {:.2f}", getPath().c_str(), m_volume);
}

// ==================== 音频信息 ====================

float AudioResource::getDuration() const {
    if (m_audioLen == 0 || m_spec.freq == 0) {
        return 0.0f;
    }
    
    // 计算时长：数据大小 / (采样率 * 声道数 * 采样大小)
    int frameSize = SDL_AUDIO_FRAMESIZE(m_spec);
    int frames = m_audioLen / frameSize;
    return static_cast<float>(frames) / static_cast<float>(m_spec.freq);
}

// ==================== 私有方法 ====================

bool AudioResource::createStream() {
    // 打开默认播放设备并创建音频流
    m_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &m_spec,
        nullptr,  // callback（我们手动填充数据）
        nullptr   // userdata
    );
    
    if (!m_stream) {
        TINA_ERROR("AudioResource::createStream - 无法创建音频流：{}", SDL_GetError());
        return false;
    }
    
    // 设置初始音量
    SDL_SetAudioStreamGain(m_stream, m_volume);
    
    TINA_INFO("AudioResource::createStream - 创建音频流成功");
    return true;
}

void AudioResource::destroyStream() {
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
        TINA_INFO("AudioResource::destroyStream - 销毁音频流");
    }
}

void AudioResource::fillStream() {
    if (!m_stream || !m_audioData) {
        return;
    }
    
    // 将音频数据放入流中
    bool success = SDL_PutAudioStreamData(m_stream, m_audioData, m_audioLen);
    if (!success) {
        TINA_ERROR("AudioResource::fillStream - 填充音频数据失败：{}", SDL_GetError());
        return;
    }
    
    // 如果是循环播放，检查流中剩余数据量，不足时补充
    if (m_looping) {
        int queued = SDL_GetAudioStreamQueued(m_stream);
        // 保持流中至少有 1 秒的数据（避免播放中断）
        int targetQueued = m_spec.freq * SDL_AUDIO_FRAMESIZE(m_spec);
        
        while (queued < targetQueued) {
            success = SDL_PutAudioStreamData(m_stream, m_audioData, m_audioLen);
            if (!success) {
                break;
            }
            queued += m_audioLen;
        }
    }
}

} // namespace Tina::Engine
