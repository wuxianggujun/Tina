//
// AudioResource - 音频资源
// - 使用 SDL3 的 SDL_AudioStream 和 SDL_LoadWAV 实现
// - 支持 WAV 格式音频文件的加载和播放
//

#pragma once

#include "Resource.hpp"
#include <SDL3/SDL.h>

namespace Tina::Engine {

/**
 * AudioResource - 音频资源
 * 
 * 职责：
 * - 加载 WAV 文件（使用 SDL_LoadWAV）
 * - 管理音频数据和音频规格
 * - 支持播放控制（播放/暂停/停止/循环）
 * - 音量控制
 * 
 * 设计：
 * - 音频数据：m_audioData（PCM 样本）
 * - 音频规格：m_spec（采样率、声道数、格式）
 * - 播放状态：m_stream（SDL_AudioStream）
 * - 循环控制：m_looping（是否循环）
 * 
 * 使用示例：
 *   auto* audio = hub->load<AudioResource>("sounds/bgm.wav");
 *   audio->play(true);  // 循环播放
 *   audio->setVolume(0.5f);  // 50% 音量
 *   audio->stop();
 */
class AudioResource : public Resource {
public:
    static inline const ResourceType TYPE{"audio"};

    explicit AudioResource(const Path& path)
        : Resource(path) {}
    
    ~AudioResource() override { unload(); }

    ResourceType getType() const override { return TYPE; }

    // ==================== 播放控制 ====================

    /**
     * 播放音频
     * @param loop 是否循环播放
     * @return 成功返回 true
     */
    bool play(bool loop = false);

    /**
     * 暂停播放（可恢复）
     */
    void pause();

    /**
     * 恢复播放
     */
    void resume();

    /**
     * 停止播放（重置播放位置）
     */
    void stop();

    /**
     * 查询是否正在播放
     */
    bool isPlaying() const;

    /**
     * 查询是否已暂停
     */
    bool isPaused() const;

    // ==================== 音量控制 ====================

    /**
     * 设置音量（0.0 ~ 1.0）
     */
    void setVolume(float volume);

    /**
     * 获取当前音量
     */
    float getVolume() const { return m_volume; }

    // ==================== 音频信息 ====================

    /**
     * 获取音频时长（秒）
     */
    float getDuration() const;

    /**
     * 获取音频规格（采样率、声道数等）
     */
    const SDL_AudioSpec& getSpec() const { return m_spec; }

protected:
    // Resource 接口实现
    bool load(const FileSystem::Content& blob) override;
    void unload() override;

private:
    // 创建音频流（用于播放）
    bool createStream();
    
    // 销毁音频流
    void destroyStream();
    
    // 填充音频流数据（用于循环播放）
    void fillStream();

private:
    // 音频数据（PCM 样本）
    Uint8* m_audioData = nullptr;
    Uint32 m_audioLen = 0;
    
    // 音频规格
    SDL_AudioSpec m_spec = {};
    
    // 播放状态
    SDL_AudioStream* m_stream = nullptr;
    bool m_looping = false;
    bool m_paused = false;
    
    // 音量控制（0.0 ~ 1.0）
    float m_volume = 1.0f;
    
    // 当前播放位置（用于循环播放）
    Uint32 m_playbackPosition = 0;
};

} // namespace Tina::Engine
