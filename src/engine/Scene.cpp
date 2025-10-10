//
// Scene 实现 - 便捷资源访问方法
//

#include "Scene.hpp"
#include "Application.hpp"
#include "../ui/UICore.hpp"
#include "../renderer/RenderQueue.hpp"
#include "../core/Log.hpp"
#include "SceneRenderer.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace Tina::Engine {

// 构造函数定义（必须在此处，RenderQueue是完整类型）
Scene::Scene() = default;

// 虚析构函数定义（必须在此处，RenderQueue是完整类型）
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

// ✅ 设置UI正交视图（避免每帧重复计算）
void Scene::setupUIView(uint16_t viewId, int width, int height) {
    bgfx::setViewRect(viewId, 0, 0, (uint16_t)width, (uint16_t)height);

    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(ortho, 0.0f, (float)width, (float)height, 0.0f,
                 -1.0f, 1.0f, 0.0f, caps ? caps->homogeneousDepth : false);
    bgfx::setViewTransform(viewId, nullptr, ortho);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
}

// 渲染队列便捷访问（懒加载）
Renderer::RenderQueue& Scene::queue() {
    if (!m_renderQueue) {
        m_renderQueue = Memory::MakeUnique<Renderer::RenderQueue>();
        if (!m_renderQueue->initialize(&app()->shaders())) {
            TINA_ERROR("Scene::queue() - RenderQueue初始化失败");
        }
    }
    return *m_renderQueue;
}

// ==================== 新增：框架渲染方法 ====================

// 框架渲染入口（由Application调用）
void Scene::renderFrame() {
    // 1. 准备视图（设置视图矩阵、投影矩阵、清屏等）
    prepareViews();

    // 2. 调用子类的渲染实现
    render();

    // 3. 完成视图（touch所有视图）
    finalizeViews();
}

// 准备视图（自动设置所有配置的视图）
void Scene::prepareViews() {
    // 仅在视图需要更新或首次渲染时重新配置
    if (m_viewDirty || m_viewSetup.empty()) {
        // 获取子类的视图配置
        m_viewSetup = getViewSetup();

        // 设置每个视图
        for (const auto& view : m_viewSetup) {
            // 设置视图矩形（所有视图都使用全屏）
            bgfx::setViewRect(view.id, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);

            // 根据类型设置视图变换
            switch (view.type) {
            case ViewSetup::World3D:
                setupWorldView(view.id);
                break;

            case ViewSetup::UI2D:
            case ViewSetup::Background2D:
                // 使用已有的setupUIView方法
                setupUIView(view.id, m_pixelWidth, m_pixelHeight);
                break;
            }

            // 如果需要清屏，设置清屏参数
            if (view.needsClear) {
                bgfx::setViewClear(view.id, view.clearFlags, view.clearColor, 1.0f, 0);
            }
        }

        m_viewDirty = false;
    }
}

// 完成视图（自动touch所有配置的视图）
void Scene::finalizeViews() {
    // Touch所有配置的视图，确保它们被渲染
    for (const auto& view : m_viewSetup) {
        bgfx::touch(view.id);
    }
}

// 设置3D世界视图
void Scene::setupWorldView(uint16_t viewId) {
    // 使用默认的单位矩阵，子类应该在render()中自己设置视图矩阵
    // 这里只是确保视图有一个有效的变换矩阵
    float identity[16];
    bx::mtxIdentity(identity);
    bgfx::setViewTransform(viewId, identity, identity);
}

// 框架事件处理（先处理通用事件，再调用子类）
void Scene::handleEventFrame(const Tina::os::Event& event) {
    using E = Tina::os::Event;

    // 框架自动处理窗口大小变化
    if (event.type == E::Type::WINDOW_SIZE) {
        m_pixelWidth = event.win_size.w;
        m_pixelHeight = event.win_size.h;
        m_viewDirty = true;

        TINA_INFO("Scene窗口大小更新: {}x{}", m_pixelWidth, m_pixelHeight);
    }

    // 调用子类的事件处理
    handleEvent(event);
}

} // namespace Tina::Engine
