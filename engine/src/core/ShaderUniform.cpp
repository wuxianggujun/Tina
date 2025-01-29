#include "ShaderUniform.hpp"
#include "graphics/Color.hpp"
#include <stdexcept>
#include <fmt/format.h>
#include <thread>
#include <chrono>

namespace Tina {
    ShaderUniform::ShaderUniform(ShaderUniform &&other) noexcept {
        this->m_handle = other.m_handle;
        this->m_uniformName = other.m_uniformName;
        other.m_handle.idx = bgfx::kInvalidHandle;
        other.m_uniformName = nullptr;
    }

    ShaderUniform & ShaderUniform::operator=(ShaderUniform &&other) noexcept {
        if (this != &other) {
            this->free();  // 使用this->来明确调用成员函数
            this->m_handle = other.m_handle;
            this->m_uniformName = other.m_uniformName;
            other.m_handle.idx = bgfx::kInvalidHandle;
            other.m_uniformName = nullptr;
        }
        return *this;
    }

    ShaderUniform::~ShaderUniform() {
        this->free();
    }

    void ShaderUniform::setHandle(bgfx::UniformHandle handle, const char* uniformName) {
        if (bgfx::isValid(m_handle)) {
            this->free();
        }
        m_handle = handle;
        m_uniformName = uniformName;
        
        if (!bgfx::isValid(m_handle)) {
            fmt::print("Warning: Setting invalid uniform handle for '{}'\n", 
                m_uniformName ? m_uniformName : "unknown");
        } else {
            fmt::print("Successfully set uniform handle for '{}'\n", 
                m_uniformName ? m_uniformName : "unknown");
        }
    }

    void ShaderUniform::init(const char *uniformName, bgfx::UniformType::Enum type, uint16_t num) {
        if (bgfx::isValid(m_handle)) {
            this->free();
        }
        
        this->m_uniformName = uniformName;
        this->m_handle = bgfx::createUniform(uniformName, type, num);
        
        if (!bgfx::isValid(m_handle)) {
            fmt::print("Failed to create uniform '{}' of type {}\n", 
                uniformName, static_cast<int>(type));
        } else {
            fmt::print("Successfully created uniform '{}'\n", uniformName);
        }
    }

    void ShaderUniform::free() {
        if (bgfx::isValid(m_handle)) {
            bgfx::destroy(m_handle);
            m_handle.idx = bgfx::kInvalidHandle;
        }
        m_uniformName = nullptr;
    }

    void ShaderUniform::setValue(float x, float y, float z, float w) const {
        if (!bgfx::isValid(m_handle)) {
            fmt::print("Attempting to set value for invalid uniform '{}'\n", m_uniformName ? m_uniformName : "unknown");
            return;
        }
        float value[4] = { x, y, z, w };
        bgfx::setUniform(m_handle, value);
    }

    void ShaderUniform::setValue(const Color &color, bool needsRounding) const {
        if (!bgfx::isValid(m_handle)) {
            fmt::print("Attempting to set color for invalid uniform '{}'\n", m_uniformName ? m_uniformName : "unknown");
            return;
        }
        if (!needsRounding) {
            float value[4] = {color.r, color.g, color.b, color.a};
            bgfx::setUniform(m_handle, value);
        }
        else {
            float value[4] = {color.r, color.g, color.b, color.a};
            bgfx::setUniform(m_handle, value);
        }
    }

    void ShaderUniform::setMatrix4(const float* matrix) const {
        if (!bgfx::isValid(m_handle)) {
            fmt::print("Attempting to set matrix for invalid uniform '{}'\n", m_uniformName ? m_uniformName : "unknown");
            throw std::runtime_error("Invalid uniform handle");
        }
        
        if (matrix == nullptr) {
            fmt::print("Attempting to set null matrix for uniform '{}'\n", m_uniformName ? m_uniformName : "unknown");
            throw std::runtime_error("Null matrix pointer");
        }

        try {
            bgfx::setUniform(m_handle, matrix);
            fmt::print("Successfully set matrix for uniform '{}'\n", m_uniformName ? m_uniformName : "unknown");
        } catch (const std::exception& e) {
            fmt::print("Failed to set matrix for uniform '{}': {}\n", m_uniformName ? m_uniformName : "unknown", e.what());
            throw;
        }
    }
}
