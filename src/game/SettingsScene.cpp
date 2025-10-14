//
// SettingsScene 实现
//

#include "SettingsScene.hpp"
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"
#include "../engine/InputSystem.hpp"  // 添加 InputSystem 头文件
#include "../core/Log.hpp"

// #include <SDL3/SDL.h>  // 不再需要SDL，使用os封装
#include <bgfx/bgfx.h>
#include "../ui/UILayout.hpp"
#include "../ui/UIConstants.hpp"
#include "../ui/UIUtils.hpp"
#include "../engine/EventSystem.hpp"
#include "GameEvents.hpp"

namespace Tina::Game {

SettingsScene::SettingsScene() = default;
SettingsScene::~SettingsScene() = default;

void SettingsScene::onEnter()
{
    TINA_INFO("SettingsScene::onEnter - 进入设置页面");

    app()->getPixelSize(m_pixelWidth, m_pixelHeight);
    // 使用全局 TextRenderer（默认 32 号），避免场景内切换字号

    #if 0
    m_textRenderer = Memory::MakeUnique<UI::TextRenderer>();
    if (!m_textRenderer->initialize(app()->shaders(), app()->resources())) {
        TINA_ERROR("SettingsScene: TextRenderer 初始化失败");
    } else {
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 28);
    }
    #endif

    // 迁移到新架构：使用 Scene 基类提供的全局 UIRenderer（ui()）

    createUI();
    // 按钮事件已在 createUI() 中绑定
}

void SettingsScene::onExit()
{
    m_root.reset();
}

void SettingsScene::update(float dt)
{
    // 处理输入
    handleInput();
    if (m_root) m_root->update(dt);
}

void SettingsScene::render()
{
    // 1. 触摸 UI 视图并设置正交投影
    bgfx::touch(uiViewId());
    setupUIView(uiViewId(), m_pixelWidth, m_pixelHeight);

    // 2. 渲染半透明遮罩（使用 SceneRenderer 新架构）
    scene().drawOverlay(uiViewId(), Tina::Core::Color(0.0f, 0.0f, 0.0f, 0.5f));

    // 3. 渲染 UI
    if (m_root) {
        auto scope = ui().beginRender(uiViewId());
        m_root->render(uiViewId(), ui());
    }
}

// handleEvent 已删除，输入处理移至 update() 中使用 InputSystem

void SettingsScene::updateWindowSize(int width, int height)
{
    // 调用基类更新（触发防抖动）
    Scene::updateWindowSize(width, height);
}

// 覆盖实际应用窗口调整的方法
void SettingsScene::applyWindowResize(int width, int height)
{
    // 更新场景的窗口尺寸（重要！createUI需要使用这些值）
    m_pixelWidth = width;
    m_pixelHeight = height;

    // 调用基类的实际应用方法
    Scene::applyWindowResize(width, height);

    // 计算新的缩放比例
    float newScale = UI::UIUtils::calculateUIScale(width, height);

    // 如果缩放变化非常大，才重建UI（避免频繁重建导致事件系统崩溃）
    if (m_uiScale == 0.0f || std::abs(newScale - m_uiScale) > 0.3f) {
        m_uiScale = newScale;
        
        // 事件系统已统一为 Engine::EventSystem，无需清空本地路由
        
        createUI();
        TINA_INFO("SettingsScene: 重建UI，新缩放比例: {}", m_uiScale);
    } else {
        // 更新缩放但不重建（简单调整布局）
        m_uiScale = newScale;
        if (m_root) {
            m_root->setSize((float)width, (float)height);
            m_root->requestLayout();
            m_root->performLayoutNow();
        }
        TINA_INFO("SettingsScene: 更新布局，缩放比例: {}", m_uiScale);
    }
}

void SettingsScene::handleInput()
{
    // 使用 InputSystem 进行输入处理
    auto* appPtr = app();
    if (!appPtr) return;
    auto& input = appPtr->input();

    // ESC 键：返回
    if (input.isKeyPressed(Engine::KeyCode::Escape)) {
        onBack();
        return;
    }

    // 🔧 安全检查：只有在UI根节点有效时才处理鼠标事件
    if (!m_root) {
        return;
    }

    // 鼠标移动/点击/滚轮：统一推送到引擎事件系统进行命中与分发
    auto mousePos = input.getMousePosition();
    bool leftDown = input.isMouseButtonDown(Engine::MouseButton::Left);
    app()->events().setUIRoot(m_root.get()); // 确保根节点设置（UniquePtr -> raw）
    app()->events().updateUIInput(mousePos.x, mousePos.y, leftDown, input.getMouseWheelDelta());
}

