//
// Created by wuxianggujun on 2025/2/15.
//

#include "tina/renderer/UniformManager.hpp"
#include <glm/gtc/type_ptr.hpp>
#include "tina/log/Logger.hpp"

namespace Tina
{
    bgfx::UniformHandle UniformManager::createUniform(const std::string& name, bgfx::UniformType::Enum type)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);

        auto it = m_Uniforms.find(name);
        if (it != m_Uniforms.end())
        {
            return it->second.handle;
        }

        bgfx::UniformHandle handle = bgfx::createUniform(name.c_str(), type);
        if (!bgfx::isValid(handle))
        {
            TINA_CORE_LOG_ERROR("Failed to create uniform: {}", name);
            return BGFX_INVALID_HANDLE;
        }

        UniformInfo info{handle, type, name};
        m_Uniforms[name] = info;

        TINA_CORE_LOG_DEBUG("Created uniform: '{}' of type {}", name, static_cast<int>(type));

        return handle;
    }

    void UniformManager::setUniform(const std::string& name, const float* data, uint16_t num)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Uniforms.find(name);
        if (it != m_Uniforms.end())
        {
            bgfx::setUniform(it->second.handle, data, num);
        }
        else
        {
            TINA_CORE_LOG_WARN("Attempting to set non-existent uniform: {}", name);
        }
    }

    void UniformManager::setUniform(const std::string& name, float value)
    {
        setUniform(name, &value, 1);
    }

    void UniformManager::setUniform(const std::string& name, const glm::vec4& value)
    {
        setUniform(name, glm::value_ptr(value), 1);
    }

    void UniformManager::setUniform(const std::string& name, const glm::mat4& value)
    {
        setUniform(name, glm::value_ptr(value), 4);
    }

    bool UniformManager::hasUniform(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Uniforms.find(name) != m_Uniforms.end();
    }

    bgfx::UniformHandle UniformManager::getUniformHandle(const std::string& name) const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Uniforms.find(name);
        if (it != m_Uniforms.end())
        {
            return it->second.handle;
        }
        return BGFX_INVALID_HANDLE;
    }

    void UniformManager::destroyUniform(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Uniforms.find(name);
        if (it != m_Uniforms.end())
        {
            if (bgfx::isValid(it->second.handle))
            {
                bgfx::destroy(it->second.handle);
                TINA_CORE_LOG_DEBUG("Destroyed uniform: {}", name);
            }
            m_Uniforms.erase(it);
        }
    }

    void UniformManager::shutdown()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Initialized) return;

        TINA_CORE_LOG_INFO("Shutting down UniformManager");

        for (auto& [name,info] : m_Uniforms)
        {
            if (bgfx::isValid(info.handle))
            {
                bgfx::destroy(info.handle);
                TINA_CORE_LOG_DEBUG("Destroyed uniform: {}", name);
            }
        }
        m_Uniforms.clear();
        m_Initialized = false;
    }

    UniformManager::~UniformManager()
    {
        shutdown();
    }
} // Tina
