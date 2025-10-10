//
// PauseScene 实现
//

#include "PauseScene.hpp"
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"
#include "../ui/UILayout.hpp"
#include "../core/Log.hpp"
#include "../ui/UIComponents.hpp"
#include "../renderer/Primitive2D.hpp"
#include "MenuScene.hpp"
#include "../engine/TypedEventBus.hpp"
#include "GameEvents.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include "../ui/UIConstants.hpp"

namespace Tina::Game {

PauseScene::PauseScene() = default;
PauseScene::~PauseScene() = default;

void PauseScene::onEnter()
{
    TINA_INFO("PauseScene::onEnter - 进入暂停菜单");

    // 获取窗口尺寸
    app()->getPixelSize(m_pixelWidth, m_pixelHeight);

    // 使用全局 TextRenderer（默认 32 号），无需在场景内切换字号

    // 文本渲染器
    #if 0
    m_textRenderer = Memory::MakeUnique<UI::TextRenderer>();
    if (!m_textRenderer->initialize(app()->shaders(), app()->resources())) {
        TINA_ERROR("TextRenderer 初始化失败");
    } else {
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 32);
    }
    #endif

    // ✅ 使用 Scene 基类提供的 ui() 方法
    // m_uiRenderer = Memory::MakeUnique<UI::UIRenderer>();
    // m_uiRenderer->initialize(app()->shaders(), &app()->textRenderer());

    // 顶点布局/着色器由 Application 的 Primitive2D 统一管理

    // 创建 UI
    createUI();
    // 绑定事件系统根节点，统一处理 hover/click
    m_events.setRoot(m_rootNode.get());

    TINA_INFO("PauseScene: 初始化完成");
}

void PauseScene::onExit()
{
    TINA_INFO("PauseScene::onExit - 退出暂停菜单");

    // 断开 Signal 连接
    m_continueConnection.disconnect();
    m_quitConnection.disconnect();
    m_cDay.disconnect();
    m_cNight.disconnect();
    m_cFwd.disconnect();
    m_cBack.disconnect();

    // 清理 UI（必须在渲染资源之前清理）
    m_rootNode.reset();
    m_btnContinue = nullptr;
    m_btnQuit = nullptr;

    // 清理渲染资源由全局 Renderer 管理
}

void PauseScene::update(float dt)
{
    if (m_rootNode) {
        m_rootNode->update(dt);
    }
}

void PauseScene::render()
{
    // 1. 确保 view 被清理（不清屏，只是触摸）
    bgfx::touch(uiViewId());

    // 2. 渲染半透明遮罩
    renderOverlay();

    // 3. ✅ 使用 Scene 基类的 ui() 方法和 RAII 作用域
    if (m_rootNode) {
        auto scope = ui().beginRender(uiViewId());
        m_rootNode->render(uiViewId(), ui());
    }
}

