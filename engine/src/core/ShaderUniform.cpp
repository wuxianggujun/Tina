#include "ShaderUniform.hpp"

#include <stdexcept>

namespace Tina {
    ShaderUniform::ShaderUniform(ShaderUniform &&other) noexcept {
        this->m_handle = other.m_handle;
        other.m_handle.idx = bgfx::kInvalidHandle;
    }

    ShaderUniform & ShaderUniform::operator=(ShaderUniform &&other) noexcept {
        this->m_handle = other.m_handle;
        other.m_handle.idx = bgfx::kInvalidHandle;
        return *this;
    }

    ShaderUniform::~ShaderUniform() {
        free();
    }

    void ShaderUniform::init(const char *uniformName, bgfx::UniformType::Enum type, uint16_t num) {
        if (bgfx::isValid(m_handle)) {
            throw std::runtime_error("ShaderUniform::init: Uniform already exists");
        }
        this->m_uniformName = uniformName;
        this->m_handle = bgfx::createUniform(uniformName, type, num);
    }

    void ShaderUniform::free() {
        if (!bgfx::isValid(m_handle)) {
           return;
        }
        bgfx::destroy(m_handle);
        m_handle.idx = bgfx::kInvalidHandle;
    }

    void ShaderUniform::setValue(float x, float y, float z, float w) const {
        float value[4] = { x, y, z, w };
        bgfx::setUniform(m_handle, value);
    }

    void ShaderUniform::setValue(const Color &color, bool needsRounding) const {
        /*if (!needsRounding) {
            float value[4] = {color.r, color.g, color.b, color.a};
            bgfx::setUniform(m_handle, value);
        }
        else {
            float value[4] = {
                color.r255() / 255.f,
                color.g255() / 255.f,
                color.b255() / 255.f,
                color.a255() / 255.f
            };

            bgfx::setUniform(m_handle, value);
        }*/
    }

    void ShaderUniform::setMatrix4(const float* matrix) const {
        bgfx::setUniform(m_handle, matrix);
    }
}
