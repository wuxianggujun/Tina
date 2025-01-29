#ifndef TINA_CORE_SHADER_RESOURCE_HPP
#define TINA_CORE_SHADER_RESOURCE_HPP

#include "Resource.hpp"
#include "core/Shader.hpp"

namespace Tina{

    class ShaderResource : public Resource{
    public:
        ShaderResource(const ResourceHandle &handle, const std::string &path);
        ~ShaderResource() override;

        bool load() override;
        void unload() override;
        bool isLoaded() const override;
        const Shader &getShader() const;

        static constexpr ResourceType staticResourceType = ResourceType::Shader;
    private:
        Shader m_shader;
    };

}

#endif //TINA_CORE_SHADER_RESOURCE_HPP