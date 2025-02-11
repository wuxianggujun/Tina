#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <bgfx/bgfx.h>

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

private:
    ShaderManager() = default;
    ~ShaderManager() = default;
    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    // 从文件加载着色器二进制数据
    std::vector<uint8_t> loadShaderBinary(const std::string& filename);

    std::unordered_map<std::string, bgfx::ShaderHandle> m_shaderCache;
    std::unordered_map<std::string, bgfx::ProgramHandle> m_programCache;
};

} // namespace Tina
