//
// WorldSelectScene 实现
//

#include "WorldSelectScene.hpp"
#include "GameScene.hpp"
#include "MenuScene.hpp"
#include "../engine/Application.hpp"
#include "../engine/SceneManager.hpp"
#include "../engine/InputSystem.hpp"
#include "../core/Log.hpp"
#include "../ui/UIConstants.hpp"

#include <algorithm>
#include <ctime>

namespace Tina::Game {

WorldSelectScene::WorldSelectScene() = default;
WorldSelectScene::~WorldSelectScene() = default;

Container::Vector<Engine::Scene::ViewSetup> WorldSelectScene::getViewSetup() {
    return {
        { UI::VIEW_BACKGROUND, Engine::Scene::ViewSetup::Background2D, false },
        { UI::VIEW_UI, Engine::Scene::ViewSetup::UI2D, false }
    };
}

void WorldSelectScene::onEnter() {
    TINA_INFO("WorldSelectScene::onEnter - 进入世界选择场景");

    // 预备数据
    loadWorldList();

    // 创建 UI
    createUI();
    addUIRoot(m_root.get());
    app()->events().setUIRoot(m_root);
}

void WorldSelectScene::onExit() {
    // 清理 UI
    if (app()) {
        app()->events().setUIRoot(Memory::SharedPtr<UI::UINode>());
    }
    m_root.reset();
    m_list = nullptr;
    m_btnEnter = m_btnCreate = m_btnBack = nullptr;
    m_createDialog = nullptr;
    m_worldNameInput = nullptr;
}

void WorldSelectScene::update(float /*dt*/) {
    handleInput();
}

void WorldSelectScene::render() {
    // 背景渐变
    {
        Core::Color top{0.08f, 0.08f, 0.1f, 1.0f};
        Core::Color bottom{0.02f, 0.02f, 0.04f, 1.0f};
        scene().drawGradientBackground(UI::VIEW_BACKGROUND, top, bottom);
    }

    auto scope = ui().beginRender(uiViewId());

    // 标题
    {
        UI::UIRenderer::TextOptions to{};
        to.r = 1; to.g = 1; to.b = 1; to.a = 1;
        to.hAlign = UI::UIRenderer::AlignH::Center;
        to.vAlign = UI::UIRenderer::AlignV::Center;
        to.fontPx = std::max(22, (int)std::lround(40.0f * m_uiScale));
        ui().drawTextBox(uiViewId(), (float)getPixelWidth() * 0.5f - 200.0f, 40.0f, 400.0f, 40.0f,
                         "选择世界", to);
    }

    if (m_root) {
        m_root->render(uiViewId(), ui());
    }
}

void WorldSelectScene::RootNode::onWindowSizeChanged(int w, int h) {
    setSize((float)w, (float)h);
    if (m_scene) m_scene->updateLayout();
    UINode::onWindowSizeChanged(w, h);
}

void WorldSelectScene::createUI() {
    // 根节点
    m_root = Memory::MakeShared<RootNode>(this);
    m_root->setSize((float)getPixelWidth(), (float)getPixelHeight());

    // 列表
    auto listPtr = Memory::MakeUnique<UI::UIListView>("WorldList");
    m_list = m_root->addChild(std::move(listPtr));
    m_list->setFontPx(20);
    m_list->setItemHeight(36.0f);
    m_list->setSize(600.0f, 400.0f);
    m_list->setPosition((float)getPixelWidth()*0.5f - 300.0f, 100.0f);
    // 数据绑定
    UI::UIListView::Items names;
    names.reserve(m_worlds.size());
    for (auto& w : m_worlds) names.push_back(w.name);
    m_list->setItems(names);
    m_list->setOnItemActivated([this](int idx){
        (void)idx; onEnterClicked();
    });
    m_list->setOnSelectionChanged([this](int /*idx*/){
        if (m_btnEnter) m_btnEnter->setEnabled(true);
    });

    // 按钮
    auto mkBtn = [&](const char* name, const char* text) -> UI::UIButton* {
        auto b = Memory::MakeUnique<UI::UIButton>(name);
        b->setText(text);
        b->setSize(160.0f, 44.0f);
        b->setFontPx(22);
        return m_root->addChild(std::move(b));
    };
    m_btnEnter = mkBtn("BtnEnter", "进入世界");
    m_btnCreate = mkBtn("BtnCreate", "新建世界");
    m_btnBack = mkBtn("BtnBack", "返回");

    if (m_btnEnter) m_btnEnter->setOnClick([this]{ onEnterClicked(); });
    if (m_btnCreate) m_btnCreate->setOnClick([this]{ onCreateClicked(); });
    if (m_btnBack) m_btnBack->setOnClick([this]{ onBackClicked(); });

    // 创建"新建世界"对话框
    auto dialog = Memory::MakeUnique<UI::UIDialog>("CreateWorldDialog");
    m_createDialog = m_root->addChild(std::move(dialog));
    m_createDialog->setTitle("新建世界");
    m_createDialog->setSize((float)getPixelWidth(), (float)getPixelHeight());
    m_createDialog->setVisible(false);  // 默认隐藏
    m_createDialog->setEventSystem(&app()->events());

    // 在对话框内容区域添加提示文本和输入框
    auto contentArea = m_createDialog->getContentArea();
    if (contentArea) {
        // 提示文本
        auto label = contentArea->createChild<UI::UILabel>("PromptLabel");
        label->setText("请输入世界名称：");
        label->setFontPx(20);
        label->setPosition(0, 20);
        label->setSize(460, 30);

        // 文本输入框
        auto textEdit = contentArea->createChild<UI::UITextEdit>("WorldNameInput");
        m_worldNameInput = textEdit;
        m_worldNameInput->setPosition(0, 60);
        m_worldNameInput->setSize(460, 40);
        m_worldNameInput->setPlaceholder("例如：我的世界");
        m_worldNameInput->setMaxLength(50);
        m_worldNameInput->setFontPx(18);
        m_worldNameInput->setEventSystem(&app()->events());
    }

    // 设置对话框回调
    m_createDialog->setOnConfirm([this]() {
        // 确定按钮：读取输入框内容，创建世界
        if (!m_worldNameInput) return;

        std::string worldName = m_worldNameInput->getText();
        if (worldName.empty()) {
            worldName = "新世界 " + std::to_string(m_worlds.size() + 1);
        }

        // 创建世界
        WorldItem item;
        item.name = worldName;
        item.seed = (uint32_t)std::time(nullptr) ^ (uint32_t)(m_worlds.size() * 2654435761u);
        m_worlds.push_back(item);

        // 更新列表
        if (m_list) {
            auto names = m_list->items();
            names.push_back(item.name);
            m_list->setItems(names);
            m_list->setSelectedIndex((int)names.size() - 1);
        }

        // 清空输入框
        m_worldNameInput->clear();

        TINA_INFO("创建新世界: '{}' (seed={})", item.name, item.seed);
    });

    m_createDialog->setOnCancel([this]() {
        // 取消按钮：清空输入框
        if (m_worldNameInput) {
            m_worldNameInput->clear();
        }
    });

    // 初始布局
    updateLayout();
}

void WorldSelectScene::updateLayout() {
    const float baseW = 1280.0f, baseH = 720.0f;
    float sx = (float)getPixelWidth() / baseW;
    float sy = (float)getPixelHeight() / baseH;
    float newScale = std::max(0.75f, std::min(sx, sy));

    // 只有在缩放改变时才更新布局
    bool scaleChanged = std::abs(newScale - m_uiScale) > 0.01f;
    m_uiScale = newScale;

    if (!m_root) return;
    m_root->setSize((float)getPixelWidth(), (float)getPixelHeight());

    float listW = 600.0f * m_uiScale;
    float listH = 420.0f * m_uiScale;
    float cx = (float)getPixelWidth() * 0.5f;
    float top = 100.0f * m_uiScale;

    if (m_list) {
        float newItemHeight = std::max(28.0f, 36.0f * m_uiScale);
        int newFontPx = std::max(16, (int)std::lround(20.0f * m_uiScale));

        // 只在真正需要时更新
        if (scaleChanged || m_list->getSize().x != listW || m_list->getSize().y != listH) {
            m_list->setSize(listW, listH);
        }

        float newPosX = cx - listW * 0.5f;
        float newPosY = top;
        auto currentPos = m_list->getPosition();
        if (std::abs(currentPos.x - newPosX) > 0.1f || std::abs(currentPos.y - newPosY) > 0.1f) {
            m_list->setPosition(newPosX, newPosY);
            TINA_DEBUG("WorldSelectScene::updateLayout - 更新列表位置: ({}, {}) -> ({}, {})",
                      currentPos.x, currentPos.y, newPosX, newPosY);
        }

        if (scaleChanged || std::abs(m_list->itemHeight() - newItemHeight) > 0.1f) {
            m_list->setItemHeight(newItemHeight);
        }

        if (m_list->fontPx() != newFontPx) {
            m_list->setFontPx(newFontPx);
        }
    }

    float btnY = top + listH + 20.0f * m_uiScale;
    float btnW = 180.0f * m_uiScale;
    float btnH = 46.0f * m_uiScale;
    float gap = 20.0f * m_uiScale;
    auto ensureFit = [&](UI::UIButton* btn, float xPos){
        if (!btn) return;
        int px = std::max(18, (int)std::lround(22.0f * m_uiScale));
        btn->setFontPx(px);
        float tw=0.0f, th=0.0f;
        ui().measureText(btn->getText(), tw, th, px);
        float minW = tw + 32.0f * m_uiScale; // 文本宽 + 左右内边距
        float finalW = std::max(btnW, minW);
        btn->setSize(finalW, btnH);
        btn->setPosition(xPos - finalW * 0.5f, btnY);
    };
    // 左中右布局（Enter 左、Create 中、Back 右）
    ensureFit(m_btnEnter, cx - (btnW + gap));
    ensureFit(m_btnCreate, cx);
    ensureFit(m_btnBack,   cx + (btnW + gap));
}

void WorldSelectScene::handleInput() {
    auto* a = app(); if (!a) return;
    auto& in = a->input();
    if (in.isKeyPressed(Engine::KeyCode::Escape)) { onBackClicked(); return; }
    if (in.isKeyPressed(Engine::KeyCode::Enter)) { onEnterClicked(); return; }
    if (m_list) {
        int sel = m_list->selectedIndex();
        if (in.isKeyPressed(Engine::KeyCode::Up)) {
            if (sel == -1 && !m_worlds.empty()) sel = 0; else sel = std::max(0, sel - 1);
            m_list->setSelectedIndex(sel);
        } else if (in.isKeyPressed(Engine::KeyCode::Down)) {
            if (sel == -1 && !m_worlds.empty()) sel = 0; else sel = std::min((int)m_worlds.size() - 1, sel + 1);
            m_list->setSelectedIndex(sel);
        }
    }
    // 同步 UI 输入到引擎事件系统（让按钮正常响应）
    a->events().updateUIInput(in.getMouseX(), in.getMouseY(), in.isMouseButtonDown(Engine::MouseButton::Left));
}

void WorldSelectScene::onBackClicked() {
    // 返回主菜单：替换为 MenuScene
    app()->scenes().replace(Memory::MakeUnique<MenuScene>());
}

void WorldSelectScene::onCreateClicked() {
    // 显示新建世界对话框
    if (m_createDialog) {
        m_createDialog->show();

        // 设置输入框焦点
        if (m_worldNameInput) {
            m_worldNameInput->setFocus(true);
        }
    }
}

void WorldSelectScene::onEnterClicked() {
    if (!m_list) return;
    int idx = m_list->selectedIndex();
    if (idx < 0 || idx >= (int)m_worlds.size()) {
        TINA_WARN("WorldSelect: 未选择世界，无法进入");
        return;
    }
    uint32_t seed = m_worlds[idx].seed;
    TINA_INFO("进入世界: '{}' (seed={})", m_worlds[idx].name, seed);
    app()->scenes().replace(Memory::MakeUnique<GameScene>(seed));
}

void WorldSelectScene::loadWorldList() {
    // 目前使用示例内存数据；后续可从磁盘扫描 resources/worlds 目录
    m_worlds.clear();
    const char* defaults[] = { "草原世界", "沙漠世界", "雪原世界" };
    for (auto* name : defaults) {
        WorldItem w; w.name = name; w.seed = hashSeed(w.name);
        m_worlds.push_back(w);
    }

    // 调试：补充更多示例项，便于验证列表可滚动与裁剪
    // 如不需要可删除以下循环
    for (int i = 1; i <= 40; ++i) {
        WorldItem w; w.name = std::string("测试世界 ") + std::to_string(i);
        w.seed = hashSeed(w.name);
        m_worlds.push_back(w);
    }
}

uint32_t WorldSelectScene::hashSeed(const std::string& name) {
    // 简易FNV-1a 32 位哈希
    uint32_t h = 2166136261u;
    for (unsigned char c : name) { h ^= c; h *= 16777619u; }
    if (h == 0) h = 1; // 避免为0
    return h;
}

} // namespace Tina::Game
