//
// Created by wuxianggujun on 2024/5/19.
//

#ifndef TINA_RENDER_WORLDRENDERER_HPP
#define TINA_RENDER_WORLDRENDERER_HPP


#include "Renderer.hpp"
#include "framework/Layer.hpp"
#include "framework/View.hpp"
#include "framework/Player.hpp"

namespace Tina {
    class WorldRenderer : public Renderer {
    public:
        void initialize() override;

        void update(const float *viewMatrix, const float *projectionMatrix) override;

        void render(float deltaTime) override;

        [[nodiscard]] bool isEnable() const override;

    private:
        View view;
        Layer layer1;
        Layer layer2;
        Layer palyerLayer;
        int levelNumber = 0;
        Level *level = nullptr;
        Player* player = nullptr;
    };
} // Tina

#endif //TINA_RENDER_WORLDRENDERER_HPP
