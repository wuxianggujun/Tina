//
// FontResource 与 FontManager
// - 缓存字体文件字节并按需为不同像素大小创建 FT_Face
// - 支持同一路径多字号共享与复用
//

#pragma once

#include "Resource.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H

namespace Tina::Engine {

class FontResource : public Resource {
public:
    static inline const ResourceType TYPE{"font"};

    explicit FontResource(const Path& p) : Resource(p) {}
    ~FontResource() override { unload(); }

    ResourceType getType() const override { return TYPE; }

    // 确保指定像素大小的 Face 可用
    bool ensureFace(int pixelSize);
    // 取得指定像素大小的 Face（确保后返回）
    FT_Face getFace(int pixelSize) const;

protected:
    bool load(const FileSystem::Content& blob) override;
    void unload() override;

private:
    struct FaceEntry { FT_Library lib = nullptr; FT_Face face = nullptr; int sizePx = 0; };
    Tina::Container::Vector<Tina::u8> m_data;  // 字体字节
    Tina::Container::HashMap<int, FaceEntry> m_faces; // sizePx -> Face
};

class FontManager : public ResourceManager {
public:
    using ResourceManager::ResourceManager;
    Resource* createResource(const Path& path) override { return new FontResource(path); }
};

} // namespace Tina::Engine

