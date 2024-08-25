#ifndef TINA_CORE_SHADER_HPP
#define TINA_CORE_SHADER_HPP

#include <bgfx/bgfx.h>
#include <bx/file.h>
#include <string>
#include <vector>
#include <bx/filepath.h>

namespace Tina {
    using TinaUniformType = bgfx::UniformType::Enum;
    using TinaVertexBufferHandle = bgfx::VertexBufferHandle;
    using TinaDynamicIndexBufferHandle = bgfx::DynamicIndexBufferHandle;
    using TinaDynamicVertexBufferHandle = bgfx::DynamicVertexBufferHandle;
    using TinaIndexBufferHandle = bgfx::IndexBufferHandle;
    using TinaVertexLayout = bgfx::VertexLayout;
    using TinaUniformHandle = bgfx::UniformHandle;
    using TinaProgramHandle = bgfx::ProgramHandle;

    // Shader uniform data structure
    struct TinaUniform {
        TinaUniformHandle tinaHandle;
        std::string tinaName;
    };

    class Shader {
    public:
        ~Shader();
        std::string getName();
        void loadShader(const std::string& name);
        static TinaVertexBufferHandle createVertexBuffer(const void* data, uint32_t size, const TinaVertexLayout& layout);
        static TinaIndexBufferHandle createIndexBuffer(const void* data, uint32_t size);

        void applyVertexBuffer(uint8_t stream,const TinaVertexBufferHandle& handle);
        void applyIndexBuffer(const TinaIndexBufferHandle& handle);

        void submit(const bgfx::ViewId& viewID,bool depth = false);
        bgfx::ProgramHandle& getProgramHandle();
        void initUniform(const std::string& name,TinaUniformType type,uint16_t nmb = 1);
        void setUniform(const std::string &name,const void* data,uint16_t nmb = 1);
        
        TinaUniform* getUniform(std::string name);

    private:
        TinaProgramHandle m_ProgramHandle = {};
        std::vector<TinaUniform> m_Uniforms;
        std::string m_Name;

        const bgfx::Memory* getMemory(bx::FileReader& fileReader);
    };

    
}

#endif //TINA_CORE_SHADER_HPP
