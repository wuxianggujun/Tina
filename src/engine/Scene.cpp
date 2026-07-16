//
// Scene 实现 - 便捷资源访问方法
//

#include "Scene.hpp"
#include "Application.hpp"
#include "InputSystem.hpp"
#include "EventSystem.hpp"  // ✅ 需要EventSystem完整定义以调用subscribe/unsubscribe
#include "../ui/UICore.hpp"
#include "../ui/UINode.hpp"  // 引入UINode完整定义
#include "../ui/UILayoutManager.hpp"  // 引入UILayoutManager完整定义
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
    
    // 创建UI布局管理器（每个Scene拥有自己的实例）
    m_uiLayoutManager = Memory::MakeUnique<UI::UILayoutManager>();
}

// 虚析构函数定义（必须在此处，RenderQueue是完整类型）
Scene::~Scene() {
    clearUIRoots();
    
    // 现在可以安全地销毁布局管理器和其他资源
    // （UniquePtr会自动按声明逆序析构）
}

// 访问输入系统
InputSystem* Scene::input() const {
    return m_app ? &m_app->input() : nullptr;
}

// UI根节点管理
void Scene::addUIRoot(UI::UINode* root) {
    if (root && std::find(m_uiRoots.begin(), m_uiRoots.end(), root) == m_uiRoots.end()) {
        m_uiRoots.push_back(root);
        
        // 为整个UI树设置布局管理器并注册
        registerUITreeToLayoutManager(root);
    }
}

void Scene::removeUIRoot(UI::UINode* root) {
    if (root) {
        // 从布局管理器注销整个UI树
        unregisterUITreeFromLayoutManager(root);
        
        m_uiRoots.erase(
            std::remove(m_uiRoots.begin(), m_uiRoots.end(), root),
            m_uiRoots.end()
        );
    }
}

void Scene::clearUIRoots() {
    for (auto* root : m_uiRoots) {
        if (root) unregisterUITreeFromLayoutManager(root);
    }
    m_uiRoots.clear();
}

// 更新窗口尺寸（简化版：直接应用，不防抖）
// 注意：正常情况下通过WindowResizedEvent事件触发
// 这个方法主要用于SceneManager在场景初始化时同步窗口尺寸
void Scene::updateWindowSize(int width, int height) {
    applyWindowResize(width, height);
}

