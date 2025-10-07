//
// Scene 实现 - 便捷资源访问方法
//

#include "Scene.hpp"
#include "Application.hpp"

namespace Tina::Engine {

// 便捷访问全局资源（避免每次写 app()->xxx()）

Renderer::ShaderManager& Scene::shaders() const {
    return app()->shaders();
}

UI::TextRenderer& Scene::textRenderer() const {
    return app()->textRenderer();
}

FileSystem& Scene::fileSystem() const {
    return app()->fileSystem();
}

ResourceManagerHub& Scene::resources() const {
    return app()->resources();
}

} // namespace Tina::Engine
