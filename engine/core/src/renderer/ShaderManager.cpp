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

    // 创建着色器程序 - 不自动销毁shaders
    bgfx::ProgramHandle program = bgfx::createProgram(vsh, fsh, false);
    if (!bgfx::isValid(program)) {
        // 如果创建失败,手动清理shaders
        if (bgfx::isValid(vsh)) bgfx::destroy(vsh);
        if (bgfx::isValid(fsh)) bgfx::destroy(fsh);
        throw std::runtime_error("Failed to create shader program: " + name);
    }

    // 缓存程序
    m_programCache[name] = program;
    
    // 缓存着色器和程序的关系
    m_programShaders[name] = {vsh, fsh};
    
    return program;
}

void ShaderManager::destroyProgram(bgfx::ProgramHandle handle) {
    if (!bgfx::isValid(handle)) {
        return;
    }

    // 从缓存中移除
    for (auto it = m_programCache.begin(); it != m_programCache.end(); ++it) {
        if (it->second.idx == handle.idx) {
            std::string name = it->first;  // 复制name而不是引用,因为我们要erase迭代器
            TINA_LOG_DEBUG("Destroying shader program: {}", name);
            
            // 销毁关联的shaders
            auto shaderIt = m_programShaders.find(name);
            if (shaderIt != m_programShaders.end()) {
                const auto& shaders = shaderIt->second;
                
                // 从shader缓存中移除
                std::string vsKey = name + "_vs";
                std::string fsKey = name + "_fs";
                
                if (bgfx::isValid(shaders.vertex)) {
                    TINA_LOG_DEBUG("Destroying vertex shader for program: {}", name);
                    bgfx::destroy(shaders.vertex);
                    m_shaderCache.erase(vsKey);
                }
                
                if (bgfx::isValid(shaders.fragment)) {
                    TINA_LOG_DEBUG("Destroying fragment shader for program: {}", name);
                    bgfx::destroy(shaders.fragment);
                    m_shaderCache.erase(fsKey);
                }
                
                m_programShaders.erase(shaderIt);
            }
            
            // 最后销毁program
            bgfx::destroy(handle);
            m_programCache.erase(it);
            return;
        }
    }
}

void ShaderManager::shutdown() {
    TINA_PROFILE_FUNCTION();
    
    if (m_isShutdown) {
        TINA_LOG_WARN("ShaderManager already shut down");
        return;
    }
    
    // 销毁所有着色器程序和其关联的shaders
    TINA_LOG_DEBUG("Destroying shader programs and associated shaders");
    
    // 使用一个临时vector存储所有需要销毁的programs
    std::vector<bgfx::ProgramHandle> programsToDestroy;
    for (const auto& pair : m_programCache) {
        if (bgfx::isValid(pair.second)) {
            programsToDestroy.push_back(pair.second);
        }
    }
    
    // 使用destroyProgram销毁每个program及其关联的shaders
    for (const auto& program : programsToDestroy) {
        destroyProgram(program);
    }
    
    // 确保所有缓存都被清理
    m_programCache.clear();
    m_programShaders.clear();
    m_shaderCache.clear();
    
    m_isShutdown = true;
    TINA_LOG_DEBUG("ShaderManager shutdown completed");
}

} // namespace Tina
