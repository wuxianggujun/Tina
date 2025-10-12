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
    m_events.setRoot(m_root.get());

    // 订阅 UI 按钮点击事件
    m_btnClickToken = app()->events().subscribe<UI::ButtonClickEvent>(this, &SettingsScene::onUIButtonClicked);
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
    // 触摸 UI 视图
    bgfx::touch(UI::VIEW_UI);
    if (m_root) {
        auto scope = ui().beginRender(UI::VIEW_UI);
        m_root->render(UI::VIEW_UI, ui());
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

    // 重新创建 UI 以适应新的窗口大小
    createUI();

    TINA_INFO("SettingsScene: 窗口大小更新为 {}x{}", width, height);
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

    // 鼠标移动/点击：使用 UIEventSystem 统一处理
    auto mousePos = input.getMousePosition();
    bool leftDown = input.isMouseButtonDown(Engine::MouseButton::Left);
    m_events.updateMouse(mousePos.x, mousePos.y, leftDown);
    m_events.processEvents();
}

void SettingsScene::createUI()
{
    m_root = Memory::MakeUnique<UI::UINode>("SettingsRoot");
    m_root->setSize((float)m_pixelWidth, (float)m_pixelHeight);

    // 根据窗口大小自适应缩放
    float scale = UI::UIUtils::calculateUIScale(m_pixelWidth, m_pixelHeight);

    // 使用与暂停页一致的"面板 + VBox"布局
    const float panelW = 520.0f * scale;
    const float pad    = 16.0f * scale;
    const float spacing= 12.0f * scale;
    const float btnH   = 50.0f * scale;
    const float titleH = 44.0f * scale;

    // 使用新的API：createChild
    auto* panel = m_root->createChild<UI::UIPanel>("SettingsPanel");
    panel->setColor(0.10f,0.10f,0.12f,0.90f);
    panel->setSize(panelW, 1.0f);
    panel->setHeightWrap();

    auto* vbox = panel->createChild<UI::UIVStack>("VBox");
    vbox->setSize(panelW, 0.0f);
    vbox->setHeightWrap();
    vbox->setPadding(pad, pad);
    vbox->setSpacing(spacing);

    auto* title = vbox->createChild<UI::UILabel>();
    title->setText("设置 / 调试");
    title->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    title->setSize(panelW - pad*2, titleH);

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

    m_btnDay = addRowButton("切换到白天");
    m_btnDay->setButtonId(BTN_DAY);
    m_btnDay->setEventSystem(&app()->events());
    
    m_btnNight = addRowButton("切换到黑夜");
    m_btnNight->setButtonId(BTN_NIGHT);
    m_btnNight->setEventSystem(&app()->events());
    
    m_btnFwd = addRowButton("时间 +10%");
    m_btnFwd->setButtonId(BTN_FWD);
    m_btnFwd->setEventSystem(&app()->events());
    
    m_btnBack = addRowButton("时间 -10%");
    m_btnBack->setButtonId(BTN_BACK);
    m_btnBack->setEventSystem(&app()->events());
    
    m_btnClose = addRowButton("返回");
    m_btnClose->setButtonId(BTN_CLOSE);
    m_btnClose->setEventSystem(&app()->events());

    // 触发布局并居中
    // 使用performLayoutNow()确保布局立即完成
    vbox->requestLayout();
    panel->requestLayout();
    panel->performLayoutNow();  // 强制立即执行布局

    // 获取实际高度并居中
    float panelHeight = panel->getSize().y;
    float centerX = (m_pixelWidth - panelW) * 0.5f;
    float centerY = (m_pixelHeight - panelHeight) * 0.5f;
    panel->setPosition(centerX, centerY);
}

void SettingsScene::onBack()
{
    app()->scenes().requestPop();
}

void SettingsScene::onSetDay()
{
    // 设定至白天中段（默认 0.25）
    Tina::Game::Events::SetDayNight event;
    event.normalized = 0.25f;
    app()->events().trigger(event);
}

void SettingsScene::onSetNight()
{
    // 设定至黑夜中段（默认 0.75）
    Tina::Game::Events::SetDayNight event;
    event.normalized = 0.75f;
    app()->events().trigger(event);
}

void SettingsScene::onFwdTime()
{
    Tina::Game::Events::AdjustDayNight event;
    event.delta = +0.10f;
    app()->events().trigger(event);
}

void SettingsScene::onBackTime()
{
    Tina::Game::Events::AdjustDayNight event;
    event.delta = -0.10f;
    app()->events().trigger(event);
}

void SettingsScene::onUIButtonClicked(const UI::ButtonClickEvent& e)
{
    switch (e.buttonId) {
        case BTN_DAY:   onSetDay();  break;
        case BTN_NIGHT: onSetNight(); break;
        case BTN_FWD:   onFwdTime();  break;
        case BTN_BACK:  onBackTime(); break;
        case BTN_CLOSE: onBack();     break;
        default: break;
    }
}

} // namespace Tina::Game
