#include "Smoke3DScene.hpp"

#include "../core/Log.hpp"
#include "../engine/Application.hpp"
#include "../ui/UIConstants.hpp"

#include <stdexcept>

namespace Tina::Game {

Container::Vector<Engine::Scene::ViewSetup> Smoke3DScene::getViewSetup()
{
    return {{UI::VIEW_WORLD_SOLID,
             Engine::Scene::ViewSetup::World3D,
             true,
             0x182238ff,
             static_cast<uint8_t>(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH)}};
}

void Smoke3DScene::onEnter()
{
    m_camera3D.setViewportPixels(getPixelWidth(), getPixelHeight());
    m_camera3D.setPerspective(60.0f, 0.1f, 100.0f);
    m_camera3D.lookAt(0.0f, 1.5f, 6.0f, 0.0f, 0.0f, 0.0f);

    if (!m_meshRenderer.initialize(app()->shaders())) {
        throw std::runtime_error("Smoke3DScene failed to initialize GPU mesh resources");
    }
    TINA_INFO("Smoke3DScene initialized: right-handed perspective camera and depth-tested cube");
}

void Smoke3DScene::onExit()
{
    m_meshRenderer.shutdown();
    TINA_INFO("Smoke3DScene released vertex and index buffers");
}

void Smoke3DScene::onWindowSizeChanged(int width, int height)
{
    m_camera3D.setViewportPixels(width, height);
}

void Smoke3DScene::update(float dt)
{
    m_rotationRadians += dt * 0.75f;
}

void Smoke3DScene::render()
{
    // Scene prepared the view rectangle/clear state; this camera replaces the legacy
    // Camera2D matrix with a true perspective transform for this World3D view.
    m_camera3D.applyToView(UI::VIEW_WORLD_SOLID);
    m_meshRenderer.drawCube(UI::VIEW_WORLD_SOLID, m_rotationRadians);
}

} // namespace Tina::Game
