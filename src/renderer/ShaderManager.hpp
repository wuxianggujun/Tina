#pragma once

#include <bgfx/bgfx.h>
#include <string>
#include "../core/Container.hpp"

namespace Tina {
namespace Renderer {

class ShaderManager {
public:
    ShaderManager() = default;
    ~ShaderManager();

    // 初始化着色器管理器，设置着色器根目录
    // 若未通过编译期宏 TINA_SHADER_ROOT_DIR 指定，将退回默认相对路径
#ifndef TINA_SHADER_ROOT_DIR
#   define TINA_SHADER_ROOT_DIR "resources/shaders"
#endif
    void initialize(const std::string& shaderRootPath = TINA_SHADER_ROOT_DIR);

    // 加载着色器程序（自动根据当前渲染器类型选择合适的着色器）
    bgfx::ProgramHandle loadProgram(const std::string& vertexShaderName, 
                                   const std::string& fragmentShaderName);

    // 着色器阶段（本地定义，避免依赖不存在的 bgfx::ShaderType）
    enum class Stage { Vertex, Fragment };

    // 直接加载单个着色器（内部使用，一次性创建交给 Program 销毁）
    bgfx::ShaderHandle loadShader(const std::string& shaderName, Stage stage);

    // 清理所有已加载的着色器
    void cleanup();

private:
    // 根据当前渲染器类型获取着色器目录名
    std::string getShaderDirectoryForCurrentRenderer() const;

    // 从文件加载着色器数据
    Tina::Container::Vector<uint8_t> loadShaderFile(const std::string& filePath);

    // 构建着色器文件路径
    std::string buildShaderPath(const std::string& shaderName, Stage stage) const;

private:
    std::string m_shaderRootPath;
    Tina::Container::HashMap<Tina::Container::String, bgfx::ProgramHandle> m_loadedPrograms;
};

} // namespace Renderer
} // namespace Tina
