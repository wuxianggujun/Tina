#include "tina/renderer/TextureAtlas.hpp"
#include "tina/log/Logger.hpp"

namespace Tina {

TextureAtlas::TextureAtlas() {
    createAtlasTexture();
}

TextureAtlas::~TextureAtlas() {
    clear();
}

bool TextureAtlas::addTexture(const std::string& name, const std::shared_ptr<Texture2D>& texture) {
    if (!texture || !texture->isValid()) {
        TINA_CORE_LOG_ERROR("Attempting to add invalid texture to atlas");
        return false;
    }

    // 检查纹理是否已存在
    if (m_Regions.find(name) != m_Regions.end()) {
        TINA_CORE_LOG_WARN("Texture '{}' already exists in atlas", name);
        return false;
    }

    uint32_t width = texture->getWidth();
    uint32_t height = texture->getHeight();

    // 检查纹理大小是否合适
    if (width > ATLAS_SIZE || height > ATLAS_SIZE) {
        TINA_CORE_LOG_ERROR("Texture '{}' is too large for atlas ({}x{})", name, width, height);
        return false;
    }

    // 在现有图集中查找空间
    Node* node = nullptr;
    uint32_t atlasIndex = 0;
    
    for (size_t i = 0; i < m_RootNodes.size(); ++i) {
        if ((node = findNode(m_RootNodes[i].get(), width, height))) {
            atlasIndex = static_cast<uint32_t>(i);
            break;
        }
    }

    // 如果没有找到空间,尝试创建新的图集
    if (!node) {
        if (m_AtlasTextures.size() >= MAX_ATLASES) {
            TINA_CORE_LOG_ERROR("Maximum number of texture atlases reached");
            return false;
        }
        
        if (!createAtlasTexture()) {
            return false;
        }
        
        atlasIndex = static_cast<uint32_t>(m_RootNodes.size() - 1);
        node = findNode(m_RootNodes.back().get(), width, height);
        
        if (!node) {
            TINA_CORE_LOG_ERROR("Failed to find space for texture in new atlas");
            return false;
        }
    }

    // 分割节点并更新使用状态
    node = splitNode(node, width, height);
    node->used = true;

    // 计算UV坐标
    float u = node->rect.x / ATLAS_SIZE;
    float v = node->rect.y / ATLAS_SIZE;
    float w = width / static_cast<float>(ATLAS_SIZE);
    float h = height / static_cast<float>(ATLAS_SIZE);

    // 创建区域信息
    AtlasRegion region;
    region.uvRect = glm::vec4(u, v, w, h);
    region.size = glm::vec2(width, height);
    region.atlasIndex = atlasIndex;

    // 复制纹理数据到图集
    // TODO: 实现纹理数据复制

    m_Regions[name] = region;
    return true;
}

const AtlasRegion* TextureAtlas::getRegion(const std::string& name) const {
    auto it = m_Regions.find(name);
    return it != m_Regions.end() ? &it->second : nullptr;
}

std::shared_ptr<Texture2D> TextureAtlas::getAtlasTexture(uint32_t index) const {
    return index < m_AtlasTextures.size() ? m_AtlasTextures[index] : nullptr;
}

void TextureAtlas::clear() {
    m_AtlasTextures.clear();
    m_RootNodes.clear();
    m_Regions.clear();
    createAtlasTexture();
}

TextureAtlas::Node* TextureAtlas::findNode(Node* node, uint32_t width, uint32_t height) {
    if (!node) return nullptr;

    // 如果节点已被使用,继续搜索子节点
    if (node->used) {
        Node* leftResult = findNode(node->left.get(), width, height);
        return leftResult ? leftResult : findNode(node->right.get(), width, height);
    }

    // 检查节点大小是否足够
    if (width <= node->rect.z && height <= node->rect.w) {
        return node;
    }

    return nullptr;
}

TextureAtlas::Node* TextureAtlas::splitNode(Node* node, uint32_t width, uint32_t height) {
    if (!node) return nullptr;

    // 创建子节点
    float remainingWidth = node->rect.z - width;
    float remainingHeight = node->rect.w - height;

    node->left = std::make_unique<Node>();
    node->right = std::make_unique<Node>();

    if (remainingWidth > remainingHeight) {
        // 水平分割
        node->left->rect = glm::vec4(node->rect.x + width, node->rect.y, remainingWidth, height);
        node->right->rect = glm::vec4(node->rect.x, node->rect.y + height, node->rect.z, remainingHeight);
    } else {
        // 垂直分割
        node->left->rect = glm::vec4(node->rect.x, node->rect.y + height, width, remainingHeight);
        node->right->rect = glm::vec4(node->rect.x + width, node->rect.y, remainingWidth, height);
    }

    // 更新当前节点的大小
    node->rect.z = width;
    node->rect.w = height;

    return node;
}

bool TextureAtlas::createAtlasTexture() {
    // 创建新的图集纹理
    auto texture = std::make_shared<Texture2D>();
    // TODO: 初始化纹理

    // 创建根节点
    auto root = std::make_unique<Node>();
    root->rect = glm::vec4(0, 0, ATLAS_SIZE, ATLAS_SIZE);

    m_AtlasTextures.push_back(texture);
    m_RootNodes.push_back(std::move(root));

    return true;
}

} // namespace Tina 