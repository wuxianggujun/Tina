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
    try {
        // 获取着色器目录路径
        #ifdef SHADER_OUTPUT_DIR
        std::filesystem::path shaderPath = SHADER_OUTPUT_DIR;
        #else
        std::filesystem::path shaderPath = std::filesystem::path(Core::Engine::getExecutablePath()) / "resources" / "shaders";
        #endif

        TINA_CORE_LOG_INFO("Loading shader from base path: {}", shaderPath.string());
        
        // 根据平台选择正确的着色器版本
        std::string platformDir;
        const auto rendererType = bgfx::getRendererType();
        TINA_CORE_LOG_INFO("Current renderer type: {}", bgfx::getRendererName(rendererType));

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
                platformDir = "dx11";
                break;
        }
        
        TINA_CORE_LOG_INFO("Using shader platform directory: {}", platformDir);
        shaderPath /= platformDir;
        
        // 修改文件名格式以匹配编译输出
        std::string filename = name + "." + type + ".bin";
        shaderPath /= filename;

        TINA_CORE_LOG_INFO("Attempting to load shader from: {}", shaderPath.string());

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
        
        TINA_CORE_LOG_INFO("Successfully loaded shader: {} ({})", name, type);
        return handle;
    } catch (const std::exception& e) {
        TINA_CORE_LOG_ERROR("Failed to load shader {} ({}): {}", name, type, e.what());
        throw;
    }
}

bgfx::ProgramHandle ShaderManager::createProgram(const std::string& name) {
    auto it = m_programs.find(name);
    if (it != m_programs.end()) {
        return it->second.program;
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

    // 存储program和其shaders
    m_programs[name] = {program, vsh, fsh};
    
    return program;
}

void ShaderManager::destroyProgram(bgfx::ProgramHandle handle) {
    if (!bgfx::isValid(handle)) {
        return;
    }

    // 查找并销毁program及其shaders
    for (auto it = m_programs.begin(); it != m_programs.end(); ++it) {
        if (it->second.program.idx == handle.idx) {
            const std::string& name = it->first;
            auto& resources = it->second;

            TINA_CORE_LOG_DEBUG("Destroying program and shaders: {}", name);
            
            // 销毁shaders
            if (bgfx::isValid(resources.vertexShader)) {
                bgfx::destroy(resources.vertexShader);
            }
            if (bgfx::isValid(resources.fragmentShader)) {
                bgfx::destroy(resources.fragmentShader);
            }
            
            // 销毁program
            bgfx::destroy(resources.program);
            
            m_programs.erase(it);
            return;
        }
    }
}

void ShaderManager::shutdown() {
    TINA_PROFILE_FUNCTION();
    
    if (m_isShutdown) {
        TINA_CORE_LOG_WARN("ShaderManager already shut down");
        return;
    }
    
    TINA_CORE_LOG_DEBUG("Destroying all shader programs");
    
    // 销毁所有programs及其shaders
    for (const auto& [name, resources] : m_programs) {
        if (bgfx::isValid(resources.program)) {
            TINA_CORE_LOG_DEBUG("Destroying program and shaders: {}", name);
            
            // 销毁shaders
            if (bgfx::isValid(resources.vertexShader)) {
                bgfx::destroy(resources.vertexShader);
            }
            if (bgfx::isValid(resources.fragmentShader)) {
                bgfx::destroy(resources.fragmentShader);
            }
            
            // 销毁program
            bgfx::destroy(resources.program);
        }
    }
    
    m_programs.clear();
    m_isShutdown = true;
    
    TINA_CORE_LOG_DEBUG("ShaderManager shutdown completed");
}

} // namespace Tina
