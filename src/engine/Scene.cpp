//
// Scene 实现 - 便捷资源访问方法
//

#include "Scene.hpp"
#include "Application.hpp"
#include "InputSystem.hpp"
#include "../ui/UICore.hpp"
#include "../ui/UINode.hpp"  // 引入UINode完整定义
#include "../renderer/RenderQueue.hpp"
#include "../core/Log.hpp"
#include "SceneRenderer.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <algorithm>  // for std::find, std::remove

namespace Tina::Engine {

// 构造函数定义（必须在此处，RenderQueue是完整类型）
Scene::Scene() {
    // Scene默认创建一个2D相机
    m_camera = Memory::MakeUnique<Camera2D>();
    // 设置默认视口（会在窗口大小更新时自动调整）
    m_camera->setViewportPixels(m_pixelWidth, m_pixelHeight);
    m_camera->setViewHeightWorld(20.0f);  // 默认视野高度20个单位
}

// 虚析构函数定义（必须在此处，RenderQueue是完整类型）
Scene::~Scene() = default;

// 访问输入系统
InputSystem* Scene::input() const {
    return m_app ? &m_app->input() : nullptr;
}

// UI根节点管理
void Scene::addUIRoot(UI::UINode* root) {
    if (root && std::find(m_uiRoots.begin(), m_uiRoots.end(), root) == m_uiRoots.end()) {
        m_uiRoots.push_back(root);
    }
}

void Scene::removeUIRoot(UI::UINode* root) {
    if (root) {
        m_uiRoots.erase(
            std::remove(m_uiRoots.begin(), m_uiRoots.end(), root),
            m_uiRoots.end()
        );
    }
}

// 更新窗口尺寸
void Scene::updateWindowSize(int width, int height) {
    m_pixelWidth = width;
    m_pixelHeight = height;
    m_viewDirty = true;

    // 更新SceneRenderer的屏幕尺寸（如果已初始化）
    if (m_sceneRenderer) {
        m_sceneRenderer->setScreenSize(width, height);
    }

    // 更新相机视口
    if (m_camera) {
        m_camera->setViewportPixels(width, height);
    }

    // 自动通知所有UI根节点（框架自动处理，递归通知整个UI树）
    for (auto* root : m_uiRoots) {
        if (root) {
            root->onWindowSizeChanged(width, height);
        }
    }

    // 自动调用子类的窗口大小改变回调（可选，用于特殊逻辑）
    onWindowSizeChanged(width, height);
}

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
    // 使用场景的相机生成视图和投影矩阵
    if (m_camera) {
        float viewMatrix[16], projMatrix[16];
        m_camera->buildViewProj(viewMatrix, projMatrix);
        bgfx::setViewTransform(viewId, viewMatrix, projMatrix);
    } else {
        // 备用：如果没有相机，使用单位矩阵
        float identity[16];
        bx::mtxIdentity(identity);
        bgfx::setViewTransform(viewId, identity, identity);
    }
}

} // namespace Tina::Engine
