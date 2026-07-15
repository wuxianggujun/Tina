#pragma once

#include "../engine/Camera3D.hpp"
#include "../engine/Scene.hpp"
#include "../renderer/SimpleMeshRenderer.hpp"

namespace Tina::Game {

// Deterministic 3D smoke scene: perspective camera + depth-tested indexed cube.
class Smoke3DScene final : public Engine::Scene {
public:
    void onEnter() override;
    void onExit() override;
    void onWindowSizeChanged(int width, int height) override;
    void update(float dt) override;

protected:
    Container::Vector<ViewSetup> getViewSetup() override;
    void render() override;

private:
    Engine::Camera3D m_camera3D;
    Renderer::SimpleMeshRenderer m_meshRenderer;
    float m_rotationRadians = 0.0f;
};

} // namespace Tina::Game