void PauseScene::handleEvent(const Tina::os::Event& event)
{
    using E = Tina::os::Event;

    // ESC 键：返回游戏
    if (event.type == E::Type::KEY && event.key.down) {
        if (event.key.key_code == os::KeyCode::ESCAPE) {
            onContinueClicked();
            return;
        }
    }

    // 鼠标移动：更新按钮悬停状态
    // 使用 UIEventSystem 统一处理 hover/click
    if (event.type == E::Type::MOUSE_MOVE || event.type == E::Type::MOUSE_BUTTON) {
        float mx = 0.0f, my = 0.0f;
        SDL_GetMouseState(&mx, &my);
        bool leftDown = false;
        if (event.type == E::Type::MOUSE_BUTTON) {
            leftDown = (event.mouse_button.button == os::MouseButton::LEFT) && event.mouse_button.down;
        } else {
            uint32_t mask = (uint32_t)SDL_GetMouseState(nullptr, nullptr);
#ifdef SDL_BUTTON_MASK
            leftDown = (mask & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
#else
            leftDown = (mask & SDL_BUTTON_LMASK) != 0;
#endif
        }
        m_events.updateMouse(mx, my, leftDown);
        m_events.processEvents();
    }

    // 统一通过 UIEventSystem 处理鼠标悬停与点击，移除手动 containsPoint 调试代码

    // 窗口调整大小
    if (event.type == E::Type::WINDOW_SIZE) {
        m_pixelWidth = event.win_size.w;
        m_pixelHeight = event.win_size.h;
        
        // ✅ 标记视图为脏（如果需要）
        // m_viewDirty = true;
        
        // 重新布局 UI
        if (m_rootNode) {
            createUI();  // 简单重建
        }
    }
}

void PauseScene::createUI()
{
    // 若重复创建（例如窗口尺寸变化），先断开旧连接并释放旧 UI
    m_continueConnection.disconnect();
    m_quitConnection.disconnect();
    m_rootNode.reset();
    m_btnContinue = nullptr;
    m_btnQuit = nullptr;

    // 创建根节点
    m_rootNode = Memory::MakeUnique<UI::UINode>();
    m_rootNode->setPosition(0, 0);
    m_rootNode->setSize((float)m_pixelWidth, (float)m_pixelHeight);

    // 顶层不再放置标题，标题移入面板内部

    // 使用布局：居中一个面板，内部垂直栈+若干水平栈，避免纵向堆叠
    const float panelW   = 680.0f;
    const float btnH     = 56.0f;
    const float pad      = 16.0f;   // VBox 内边距（左右/上下）
    const float spacing  = 12.0f;   // VBox 子项间距
    const float titleH   = 44.0f;   // 标题高度
    const float debugH   = 36.0f;   // 分组标题高度
    const float rowW     = panelW - pad * 2.0f;               // VBox 左右 padding 共 32
    const float colW     = (rowW - 8.0f * 2.0f - 12.0f) * 0.5f; // 行左右 padding=8，列间距=12

    // 使用新的API：createChild
    auto* panel = m_rootNode->createChild<UI::UIPanel>("PausePanel");
    panel->setColor(0.08f, 0.08f, 0.10f, 0.92f);
    panel->setSize(panelW, 1.0f);
    panel->setHeightWrap(); // 交由容器包裹

    auto* vbox = panel->createChild<UI::UIVStack>("VBox");
    vbox->setSize(panelW, 0.0f);
    vbox->setHeightWrap(); // 容器自动包裹内容高度
    vbox->setPadding(pad, pad);
    vbox->setSpacing(spacing);

    // 标题
    auto* title = vbox->createChild<UI::UILabel>();
    title->setText("游戏已暂停");
    title->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    title->setSize(panelW - 32.0f, 44.0f);

    // 第一行：继续
    auto* rowTop = vbox->createChild<UI::UIHStack>("RowTop");
    rowTop->setSize(rowW, btnH);
    rowTop->setSpacing(12.0f);
    rowTop->setPadding(0.0f, 0.0f); // 顶部占满行，无左右内边距，避免右侧溢出

    m_btnContinue = rowTop->createChild<UI::UIButton>();
    m_btnContinue->setText("继续游戏 (ESC)");
    m_btnContinue->setSize(rowW, btnH);
    m_btnContinue->setNormalColor(0.2f, 0.6f, 0.2f, 0.95f);
    m_btnContinue->setHoverColor(0.3f, 0.8f, 0.3f, 1.0f);
    m_btnContinue->setPressedColor(0.1f, 0.4f, 0.1f, 1.0f);
    m_continueConnection = m_btnContinue->onClick.connect(this, &PauseScene::onContinueClicked);

    // 设置按钮已移除，保留单行继续按钮

    // 分组标题：调试 / 昼夜
    auto* dbg = vbox->createChild<UI::UILabel>();
    dbg->setText("调试 / 昼夜");
    dbg->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    dbg->setSize(rowW, debugH);

    // 两行两列的调试网格
    auto makeRow = [&](UI::UIButton*& a, const char* ta, UI::UIButton*& b, const char* tb){
        auto* row = vbox->createChild<UI::UIHStack>("Row");
        row->setSize(rowW, btnH);
        row->setSpacing(12.0f);
        row->setPadding(8.0f, 8.0f); // 与 colW 计算一致的左右内边距
        a = row->createChild<UI::UIButton>();
        a->setText(ta);
        a->setSize(colW, btnH);
        b = row->createChild<UI::UIButton>();
        b->setText(tb);
        b->setSize(colW, btnH);
        return row;
    };

    makeRow(m_btnDay,    "切换到白天",   m_btnNight,   "切换到黑夜");
    makeRow(m_btnFwd,    "时间 +10%",    m_btnBack,    "时间 -10%");

    // 绑定点击（使用便捷重载：对象指针 + 成员函数）
    m_cDay      = m_btnDay->onClick.connect(this, &PauseScene::onSetDay);
    m_cNight    = m_btnNight->onClick.connect(this, &PauseScene::onSetNight);
    m_cFwd      = m_btnFwd->onClick.connect(this, &PauseScene::onFwdTime);
    m_cBack     = m_btnBack->onClick.connect(this, &PauseScene::onBackTime);

    // 退出按钮独占一行
    auto* rowBottom = vbox->createChild<UI::UIHStack>("RowBottom");
    rowBottom->setSize(rowW, btnH);
    rowBottom->setSpacing(12.0f);
    rowBottom->setPadding(0.0f, 0.0f); // 占满行，无左右内边距

    m_btnQuit = rowBottom->createChild<UI::UIButton>();
    m_btnQuit->setText("返回主菜单");
    m_btnQuit->setWidthMatch();
    m_btnQuit->setHeight(btnH);
    m_btnQuit->setNormalColor(0.6f, 0.2f, 0.2f, 0.95f);
    m_btnQuit->setHoverColor(0.8f, 0.3f, 0.3f, 1.0f);
    m_btnQuit->setPressedColor(0.4f, 0.1f, 0.1f, 1.0f);
    m_quitConnection = m_btnQuit->onClick.connect(this, &PauseScene::onQuitClicked);

    // 触发布局计算并回填 Panel 高度，再居中 Panel
    vbox->update(0.0f);
    panel->update(0.0f);
    panel->setPosition((m_pixelWidth - panelW) * 0.5f, (m_pixelHeight - panel->getSize().y) * 0.5f);

    // 更新事件系统根节点，保证重建后事件命中正确
    m_events.setRoot(m_rootNode.get());

    TINA_INFO("PauseScene: UI 创建完成");
}

void PauseScene::renderOverlay()
{
    // 渲染半透明黑色遮罩（覆盖整个屏幕）。使用 UI 层视图
    bgfx::setViewRect(UI::VIEW_UI, 0, 0, (uint16_t)m_pixelWidth, (uint16_t)m_pixelHeight);
    // 由全局 Primitive2D 设置正交并绘制半透明全屏遮罩
    app()->primitives2D().setOrtho(UI::VIEW_UI, (float)m_pixelWidth, (float)m_pixelHeight);
    // 半透明黑色: ABGR 0x80000000
    app()->primitives2D().drawFullscreen(UI::VIEW_UI, (float)m_pixelWidth, (float)m_pixelHeight, 0x80000000u, true);
}

void PauseScene::onContinueClicked()
{
    TINA_INFO("PauseScene: 继续游戏按钮被点击");
    // 使用延迟场景操作，避免在回调栈内修改场景栈
    app()->scenes().requestPop();  // 返回 GameScene
}

void PauseScene::onQuitClicked()
{
    TINA_INFO("PauseScene: 返回主菜单按钮被点击");
    // 清空所有场景后，压入主菜单
    // 使用延迟场景操作，避免在回调栈内修改场景栈
    app()->scenes().requestClear();
    app()->scenes().requestPush(Memory::MakeUnique<MenuScene>());
}

void PauseScene::onSetDay()      { app()->events().trigger<Tina::Game::Events::SetDayNight>(0.25f); }
void PauseScene::onSetNight()    { app()->events().trigger<Tina::Game::Events::SetDayNight>(0.75f); }
// 已移除“暂停/恢复昼夜”功能
void PauseScene::onFwdTime()     { app()->events().trigger<Tina::Game::Events::AdjustDayNight>(+0.10f); }
void PauseScene::onBackTime()    { app()->events().trigger<Tina::Game::Events::AdjustDayNight>(-0.10f); }

} // namespace Tina::Game

