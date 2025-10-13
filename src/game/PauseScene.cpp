//
// PauseScene 实现
//

#include "PauseScene.hpp"
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"
#include "../engine/InputSystem.hpp"  // 添加 InputSystem 头文件
#include "../ui/UILayout.hpp"
#include "../core/Log.hpp"
#include "../ui/UIComponents.hpp"
#include "../ui/UIUtils.hpp"
#include "MenuScene.hpp"
#include "../engine/EventSystem.hpp"
#include "GameEvents.hpp"

// #include <SDL3/SDL.h>  // 不再需要：使用 InputSystem
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

    // m_uiRenderer = Memory::MakeUnique<UI::UIRenderer>();
    // m_uiRenderer->initialize(app()->shaders(), &app()->textRenderer());

    // 顶点布局/着色器由 Application 的 Primitive2D 统一管理

    // 创建 UI
    createUI();
    
    // 设置UI根节点到事件系统（用于命中测试）
    app()->events().setUIRoot(m_rootNode.get());
    
    // 绑定按钮回调（EventSystem 已自动连接）
    if (m_btnContinue) m_btnContinue->setOnClick([this]{ onContinueClicked(); });
    if (m_btnDay)      m_btnDay->setOnClick([this]{ onSetDay(); });
    if (m_btnNight)    m_btnNight->setOnClick([this]{ onSetNight(); });
    if (m_btnFwd)      m_btnFwd->setOnClick([this]{ onFwdTime(); });
    if (m_btnBack)     m_btnBack->setOnClick([this]{ onBackTime(); });
    if (m_btnQuit)     m_btnQuit->setOnClick([this]{ onQuitClicked(); });

    TINA_INFO("PauseScene: 初始化完成");
}

void PauseScene::onExit()
{
    TINA_INFO("PauseScene::onExit - 退出暂停菜单");

    // 🔧 关键：先清空事件系统的UI根节点，避免访问已销毁的UI
    if (app()) {
        app()->events().setUIRoot(nullptr);
    }

    // 清理 UI（必须在渲染资源之前清理）
    m_rootNode.reset();
    m_btnContinue = nullptr;
    m_btnQuit = nullptr;

    // 清理渲染资源由全局 Renderer 管理
}

void PauseScene::update(float dt)
{
    // 处理输入
    handleInput();

    if (m_rootNode) {
        m_rootNode->update(dt);
    }
}

void PauseScene::render()
{
    // 1. 确保 UI 视图有效（触摸 + 设置正交）
    bgfx::touch(uiViewId());
    setupUIView(uiViewId(), m_pixelWidth, m_pixelHeight);

    // 2. 渲染半透明遮罩（使用 SceneRenderer 新架构）
    scene().drawOverlay(uiViewId(), Tina::Core::Color(0.0f, 0.0f, 0.0f, 0.5f));

    // 3. ✅ 使用 Scene 基类的 ui() 方法和 RAII 作用域
    if (m_rootNode) {
        auto scope = ui().beginRender(uiViewId());
        m_rootNode->render(uiViewId(), ui());
    }
}

// handleEvent 已删除，输入处理移至 update() 中使用 InputSystem

void PauseScene::updateWindowSize(int width, int height)
{
    // 调用基类更新（触发防抖动）
    Scene::updateWindowSize(width, height);
}

// 覆盖实际应用窗口调整的方法
void PauseScene::applyWindowResize(int width, int height)
{
    // 更新场景的窗口尺寸（重要！createUI需要使用这些值）
    m_pixelWidth = width;
    m_pixelHeight = height;

    // 调用基类的实际应用方法
    Scene::applyWindowResize(width, height);

    // 计算新的缩放比例
    float newScale = UI::UIUtils::calculateUIScale(width, height);

    // 如果缩放变化非常大，才重建UI（提高阈值，减少重建）
    if (std::abs(newScale - m_uiScale) > 0.3f) {
        m_uiScale = newScale;
        createUI();  // 重建UI
        TINA_INFO("PauseScene: 重建UI，新缩放比例: {}", m_uiScale);
    } else {
        // 更新缩放但不重建
        m_uiScale = newScale;
        // 只需重新居中面板
        if (m_panel && m_rootNode) {
            m_rootNode->setSize((float)width, (float)height);

            // 重新计算居中位置
            float panelW = m_panel->getSize().x;
            float panelH = m_panel->getSize().y;
            auto [centerX, centerY] = UI::UIUtils::calculateCenterPosition(
                (float)width, (float)height, panelW, panelH);

            m_panel->setPosition(centerX, centerY);
            m_rootNode->requestLayout();
            m_rootNode->performLayoutNow();

            TINA_INFO("PauseScene: 重新居中面板到 ({}, {})", centerX, centerY);
        }
    }
}

void PauseScene::handleInput()
{
    // 使用 InputSystem 进行输入处理
    auto* appPtr = app();
    if (!appPtr) return;
    auto& input = appPtr->input();

    // ESC 键：返回游戏
    if (input.isKeyPressed(Engine::KeyCode::Escape)) {
        onContinueClicked();
        return;
    }

    // 更新UI输入到引擎事件系统
    auto mousePos = input.getMousePosition();
    bool leftDown = input.isMouseButtonDown(Engine::MouseButton::Left);
    app()->events().updateUIInput(mousePos.x, mousePos.y, leftDown);
}

