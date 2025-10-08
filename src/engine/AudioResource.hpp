//
// AudioResource - 音频资源
// - 使用 SDL_mixer 实现多格式音频支持
// - 支持格式：WAV, MP3, OGG, FLAC, MOD, MIDI 等
// - 基于 SDL3_mixer（3.x）音频管线
// - 音乐/音效统一以 MIX_Audio 表示，通过 MIX_Track 播放
//

#pragma once

#include "Resource.hpp"
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

namespace Tina::Engine {

/**
 * AudioResource - 音频资源（基于 SDL_mixer）
 * 
 * 职责：
 * - 加载音频文件（支持 WAV/MP3/OGG/FLAC/MOD/MIDI）
 * - 管理音频资源（MIX_Audio）
 * - 支持播放控制（播放/暂停/停止/循环）
 * - 音量控制（独立音量和全局音量）
 * 
 * 设计：
 * - 音频资源：MIX_Audio*（统一的音频数据对象）
 * - 轨道对象：MIX_Track*（用于播放控制，可复用）
 * - 自动格式检测：根据扩展名自动加载
 * 
 * 支持格式：
 * - WAV：未压缩音频
 * - MP3：压缩音乐（需要 mpg123）
 * - OGG：开源压缩格式（需要 Vorbis）
 * - FLAC：无损压缩（需要 FLAC）
 * - MOD/XM/S3M：追踪音乐（需要 libxmp）
 * - MIDI：合成音乐（需要 Timidity）
 * 
 * 使用示例：
 *   auto* audio = hub->load<AudioResource>("sounds/bgm.mp3");
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
    // 播放音频，支持可选淡入（毫秒）
    bool play(bool loop = false, int fadeInMs = 0);

    // 便捷：一次性播放（内部使用 MIX_PlayAudio；不支持单独设置音量/淡入）
    bool playOneShot();

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
     * 设置/获取全局 Mixer（由 Application 在初始化后设置）
     */
    static void SetGlobalMixer(MIX_Mixer* mixer) { s_mixer = mixer; }
    static MIX_Mixer* GetGlobalMixer() { return s_mixer; }

protected:
    // Resource 接口实现
    bool load(const FileSystem::Content& blob) override;
    void unload() override;

private:
    // 全局 MIX_Mixer 由应用生命周期管理
    static inline MIX_Mixer* s_mixer = nullptr;

private:
    // 音频数据与播放轨道
    MIX_Audio* m_audio = nullptr;
    MIX_Track* m_track = nullptr;
    
    // 未来扩展：短音效可共享 m_audio，多轨并行播放
    
    // 播放状态
    bool m_isPlaying = false;
    bool m_looping = false;
    
    // 音量控制（0.0 ~ 1.0）
    float m_volume = 1.0f;
};

} // namespace Tina::Engine
