//
// Scene 实现 - 便捷资源访问方法
//

#include "Scene.hpp"
#include "Application.hpp"
#include "InputSystem.hpp"
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
    // ⚠️ 重要：必须在布局管理器销毁前清理所有UI根节点
    // 否则UINode析构时会访问已销毁的布局管理器
    for (auto* root : m_uiRoots) {
        if (root) {
            unregisterUITreeFromLayoutManager(root);
        }
    }
    m_uiRoots.clear();
    
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

// 更新窗口尺寸（智能防抖动）
void Scene::updateWindowSize(int width, int height) {
    // 计算变化幅度
    int deltaW = std::abs(width - m_pixelWidth);
    int deltaH = std::abs(height - m_pixelHeight);

    // 如果是大幅度变化（如最大化/最小化），立即应用
    // 阈值：宽或高变化超过30%，或绝对值超过300像素
    bool isLargeChange = (deltaW > m_pixelWidth * 0.3f || deltaH > m_pixelHeight * 0.3f ||
                          deltaW > 300 || deltaH > 300);

    if (isLargeChange) {
        // 大幅度变化，立即应用，避免卡顿
        TINA_INFO("窗口大幅变化: {}x{} -> {}x{}，立即应用",
                  m_pixelWidth, m_pixelHeight, width, height);
        applyWindowResize(width, height);
        m_pendingResize = false;
        m_resizeTimer = 0.0f;
    } else {
        // 小幅度变化，使用防抖动（降低延迟到30ms）
        m_pendingResize = true;
        m_pendingWidth = width;
        m_pendingHeight = height;
        m_resizeTimer = 0.03f;  // 30ms 延迟，更快响应
    }
}

// 实际应用窗口尺寸更新
void Scene::applyWindowResize(int width, int height) {
    TINA_DEBUG("Scene::applyWindowResize - 更新窗口尺寸: {}x{} -> {}x{}",
              m_pixelWidth, m_pixelHeight, width, height);
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

// ==================== 新增：框架方法 ====================

// 框架更新入口（自动处理防抖动）
void Scene::updateFrame(float dt) {
    // 1. 处理防抖动的窗口调整
    if (m_pendingResize && m_resizeTimer > 0) {
        m_resizeTimer -= dt;
        if (m_resizeTimer <= 0) {
            // 时间到，应用窗口调整
            applyWindowResize(m_pendingWidth, m_pendingHeight);
            m_pendingResize = false;
        }
    }

    // 2. 自动更新所有UI根节点（让Scene不需要手动调用）
    for (auto* root : m_uiRoots) {
        if (root) {
            root->update(dt);
        }
    }

    // 3. 调用子类的更新逻辑
    update(dt);
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

// 准备视图（自动设置所有配置的视图）
void Scene::prepareViews() {
    // 如果还没有视图配置，或标记为需要更新，则刷新基本的视图矩形/清屏等静态参数
    if (m_viewSetup.empty()) {
        m_viewSetup = getViewSetup();
        m_viewDirty = true;
    }

    if (m_viewDirty) {
        for (const auto& view : m_viewSetup) {
            // 设置视图矩形（所有视图都使用全屏）
            bgfx::setViewRect(view.id, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);

            // UI/背景视图的正交投影在尺寸变化时更新即可
            if (view.type == ViewSetup::UI2D || view.type == ViewSetup::Background2D) {
                setupUIView(view.id, m_pixelWidth, m_pixelHeight);
            }

            // 如果需要清屏，设置清屏参数
            if (view.needsClear) {
                bgfx::setViewClear(view.id, view.clearFlags, view.clearColor, 1.0f, 0);
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
    
    // 设置布局管理器指针
    node->setLayoutManager(m_uiLayoutManager.get());
    
    // 注册节点
    m_uiLayoutManager->registerNode(node);
    
    // 递归注册所有子节点
    for (size_t i = 0; i < node->getChildCount(); ++i) {
        if (auto* child = node->getChild(i)) {
            registerUITreeToLayoutManager(child);
        }
    }
}

// 递归注销UI树从布局管理器
void Scene::unregisterUITreeFromLayoutManager(UI::UINode* node) {
    if (!node || !m_uiLayoutManager) return;
    
    // 递归注销所有子节点
    for (size_t i = 0; i < node->getChildCount(); ++i) {
        if (auto* child = node->getChild(i)) {
            unregisterUITreeFromLayoutManager(child);
        }
    }
    
    // 注销节点
    m_uiLayoutManager->unregisterNode(node);
    
    // 清除布局管理器指针
    node->setLayoutManager(nullptr);
}

} // namespace Tina::Engine