// 实际应用窗口尺寸更新
void Scene::applyWindowResize(int width, int height) {
    m_pixelWidth = width;
    m_pixelHeight = height;
    m_viewDirty = true;

    // 更新渲染器和相机
    if (m_sceneRenderer) {
        m_sceneRenderer->setScreenSize(width, height);
    }
    if (m_camera) {
        m_camera->setViewportPixels(width, height);
    }

    // 通知所有UI根节点
    for (auto* root : m_uiRoots) {
        if (root) {
            root->setSize((float)width, (float)height);
            root->onWindowSizeChanged(width, height);
        }
    }

    // 调用子类回调
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

// ==================== 新增：框架方法 ====================

// 框架更新入口
void Scene::updateFrame(float dt) {
    // 1. 子类更新游戏/页面状态；其中产生的布局请求在本帧统一提交。
    update(dt);

    // 2. 更新UI节点动画和状态，不隐式执行布局。
    for (auto* root : m_uiRoots) {
        if (root) {
            root->update(dt);
        }
    }

    // 3. 每帧至多执行一次批量布局。
    if (m_uiLayoutManager) {
        m_uiLayoutManager->performPendingLayouts();
    }

    // 4. 布局完成后只做一次命中测试与 routed event 分发。
    if (m_app && m_app->getEventSystem()) {
        m_app->events().setUIRoots(m_uiRoots);
        if (InputSystem* inputSystem = input()) {
            const auto mouse = inputSystem->getMousePosition();
            m_app->events().updateUIInputLogical(
                mouse.x,
                mouse.y,
                inputSystem->isMouseButtonDown(MouseButton::Left),
                inputSystem->getMouseWheelDelta());
        }
    }
}

// 框架渲染入口（由Application调用）
void Scene::renderFrame() {
    // 1. 准备视图（设置视图矩阵、投影矩阵、清屏等）
    prepareViews();

    // 2. 调用子类的渲染实现
    render();

    // 3. 完成视图（touch所有视图）
    finalizeViews();
}

// ✅ 事件驱动：设置事件监听器
void Scene::setupEventHandlers() {
    if (!m_app || !m_app->getEventSystem()) return;
    
    // 订阅窗口resize事件
    m_windowResizeToken = m_app->getEventSystem()->subscribe<Events::WindowResizedEvent>(
        [this](const Events::WindowResizedEvent& e) {
            // ✅ 直接应用窗口尺寸变化
            // Application已调用bgfx::reset，这里立即更新Scene状态
            applyWindowResize(e.width, e.height);
            TINA_DEBUG("Scene收到WindowResizedEvent: {}x{}, 已应用", e.width, e.height);
        }
    );
}

// ✅ 事件驱动：清理事件监听器
void Scene::cleanupEventHandlers() {
    // SubscriptionToken是RAII的，析构时自动取消订阅
    // 但我们可以手动调用unsubscribe以立即清理
    m_windowResizeToken.unsubscribe();
}

// 准备视图（自动设置所有配置的视图）
void Scene::prepareViews() {
    // 如果还没有视图配置，获取配置
    if (m_viewSetup.empty()) {
        m_viewSetup = getViewSetup();
        TINA_DEBUG("Scene::prepareViews - 初始化视图配置，视图数量: {}", m_viewSetup.size());
    }

    // ✅ 简单实现：直接使用m_pixelWidth/Height，不使用bgfx::getStats()
    if (m_viewDirty) {
        TINA_DEBUG("Scene::prepareViews - 更新视图: {}x{}, 视图数量: {}", 
                  m_pixelWidth, m_pixelHeight, m_viewSetup.size());

        for (const auto& view : m_viewSetup) {
            bgfx::setViewRect(view.id, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);

            if (view.type == ViewSetup::UI2D || view.type == ViewSetup::Background2D) {
                setupUIView(view.id, m_pixelWidth, m_pixelHeight);
            }

            if (view.needsClear) {
                bgfx::setViewClear(view.id, view.clearFlags, view.clearColor, 1.0f, 0);
                TINA_INFO("Scene::prepareViews - View {} 设置清屏: color=0x{:08x}, flags={}", 
                         view.id, view.clearColor, view.clearFlags);
            } else {
                // ✅ 关键修复：不清屏的view也需要重置clear标志，否则可能保留之前场景的清屏设置
                bgfx::setViewClear(view.id, BGFX_CLEAR_NONE);
                TINA_INFO("Scene::prepareViews - View {} 禁用清屏", view.id);
            }
        }
        m_viewDirty = false;
    }

    // 相机变换需要每帧更新（相机可能在update中移动）
    for (const auto& view : m_viewSetup) {
        if (view.type == ViewSetup::World3D) {
            setupWorldView(view.id);
        }
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

// 递归注册UI树到布局管理器
void Scene::registerUITreeToLayoutManager(UI::UINode* node) {
    if (!node || !m_uiLayoutManager) return;

    // UINode 会递归注册整棵树，后续动态添加的子节点也会继承上下文。
    node->setLayoutManager(m_uiLayoutManager.get());
    node->setEventSystem(m_app ? m_app->getEventSystem() : nullptr);
}

// 递归注销UI树从布局管理器
void Scene::unregisterUITreeFromLayoutManager(UI::UINode* node) {
    if (!node || !m_uiLayoutManager) return;

    node->setEventSystem(nullptr);
    node->setLayoutManager(nullptr);
}

} // namespace Tina::Engine
