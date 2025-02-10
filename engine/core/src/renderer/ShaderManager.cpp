#include "tina/renderer/ShaderManager.hpp"
#include "tina/core/Engine.hpp"
#include <fstream>
#include <filesystem>

namespace Tina {

std::vector<uint8_t> ShaderManager::loadShaderBinary(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Failed to read shader file: " + filename);
    }

    return buffer;
}

bgfx::ShaderHandle ShaderManager::loadShader(const std::string& name, const std::string& type) {
    std::string cacheKey = name + "_" + type;
    auto it = m_shaderCache.find(cacheKey);
    if (it != m_shaderCache.end()) {
        return it->second;
    }

    // 构建着色器文件路径
    std::filesystem::path shaderPath = std::filesystem::path(Tina::Core::Engine::getExecutablePath()) / "bin" / "shaders";
    
    // 根据平台选择正确的着色器版本
    std::string platformDir;
    switch (bgfx::getRendererType()) {
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12:
            platformDir = "dx11";
            break;
        case bgfx::RendererType::OpenGL:
            platformDir = "glsl";
            break;
        case bgfx::RendererType::Metal:
            platformDir = "metal";
            break;
        case bgfx::RendererType::Vulkan:
            platformDir = "spirv";
            break;
        default:
            throw std::runtime_error("Unsupported renderer type");
    }
    
    shaderPath /= platformDir;
    shaderPath /= name + "." + type + ".bin";

    // 加载着色器二进制数据
    auto shaderData = loadShaderBinary(shaderPath.string());
    
    // 创建着色器
    const bgfx::Memory* mem = bgfx::copy(shaderData.data(), shaderData.size());
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    
    if (!bgfx::isValid(handle)) {
        throw std::runtime_error("Failed to create shader: " + name);
    }

    // 设置调试名称
    bgfx::setName(handle, (name + "_" + type).c_str());
    
    // 缓存着色器句柄
    m_shaderCache[cacheKey] = handle;
    
    return handle;
}

bgfx::ProgramHandle ShaderManager::createProgram(const std::string& name) {
    auto it = m_programCache.find(name);
    if (it != m_programCache.end()) {
        return it->second;
    }

    // 加载顶点和片段着色器
    bgfx::ShaderHandle vsh = loadShader(name, "vs");
    bgfx::ShaderHandle fsh = loadShader(name, "fs");

    // 创建着色器程序
    bgfx::ProgramHandle program = bgfx::createProgram(vsh, fsh, true);
    
    if (!bgfx::isValid(program)) {
        throw std::runtime_error("Failed to create shader program: " + name);
    }

    // 缓存程序句柄
    m_programCache[name] = program;
    
    return program;
}

void ShaderManager::shutdown() {
    // 销毁所有着色器程序
    for (const auto& pair : m_programCache) {
        bgfx::destroy(pair.second);
    }
    m_programCache.clear();

    // 销毁所有着色器
    for (const auto& pair : m_shaderCache) {
        bgfx::destroy(pair.second);
    }
    m_shaderCache.clear();
}

} // namespace Tina
