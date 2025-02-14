#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <bgfx/bgfx.h>
#include "tina/utils/Profiler.hpp"

namespace Tina {

class ShaderManager {
public:
    static ShaderManager& getInstance() {
        static ShaderManager instance;
        return instance;
    }

    // 从编译好的二进制文件加载着色器
    bgfx::ShaderHandle loadShader(const std::string& name, const std::string& type);
    
    // 创建着色器程序
    bgfx::ProgramHandle createProgram(const std::string& name);

    // 释放所有着色器资源
    void shutdown();

    // 销毁着色器程序
    void destroyProgram(bgfx::ProgramHandle handle);

    // 检查是否已经关闭
    bool isShutdown() const { return m_isShutdown; }

private:
    ShaderManager() : m_isShutdown(false) {}
    ~ShaderManager() = default;
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    // 从文件加载着色器二进制数据
    std::vector<uint8_t> loadShaderBinary(const std::string& filename);

    // 检查资源是否有效
    bool isResourceValid() const { return !m_isShutdown; }

    // 存储program和其关联的shaders
    struct ShaderPair {
        bgfx::ShaderHandle vertex;
        bgfx::ShaderHandle fragment;
    };

    std::unordered_map<std::string, bgfx::ShaderHandle> m_shaderCache;
    std::unordered_map<std::string, bgfx::ProgramHandle> m_programCache;
    std::unordered_map<std::string, ShaderPair> m_programShaders;
    bool m_isShutdown;
};

} // namespace Tina
