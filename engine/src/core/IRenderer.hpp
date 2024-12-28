#ifndef TINA_CORE_IRENDERER_HPP
#define TINA_CORE_IRENDERER_HPP

#include "math/Vector.hpp"
#include "graphics/Texture.hpp"
#include "Shader.hpp"
#include "ShaderUniform.hpp"
#include "resource/ResourceHandle.hpp"

namespace Tina{

    class IRenderer {
    public:
        virtual ~IRenderer() = default;

        virtual void init(Vector2i resolution) = 0;
        virtual void shutdown() = 0;
        virtual void render() = 0;
        virtual void frame() = 0;
       [[nodiscard]] virtual Vector2i getResolution() const = 0;
        virtual void setTexture(uint8_t stage,const ShaderUniform& uniform, const ResourceHandle& textureHandle) = 0;
        virtual void submit(uint8_t viewId,const ResourceHandle& shaderHandle) = 0 ;
    };
    

}

#endif