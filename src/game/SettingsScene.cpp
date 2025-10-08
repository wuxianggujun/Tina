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
    m_cPause.disconnect();
    m_cResume.disconnect();
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

    // 标题
    auto* title = new UI::UILabel();
    title->setText("设置 / 调试");
    title->setAlignment(UI::UILabel::TextAlignH::Center, UI::UILabel::TextAlignV::Center);
    title->setPosition((float)m_pixelWidth / 2 - 200, (float)m_pixelHeight / 2 - 180);
    title->setSize(400, 40);
    m_root->addChild(title);

    const float btnW = 280.0f;
    const float btnH = 50.0f;
    const float spacing = 14.0f;
    float x = (float)m_pixelWidth / 2 - btnW / 2;
    float y = (float)m_pixelHeight / 2 - 110.0f;

    // 切换到白天
    m_btnDay = new UI::UIButton();
    m_btnDay->setText("切换到白天");
    m_btnDay->setPosition(x, y);
    m_btnDay->setSize(btnW, btnH);
    m_cDay = m_btnDay->onClick.connect([this]{ onSetDay(); });
    m_root->addChild(m_btnDay);
    y += btnH + spacing;

    // 切换到黑夜
    m_btnNight = new UI::UIButton();
    m_btnNight->setText("切换到黑夜");
    m_btnNight->setPosition(x, y);
    m_btnNight->setSize(btnW, btnH);
    m_cNight = m_btnNight->onClick.connect([this]{ onSetNight(); });
    m_root->addChild(m_btnNight);
    y += btnH + spacing;

    // 暂停昼夜
    m_btnPause = new UI::UIButton();
    m_btnPause->setText("暂停昼夜循环");
    m_btnPause->setPosition(x, y);
    m_btnPause->setSize(btnW, btnH);
    m_cPause = m_btnPause->onClick.connect([this]{ onPauseDayNight(); });
    m_root->addChild(m_btnPause);
    y += btnH + spacing;

    // 恢复昼夜
    m_btnResume = new UI::UIButton();
    m_btnResume->setText("恢复昼夜循环");
    m_btnResume->setPosition(x, y);
    m_btnResume->setSize(btnW, btnH);
    m_cResume = m_btnResume->onClick.connect([this]{ onResumeDayNight(); });
    m_root->addChild(m_btnResume);
    y += btnH + spacing;

    // 时间 +10%
    m_btnFwd = new UI::UIButton();
    m_btnFwd->setText("时间 +10%");
    m_btnFwd->setPosition(x, y);
    m_btnFwd->setSize(btnW, btnH);
    m_cFwd = m_btnFwd->onClick.connect([this]{ onFwdTime(); });
    m_root->addChild(m_btnFwd);
    y += btnH + spacing;

    // 时间 -10%
    m_btnBack = new UI::UIButton();
    m_btnBack->setText("时间 -10%");
    m_btnBack->setPosition(x, y);
    m_btnBack->setSize(btnW, btnH);
    m_cBack = m_btnBack->onClick.connect([this]{ onBackTime(); });
    m_root->addChild(m_btnBack);
    y += btnH + spacing;

    // 关闭/返回
    m_btnClose = new UI::UIButton();
    m_btnClose->setText("返回");
    m_btnClose->setPosition(x, y);
    m_btnClose->setSize(btnW, btnH);
    m_cClose = m_btnClose->onClick.connect([this]{ onBack(); });
    m_root->addChild(m_btnClose);
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

void SettingsScene::onPauseDayNight()
{
    app()->events().onSetDayNightPaused.emit(true);
}

void SettingsScene::onResumeDayNight()
{
    app()->events().onSetDayNightPaused.emit(false);
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
