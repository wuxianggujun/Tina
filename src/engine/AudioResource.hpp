//
// AudioResource - 音频资源
// - 使用 miniaudio 内置解码器播放 WAV、MP3、FLAC
// - 支持 Music/SFX 分组、循环、淡入淡出和 one-shot
// - 使用 Pimpl 模式隐藏 miniaudio 依赖
//

#pragma once

#include "Resource.hpp"
#include "../core/Memory.hpp"

namespace Tina::Engine {

class AudioEngine;

/**
 * AudioResource - 音频资源（基于 miniaudio）
 * 
 * 职责：
 * - 从 FileSystem 内存数据加载 WAV/MP3/FLAC
 * - 管理音频资源
 * - 支持播放控制（播放/暂停/停止/循环）
 * - 音量控制（独立音量和全局音量）
 * 
 * 设计：
 * - 使用 Pimpl 模式隐藏 miniaudio 实现细节
 * - 预解码为 PCM，避免播放期间持有 FileSystem 临时数据
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
     * 绑定全局 AudioEngine（通常由 Application 在初始化后设置）
     */
    static void SetGlobalAudioEngine(AudioEngine* engine) noexcept;
    
    /**
     * 获取全局 AudioEngine
     */
    static AudioEngine* GetGlobalAudioEngine() noexcept;

    // 兼容旧调用点：不透明指针现在必须指向 AudioEngine。
    static void SetGlobalMixer(void* audioEngine) noexcept;
    static void* GetGlobalMixer() noexcept;

protected:
    // Resource 接口实现
    bool load(const FileSystem::Content& blob) override;
    void unload() override;

private:
    // Pimpl 实现类（隐藏 miniaudio 依赖）
    struct Impl;
    Memory::UniquePtr<Impl> m_impl;
};

} // namespace Tina::Engine
