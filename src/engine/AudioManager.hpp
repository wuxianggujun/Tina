//
// AudioManager - 音频资源管理器
// - 管理音频资源的加载和卸载
// - 提供便捷的音频加载接口
//

#pragma once

#include "AudioResource.hpp"
#include "Resource.hpp"

namespace Tina::Engine {

/**
 * AudioManager - 音频资源管理器
 * 
 * 职责：
 * - 管理音频资源的加载和卸载
 * - 提供便捷的音频加载接口
 * - 自动缓存已加载的音频
 * 
 * 使用示例：
 *   AudioManager audioMgr(fs);
 *   auto* bgm = audioMgr.loadAudio("sounds/bgm.wav");
 *   bgm->play(true);
 */
class AudioManager : public ResourceManager {
public:
    using ResourceManager::ResourceManager;
    
    Resource* createResource(const Path& path) override {
        return new AudioResource(path);
    }
    
    /**
     * 便捷接口：加载音频资源
     * @param path 音频文件路径（相对于 resources/）
     * @return AudioResource 指针（失败返回 nullptr）
     */
    AudioResource* loadAudio(const Path& path) {
        return static_cast<AudioResource*>(load(path));
    }
};

} // namespace Tina::Engine
