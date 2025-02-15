//
// Created by wuxianggujun on 2025/2/15.
//

#pragma once

#include <mutex>

#include "tina/core/Core.hpp"
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include <bgfx/bgfx.h>

namespace Tina
{
    class TINA_CORE_API UniformManager
    {
    public:
        UniformManager() = default;
        ~UniformManager();


        UniformManager(const UniformManager&) = delete;
        UniformManager& operator=(const UniformManager&) = delete;

        // 创建或获取Uniform
        bgfx::UniformHandle createUniform(const std::string& name,bgfx::UniformType::Enum type);

        // 设置Uniform的编写方法
        void setUniform(const std::string& name,const float* data,uint16_t num = 1);
        void setUniform(const std::string& name,float value);
        void setUniform(const std::string& name,const glm::vec4& value);
        void setUniform(const std::string& name,const glm::mat4& value);

        // 检查Uniform 是否存在
        bool hasUniform(const std::string& name) const;

        // 获取Uniform handle
        bgfx::UniformHandle getUniformHandle(const std::string& name) const;

        // 删除Uniform
        void destroyUniform(const std::string& name);

        void shutdown();

    private:

        struct UniformInfo
        {
            bgfx::UniformHandle handle;
            bgfx::UniformType::Enum type;
            std::string name;
        };

        std::unordered_map<std::string, UniformInfo> m_Uniforms;
        mutable std::mutex m_Mutex;
        bool m_Initialized = false;
    };
} // Tina
