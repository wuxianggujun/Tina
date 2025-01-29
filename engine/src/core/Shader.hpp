#ifndef TINA_CORE_SHADER_HPP
#define TINA_CORE_SHADER_HPP

#include <bgfx/bgfx.h>
#include <bx/file.h>
#include <string>
#include <vector>
#include <bx/filepath.h>

namespace Tina {
    
    class Transform;

    class Shader {
    public:
        Shader() = default;

        explicit Shader(const std::string &name);

        ~Shader();

        void loadFromFile(const std::string &name);

        bgfx::ShaderHandle loadShader(const std::string &path);

        [[nodiscard]] bool isValid() const;

        [[nodiscard]] const bgfx::ProgramHandle &getProgram() const {
            return m_program;
        }

        void destory();

    private:
        const std::string SHADER_PATH = "../resources/shaders/";
        bgfx::ShaderHandle m_vertexShader = BGFX_INVALID_HANDLE;
        bgfx::ShaderHandle m_fragmentShader = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    };
}

#endif //TINA_CORE_SHADER_HPP
