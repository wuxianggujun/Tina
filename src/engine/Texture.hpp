//
// Texture2D 资源与管理器
// - 使用 bimg_decode 解码 PNG/TGA/JPG 等常见格式
// - 通过 bgfx 创建纹理句柄，统一由资源系统引用计数与释放
//

#pragma once

#include "Resource.hpp"
#include <bgfx/bgfx.h>

namespace Tina::Engine {

class Texture2DResource : public Resource {
public:
    static inline const ResourceType TYPE{"texture2d"};

    explicit Texture2DResource(const Path& path)
        : Resource(path) {}

    ResourceType getType() const override { return TYPE; }

    bgfx::TextureHandle handle() const { return m_tex; }
    uint16_t width() const { return m_width; }
    uint16_t height() const { return m_height; }

protected:
    bool load(const FileSystem::Content& blob) override;
    void unload() override;

private:
    bgfx::TextureHandle m_tex = BGFX_INVALID_HANDLE;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
};

class TextureManager : public ResourceManager {
public:
    using ResourceManager::ResourceManager;
    Resource* createResource(const Path& path) override { return new Texture2DResource(path); }
};

} // namespace Tina::Engine