void SettingsScene::createUI()
{
    m_root = Memory::MakeUnique<UI::UINode>("SettingsRoot");
    m_root->setSize((float)m_pixelWidth, (float)m_pixelHeight);

    // 根据窗口大小自适应缩放
    float scale = UI::UIUtils::calculateUIScale(m_pixelWidth, m_pixelHeight);

    // 使用与暂停页一致的"面板 + VBox"布局
    const float panelW = 600.0f * scale;
    const float pad    = 20.0f * scale;
    const float spacing= 16.0f * scale;
    const float btnH   = 50.0f * scale;
    const float titleH = 48.0f * scale;
    const float sectionH = 36.0f * scale;

    // 使用新的API：createChild
    auto* panel = m_root->createChild<UI::UIPanel>("SettingsPanel");
    panel->setColor(0.10f,0.10f,0.12f,0.92f);
    panel->setSize(panelW, 1.0f);
    panel->setHeightWrap();

    auto* vbox = panel->createChild<UI::UIVStack>("VBox");
    vbox->setSize(panelW, 0.0f);
    vbox->setHeightWrap();
    vbox->setPadding(pad, pad);
    vbox->setSpacing(spacing);

    // 主标题
    auto* title = vbox->createChild<UI::UILabel>();
    title->setText("游戏设置");
    title->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    title->setSize(panelW - pad*2, titleH);

    // 辅助函数：添加分组标题
    auto addSection = [&](const char* text) {
        auto* section = vbox->createChild<UI::UILabel>();
        section->setText(text);
        section->setAlignment(UI::UILabel::TextAlignH::Left, UI::UILabel::TextAlignV::Center);
        section->setSize(panelW - pad*2, sectionH);
        return section;
    };

    // 辅助函数：添加按钮行
    auto addRowButton = [&](const char* text){
        auto* row = vbox->createChild<UI::UIHStack>("Row");
        row->setSize(panelW - pad*2, btnH);
        row->setPadding(0,0);
        auto* btn = row->createChild<UI::UIButton>();
        btn->setText(text);
        btn->setWidthMatch();
        btn->setHeight(btnH);
        return btn;
    };

    // === 音频设置 ===
    addSection("音频设置");
    m_btnDay   = addRowButton("主音量: 100%");  // TODO: 实现音量调节
    m_btnNight = addRowButton("音效音量: 100%"); // TODO: 实现音效音量
    m_btnFwd   = addRowButton("音乐音量: 100%"); // TODO: 实现音乐音量

    // === 图形设置 ===
    addSection("图形设置");
    m_btnBack  = addRowButton("全屏模式: 关闭"); // TODO: 实现全屏切换

    // === 控制设置 ===
    addSection("控制设置");
    auto* btnControls = addRowButton("按键绑定..."); // TODO: 实现按键绑定界面
    btnControls->setOnClick([]{ 
        TINA_INFO("SettingsScene: 按键绑定功能待实现");
    });

    // 返回按钮
    m_btnClose = addRowButton("返回");
    m_btnClose->setNormalColor(0.3f, 0.3f, 0.35f, 0.95f);
    m_btnClose->setHoverColor(0.4f, 0.4f, 0.45f, 1.0f);
    m_btnClose->setPressedColor(0.2f, 0.2f, 0.25f, 1.0f);

    // 绑定点击（暂时使用占位功能）
    if (m_btnDay)   m_btnDay->setOnClick([]{ 
        TINA_INFO("SettingsScene: 主音量调节功能待实现");
    });
    if (m_btnNight) m_btnNight->setOnClick([]{ 
        TINA_INFO("SettingsScene: 音效音量调节功能待实现");
    });
    if (m_btnFwd)   m_btnFwd->setOnClick([]{ 
        TINA_INFO("SettingsScene: 音乐音量调节功能待实现");
    });
    if (m_btnBack)  m_btnBack->setOnClick([]{ 
        TINA_INFO("SettingsScene: 全屏切换功能待实现");
    });
    if (m_btnClose) m_btnClose->setOnClick([this]{ onBack(); });

    // 触发布局并居中
    vbox->requestLayout();
    panel->requestLayout();
    panel->performLayoutNow();

    // 获取实际高度并居中
    float panelHeight = panel->getSize().y;
    float centerX = (m_pixelWidth - panelW) * 0.5f;
    float centerY = (m_pixelHeight - panelHeight) * 0.5f;
    panel->setPosition(centerX, centerY);

    // 根节点供引擎事件系统使用
    if (auto* a = app()) a->events().setUIRoot(m_root.get());
}

void SettingsScene::onBack()
{
    app()->scenes().requestPop();
}

// 这些方法已移除，设置界面不再包含昼夜调试功能
// 昼夜调试功能保留在 PauseScene 中

// 无需集中路由函数，事件由路由器直接回调

} // namespace Tina::Game
