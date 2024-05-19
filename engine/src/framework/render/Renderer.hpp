//
// Created by wuxianggujun on 2024/5/18.
//

#ifndef TINA_FRAMEWORK_RENDERER_HPP
#define TINA_FRAMEWORK_RENDERER_HPP

#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

namespace Tina {

    class RenderContext;

    class RenderTarget;

    class Renderer {

    public:
//        Renderer() = delete;
        Renderer() = default;

        explicit Renderer(uint16_t viewId, RenderTarget *renderTarget = nullptr);

        Renderer(const Renderer &) = delete;

        Renderer &operator=(const Renderer &) = delete;

        Renderer(Renderer &&) = delete;

        Renderer &operator=(Renderer &&) = delete;

        virtual ~Renderer() = default;

        static void setRenderContext(RenderContext *context);

        static RenderContext *getRenderContext();

        virtual void initialize() = 0;

        virtual void update(const float *viewMatrix, const float *projectionMatrix) = 0;

        virtual void render(float deltaTime) = 0;

        void updateViewRenderTarget();

        void setRenderTarget(RenderTarget *renderTarget);

        [[nodiscard]] const RenderTarget *getRenderTarget() const {return renderTarget;}

        virtual void setEnable(bool value) {renderIsEnable = value;}

        [[nodiscard]] virtual bool isEnable() const {return renderIsEnable;}


    protected:
        bgfx::ViewId viewId = 0;
        bool renderIsEnable = true;
        RenderTarget *renderTarget = nullptr;

    };

} // Tina

#endif //TINA_FRAMEWORK_RENDERER_HPP
