//
// Created by wuxianggujun on 2024/5/19.
//

#include "WorldRenderer.hpp"

namespace Tina {
    std::vector<std::string> levels{
        "./levels/1.lf",
        "./levels/2.lf",
        "./levels/3.lf",
        "./levels/4.lf",
        "./levels/5.lf"
    };

    void WorldRenderer::initialize() {
        // Set view 0 clear state.
        /*bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0
        );*/
        // Set view 0 default viewport.
        view.addLayer(layer1);
        view.addLayer(layer1);
        view.addLayer(palyerLayer);

        level = new Level(levels.at(levelNumber));
        level->setLayer(&layer1);

        player = new Player(level);

    }

    void WorldRenderer::update(const float *viewMatrix, const float *projectionMatrix) {
    }

    void WorldRenderer::render(float deltaTime) {
        player->UpdateVelocity(deltaTime);
        player->UpdatePosition(deltaTime);

        if (player->nextLevel) {
            levelNumber++;
            if (levelNumber < levels.size()) {
                level = new Level(levels.at(levelNumber));
                level->setLayer(&layer2);
                player->SetLevel(level);
                player->nextLevel = false;
            } else {
                std::cout << "You win!!" << std::endl;
            }
        }

        view.submit();
        bgfx::frame();

        /*bgfx::touch(viewId);
        bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR, 0x333399FF);


        //绘图操作

        bgfx::frame();*/
    }

    bool WorldRenderer::isEnable() const {
        return Renderer::isEnable();
    }
} // Tina
