#include "tina/renderer/ShaderManager.hpp"
#include "tina/log/Logger.hpp"
#include <fstream>
#include <vector>
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

    // 获取着色器目录路径
    #ifdef SHADER_OUTPUT_DIR
    std::filesystem::path shaderPath = SHADER_OUTPUT_DIR;
    #else
    std::filesystem::path shaderPath = std::filesystem::path(Core::Engine::getExecutablePath()) / "resources" / "shaders";
    #endif

    TINA_LOG_INFO("Loading shader from base path: {}", shaderPath.string());
    
    // 根据平台选择正确的着色器版本
    std::string platformDir;
    const auto rendererType = bgfx::getRendererType();
    TINA_LOG_INFO("Current renderer type: {}", bgfx::getRendererName(rendererType));

    switch (rendererType) {
        case bgfx::RendererType::Direct3D11:
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
        case bgfx::RendererType::Noop:
            platformDir = "null";
            break;
        default:
            // 对于其他类型，使用 dx11 作为默认值
            platformDir = "dx11";
            break;
    }
    
    TINA_LOG_INFO("Using shader platform directory: {}", platformDir);
    shaderPath /= platformDir;
    
    // 修改文件名格式以匹配编译输出
    std::string filename = name + "." + type + ".bin";
    shaderPath /= filename;

    TINA_LOG_INFO("Attempting to load shader from: {}", shaderPath.string());

    try {
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
        
        TINA_LOG_INFO("Successfully loaded shader: {} ({})", name, type);
        return handle;
    } catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to load shader {} ({}): {}", name, type, e.what());
        throw;
    }
}

bgfx::ProgramHandle ShaderManager::createProgram(const std::string& name) {
    auto it = m_programCache.find(name);
    if (it != m_programCache.end()) {
        return it->second;
    }

    // 加载顶点和片段着色器
    const bgfx::ShaderHandle vsh = loadShader(name, "vs");
    const bgfx::ShaderHandle fsh = loadShader(name, "fs");

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
