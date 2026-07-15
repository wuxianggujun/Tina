//
// AudioResource - 音频资源
// - 使用 SDL_mixer 实现多格式音频支持
// - 支持格式：WAV, MP3, OGG, FLAC, MOD, MIDI 等
// - 基于 SDL3_mixer（3.x）音频管线
// - 音乐/音效统一以 MIX_Audio 表示，通过 MIX_Track 播放
// - 使用 Pimpl 模式隐藏 SDL 依赖
//

#pragma once

#include "Resource.hpp"
#include "../core/Memory.hpp"

namespace Tina::Engine {

/**
 * AudioResource - 音频资源（基于 SDL_mixer）
 * 
 * 职责：
 * - 加载音频文件（支持 WAV/MP3/OGG/FLAC/MOD/MIDI）
 * - 管理音频资源
 * - 支持播放控制（播放/暂停/停止/循环）
 * - 音量控制（独立音量和全局音量）
 * 
 * 设计：
 * - 使用 Pimpl 模式隐藏 SDL 实现细节
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

    explicit AudioResource(const Path& path);
    ~AudioResource() override;

    // 禁止拷贝和移动（因为包含 Pimpl）
    AudioResource(const AudioResource&) = delete;
    AudioResource& operator=(const AudioResource&) = delete;
    AudioResource(AudioResource&&) = delete;
    AudioResource& operator=(AudioResource&&) = delete;

    ResourceType getType() const override { return TYPE; }

    // ==================== 播放控制 ====================

    /**
     * 播放音频
     * @param loop 是否循环播放
     * @param fadeInMs 淡入时间（毫秒）
     * @return 成功返回 true
     */
    bool play(bool loop = false, int fadeInMs = 0);

    /**
     * 便捷：一次性播放（不支持单独设置音量/淡入）
     */
    bool playOneShot();

    /**
     * 设置播放标签（用于分组音量控制），如 "music" 或 "sfx"
     */
    void setTag(const char* tag);
    
    /**
     * 获取播放标签
     */
    const char* getTag() const;

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
     * @param fadeOutMs 淡出时间（毫秒）
     */
    void stop(int fadeOutMs = 0);

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
    float getVolume() const;

    // ==================== 音频信息 ====================

    /**
     * 获取音频时长（秒）
     */
    float getDuration() const;

    /**
     * 设置全局 Mixer（由 Application 在初始化后设置）
     * @param mixer 不透明指针，实际类型为 MIX_Mixer*
     */
    static void SetGlobalMixer(void* mixer);
    
    /**
     * 获取全局 Mixer
     * @return 不透明指针，实际类型为 MIX_Mixer*
     */
    static void* GetGlobalMixer();

protected:
    // Resource 接口实现
    bool load(const FileSystem::Content& blob) override;
    void unload() override;

private:
    // Pimpl 实现类（隐藏 SDL 依赖）
    struct Impl;
    Memory::UniquePtr<Impl> m_impl;
};

} // namespace Tina::Engine
