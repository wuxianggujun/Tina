#pragma once

#include "Texture2D.hpp"
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace Tina {

struct AtlasRegion {
    glm::vec4 uvRect;  // x, y, width, height in UV coordinates
    glm::vec2 size;    // Original texture size
    uint32_t atlasIndex; // Which atlas texture this region belongs to
};

class TextureAtlas {
public:
    static constexpr uint32_t ATLAS_SIZE = 2048;
    static constexpr uint32_t MAX_ATLASES = 16;

    TextureAtlas();
    ~TextureAtlas();

    // 添加纹理到图集
    bool addTexture(const std::string& name, const std::shared_ptr<Texture2D>& texture);
    
    // 获取纹理区域
    const AtlasRegion* getRegion(const std::string& name) const;
    
    // 获取图集纹理
    std::shared_ptr<Texture2D> getAtlasTexture(uint32_t index) const;
    
    // 清空图集
    void clear();

private:
    struct Node {
        glm::vec4 rect;  // x, y, width, height in pixels
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
        bool used = false;
    };

    // 在图集中查找空间
    Node* findNode(Node* node, uint32_t width, uint32_t height);
    
    // 分割节点
    Node* splitNode(Node* node, uint32_t width, uint32_t height);
    
    // 创建新的图集纹理
    bool createAtlasTexture();

private:
    std::vector<std::shared_ptr<Texture2D>> m_AtlasTextures;
    std::vector<std::unique_ptr<Node>> m_RootNodes;
    std::unordered_map<std::string, AtlasRegion> m_Regions;
};

} // namespace Tina 