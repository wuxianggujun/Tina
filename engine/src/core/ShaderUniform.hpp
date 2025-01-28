#ifndef TINA_CORE_SHADERUNIFORM_HPP
#define TINA_CORE_SHADERUNIFORM_HPP

#include <bgfx/bgfx.h>
#include "base/NonCopyable.hpp"

namespace Tina {
    class Color;

    class ShaderUniform : public NonCopyable {
    public:
        ShaderUniform() = default;

        ShaderUniform(ShaderUniform &&other) noexcept;

        ShaderUniform &operator=(ShaderUniform &&other) noexcept;

        ~ShaderUniform();

        void init(const char *uniformName, bgfx::UniformType::Enum type, uint16_t num = 1);

        void free();

        void setHandle(bgfx::UniformHandle handle);

        void setValue(float x, float y = 0.f, float z = 0.f, float w = 0.f) const;

        void setValue(const Color &color, bool needsRounding = false) const;

        void setMatrix4(const float* matrix) const;

        [[nodiscard]] bool isValid() const { return bgfx::isValid(m_handle); }

        [[nodiscard]] const bgfx::UniformHandle &getHandle() const { return m_handle; };

    private:
        const char *m_uniformName = nullptr;
        bgfx::UniformHandle m_handle = BGFX_INVALID_HANDLE;
    };
}


#endif //TINA_CORE_SHADERUNIFORM_HPP
