//
// Scene 实现 - 便捷资源访问方法
//

#include "Scene.hpp"
#include "Application.hpp"
#include "../ui/UICore.hpp"
#include "../core/Log.hpp"
#include "SceneRenderer.hpp"

namespace Tina::Engine {

// 虚析构函数定义（必须在此处，SceneRenderer是完整类型）
Scene::~Scene() = default;

// 注意：原先计划在此提供若干便捷访问（shaders()/textRenderer()/fileSystem()/resources()），
// 但与当前 Application 接口不完全一致（不存在 textRenderer()）。为避免接口不匹配，
// 暂不提供这些包装函数，仅保留 ui()/scene() 的懒加载实现与虚析构定义。

// UI便捷访问（懒加载）
UI::UIRenderer& Scene::ui() {
    if (!m_uiRenderer) {
        m_uiRenderer = Memory::MakeUnique<UI::UIRenderer>();
        // 不强依赖 TextRenderer，由各具体 Scene 自行管理文本渲染器
        if (!m_uiRenderer->initialize(app()->shaders(), &app()->textRenderer())) {
            TINA_ERROR("Scene::ui() - UIRenderer初始化失败");
        }
    }
    return *m_uiRenderer;
}

// Scene渲染器便捷访问（懒加载）
SceneRenderer& Scene::scene() {
    if (!m_sceneRenderer) {
        m_sceneRenderer = Memory::MakeUnique<SceneRenderer>();
        int w, h;
        app()->getPixelSize(w, h);
        m_sceneRenderer->initialize(app()->shaders(), w, h);
    }
    return *m_sceneRenderer;
}

} // namespace Tina::Engine
