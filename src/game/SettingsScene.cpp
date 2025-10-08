//
// SettingsScene 实现
//

#include "SettingsScene.hpp"
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"
#include "../engine/EventBus.hpp"
#include "../core/Log.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include "../ui/UILayout.hpp"

namespace Tina::Game {

SettingsScene::SettingsScene() = default;
SettingsScene::~SettingsScene() = default;

void SettingsScene::onEnter()
{
    TINA_INFO("SettingsScene::onEnter - 进入设置页面");

    app()->getPixelSize(m_pixelWidth, m_pixelHeight);

    m_textRenderer = Memory::MakeUnique<UI::TextRenderer>();
    if (!m_textRenderer->initialize(app()->shaders(), app()->resources())) {
        TINA_ERROR("SettingsScene: TextRenderer 初始化失败");
    } else {
        m_textRenderer->loadFont("resources/fonts/SourceHanSansSC-Regular.otf", 28);
    }

    m_uiRenderer = Memory::MakeUnique<UI::UIRenderer>();
    m_uiRenderer->initialize(app()->shaders(), m_textRenderer.get());

    createUI();
    m_events.setRoot(m_root.get());
}

void SettingsScene::onExit()
{
    m_cDay.disconnect();
    m_cNight.disconnect();
    m_cFwd.disconnect();
    m_cBack.disconnect();
    m_cClose.disconnect();

    m_root.reset();
    m_uiRenderer.reset();
    m_textRenderer.reset();
}

void SettingsScene::update(float dt)
{
    (void)dt;
    if (m_root) m_root->update(dt);
}

void SettingsScene::render()
{
    // 触摸 UI 视图
    bgfx::touch(3);
    if (m_root && m_uiRenderer) m_root->render(3, *m_uiRenderer);
}

void SettingsScene::handleEvent(const Tina::os::Event& event)
{
    using E = Tina::os::Event;
    if (event.type == E::Type::KEY && event.key.down) {
        if (event.key.key_code == os::KeyCode::ESCAPE) {
            onBack();
            return;
        }
    }

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
}

void SettingsScene::createUI()
{
    m_root = Memory::MakeUnique<UI::UINode>("SettingsRoot");
    m_root->setSize((float)m_pixelWidth, (float)m_pixelHeight);

    // 使用与暂停页一致的“面板 + VBox”布局
    const float panelW = 520.0f;
    const float pad    = 16.0f;
    const float spacing= 12.0f;
    const float btnH   = 50.0f;

    auto* panel = new UI::UIPanel("SettingsPanel");
    panel->setColor(0.10f,0.10f,0.12f,0.90f);
    panel->setSize(panelW, 1.0f);
    panel->setHeightWrap();
    m_root->addChild(panel);

    auto* vbox = new UI::UIVStack("VBox");
    vbox->setSize(panelW, 0.0f);
    vbox->setHeightWrap();
    vbox->setPadding(pad, pad);
    vbox->setSpacing(spacing);
    panel->addChild(vbox);

    auto* title = new UI::UILabel();
    title->setText("设置 / 调试");
    title->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    title->setSize(panelW - pad*2, 44.0f);
    vbox->addChild(title);

    auto addRowButton = [&](const char* text, auto&& conn){
        auto* row = new UI::UIHStack("Row");
        row->setSize(panelW - pad*2, btnH);
        row->setPadding(0,0);
        vbox->addChild(row);
        auto* btn = new UI::UIButton();
        btn->setText(text);
        btn->setWidthMatch();
        btn->setHeight(btnH);
        row->addChild(btn);
        return btn;
    };

    m_btnDay = addRowButton("切换到白天", [this]{ onSetDay(); });
    m_cDay = m_btnDay->onClick.connect([this]{ onSetDay(); });
    m_btnNight = addRowButton("切换到黑夜", [this]{ onSetNight(); });
    m_cNight = m_btnNight->onClick.connect([this]{ onSetNight(); });
    m_btnFwd = addRowButton("时间 +10%", [this]{ onFwdTime(); });
    m_cFwd = m_btnFwd->onClick.connect([this]{ onFwdTime(); });
    m_btnBack = addRowButton("时间 -10%", [this]{ onBackTime(); });
    m_cBack = m_btnBack->onClick.connect([this]{ onBackTime(); });
    m_btnClose = addRowButton("返回", [this]{ onBack(); });
    m_cClose = m_btnClose->onClick.connect([this]{ onBack(); });

    // 触发布局并居中
    vbox->update(0.0f);
    panel->update(0.0f);
    panel->setPosition((m_pixelWidth - panelW)*0.5f, (m_pixelHeight - panel->getSize().y)*0.5f);
}

void SettingsScene::onBack()
{
    app()->scenes().requestPop();
}

void SettingsScene::onSetDay()
{
    // 设定至白天中段（默认 0.25）
    app()->events().onSetDayNightNormalized.emit(0.25f);
}

void SettingsScene::onSetNight()
{
    // 设定至黑夜中段（默认 0.75）
    app()->events().onSetDayNightNormalized.emit(0.75f);
}

void SettingsScene::onFwdTime()
{
    app()->events().onAdjustDayNightNormalized.emit(+0.10f);
}

void SettingsScene::onBackTime()
{
    app()->events().onAdjustDayNightNormalized.emit(-0.10f);
}

} // namespace Tina::Game