void PauseScene::createUI()
{
    // 🔧 关键修复：若重复创建（例如窗口尺寸变化），先清空事件系统的UI引用
    if (m_rootNode && app()) {
        app()->events().setUIRoot(nullptr);
    }
    
    // 然后释放旧 UI
    m_rootNode.reset();
    m_btnContinue = nullptr;
    m_btnQuit = nullptr;
    m_panel = nullptr;  // 重置面板引用

    // 创建根节点
    m_rootNode = Memory::MakeUnique<UI::UINode>("PauseRoot");
    m_rootNode->setPosition(0, 0);
    m_rootNode->setSize((float)m_pixelWidth, (float)m_pixelHeight);

    // 顶层不再放置标题，标题移入面板内部

    // 使用布局：居中一个面板，内部垂直栈+若干水平栈，避免纵向堆叠
    // 使用存储的缩放比例（如果没有设置，则计算）
    if (m_uiScale == 1.0f) {
        m_uiScale = UI::UIUtils::calculateUIScale(m_pixelWidth, m_pixelHeight);
    }
    float scale = m_uiScale;

    const float panelW   = 680.0f * scale;
    const float btnH     = 56.0f * scale;
    const float pad      = 16.0f * scale;   // VBox 内边距（左右/上下）
    const float spacing  = 12.0f * scale;   // VBox 子项间距
    const float titleH   = 44.0f * scale;   // 标题高度
    const float debugH   = 36.0f * scale;   // 分组标题高度
    const float rowW     = panelW - pad * 2.0f;               // VBox 左右 padding 共 32
    const float colW     = (rowW - 8.0f * scale * 2.0f - 12.0f * scale) * 0.5f; // 行左右 padding=8，列间距=12

    // 使用新的API：createChild
    m_panel = m_rootNode->createChild<UI::UIPanel>("PausePanel");  // 保存面板引用
    m_panel->setColor(0.08f, 0.08f, 0.10f, 0.92f);
    m_panel->setSize(panelW, 1.0f);
    m_panel->setHeightWrap(); // 交由容器包裹

    auto* panel = m_panel;  // 为了兼容后续代码

    auto* vbox = panel->createChild<UI::UIVStack>("VBox");
    vbox->setSize(panelW, 0.0f);
    vbox->setHeightWrap(); // 容器自动包裹内容高度
    vbox->setPadding(pad, pad);
    vbox->setSpacing(spacing);

    // 标题
    auto* title = vbox->createChild<UI::UILabel>();
    title->setText("游戏已暂停");
    title->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    title->setSize(panelW - pad * 2.0f, titleH);

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
    m_btnContinue->setOnClick([this]{ onContinueClicked(); });

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
    m_btnDay->setOnClick([this]{ onSetDay(); });
    m_btnNight->setOnClick([this]{ onSetNight(); });

    makeRow(m_btnFwd,    "时间 +10%",    m_btnBack,    "时间 -10%");
    m_btnFwd->setOnClick([this]{ onFwdTime(); });
    m_btnBack->setOnClick([this]{ onBackTime(); });

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
    m_btnQuit->setOnClick([this]{ onQuitClicked(); });
    

    // 触发布局计算并回填 Panel 高度，再居中 Panel
    // 重要：使用performLayoutNow()确保布局立即完成，这样getSize()才能返回正确的值
    vbox->requestLayout();
    panel->requestLayout();

    // 调试：执行布局前的尺寸
    TINA_INFO("PauseScene: 布局前 - vbox高度: {}, panel高度: {}",
              vbox->getSize().y, panel->getSize().y);

    panel->performLayoutNow();  // 强制立即执行布局（包括所有子节点）

    // 调试：执行布局后的尺寸
    TINA_INFO("PauseScene: 布局后 - vbox高度: {}, panel高度: {}",
              vbox->getSize().y, panel->getSize().y);

    // 现在可以安全地获取panel的实际高度来计算居中位置
    float panelHeight = panel->getSize().y;
    float centerX = (m_pixelWidth - panelW) * 0.5f;
    float centerY = (m_pixelHeight - panelHeight) * 0.5f;

    TINA_INFO("PauseScene: 面板居中 - 屏幕({}x{}), 面板({}x{}), 中心位置({}, {})",
              m_pixelWidth, m_pixelHeight, panelW, panelHeight, centerX, centerY);

    panel->setPosition(centerX, centerY);

    // 🔧 关键修复：重新设置UI根节点到事件系统
    if (app()) {
        app()->events().setUIRoot(m_rootNode.get());
    }

    TINA_INFO("PauseScene: UI 创建完成");
}

void PauseScene::renderOverlay()
{
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

void PauseScene::onSetDay() {
    Tina::Game::Events::SetDayNight event;
    event.normalized = 0.25f;
    app()->events().trigger(event);
}

void PauseScene::onSetNight() {
    Tina::Game::Events::SetDayNight event;
    event.normalized = 0.75f;
    app()->events().trigger(event);
}

// 已移除"暂停/恢复昼夜"功能
void PauseScene::onFwdTime() {
    Tina::Game::Events::AdjustDayNight event;
    event.delta = +0.10f;
    app()->events().trigger(event);
}

void PauseScene::onBackTime() {
    Tina::Game::Events::AdjustDayNight event;
    event.delta = -0.10f;
    app()->events().trigger(event);
}

} // namespace Tina::Game

