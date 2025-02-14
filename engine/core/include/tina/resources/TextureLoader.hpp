#pragma once

#include "tina/resources/ResourceManager.hpp"
#include "tina/renderer/Texture2D.hpp"
#include <memory>

namespace Tina {

class TextureLoader : public IResourceLoader {
public:
    std::shared_ptr<Resource> load(const std::string& path) override;
    bool reload(Resource* resource) override;

private:
    bool loadTextureData(const std::string& path, Texture2D* texture);
};

// 便捷函数
inline std::shared_ptr<Texture2D> loadTexture(const std::string& path) {
    return ResourceManager::getInstance().load<Texture2D>(path);
}

} // namespace Tina 