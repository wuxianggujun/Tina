#include "ShaderManager.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <SDL3/SDL.h>
#include "core/Log.hpp"

namespace Tina {
namespace Renderer {

ShaderManager::~ShaderManager() {
    cleanup();
}

void ShaderManager::initialize(const std::string& shaderRootPath) {
    namespace fs = std::filesystem;
    fs::path p(shaderRootPath);
    if (!p.is_absolute()) {
        const char* base = SDL_GetBasePath();
        if (base) {
            fs::path basep(base);
            SDL_free((void*)base);
            p = basep / p;
        }
    }
    m_shaderRootPath = p.lexically_normal().string();
    TINA_INFO("ShaderManager 初始化，根路径: {}", m_shaderRootPath);
}

bgfx::ProgramHandle ShaderManager::loadProgram(const std::string& vertexShaderName, 
                                              const std::string& fragmentShaderName) {
    Tina::Container::String programKey = (vertexShaderName + "+" + fragmentShaderName).c_str();
    
    // 检查是否已经加载过
    auto it = m_loadedPrograms.find(programKey);
    if (it != m_loadedPrograms.end()) {
        // 防御：句柄可能在其他场景被销毁（例如暂停场景清理），此处检测并自动重建
        if (bgfx::isValid(it->second)) {
            return it->second;
        }
        // 句柄已失效，移除缓存，走重建流程
        m_loadedPrograms.erase(it);
        TINA_WARN("ShaderManager: 程序已失效，重新加载: {}", programKey.c_str());
    }

    // 加载顶点和片段着色器
    bgfx::ShaderHandle vsh = loadShader(vertexShaderName, Stage::Vertex);
    bgfx::ShaderHandle fsh = loadShader(fragmentShaderName, Stage::Fragment);

    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
        TINA_ERROR("创建程序失败：着色器加载失败 {} + {}", vertexShaderName, fragmentShaderName);
        return BGFX_INVALID_HANDLE;
    }

    // 创建程序
    // 程序负责销毁着色器（简化生命周期管理）
    bgfx::ProgramHandle program = bgfx::createProgram(vsh, fsh, true);
    
    if (bgfx::isValid(program)) {
        m_loadedPrograms.emplace(programKey, program);
        TINA_INFO("加载着色器程序成功: {}", programKey);
    } else {
        TINA_ERROR("创建着色器程序失败: {}", programKey);
    }

    return program;
}

bgfx::ShaderHandle ShaderManager::loadShader(const std::string& shaderName, Stage stage) {
    // 构建文件路径
    std::string filePath = buildShaderPath(shaderName, stage);
    
    // 加载文件数据
    Tina::Container::Vector<uint8_t> shaderData = loadShaderFile(filePath);
    if (shaderData.empty()) {
        TINA_ERROR("加载着色器文件失败: {}", filePath);
        return BGFX_INVALID_HANDLE;
    }

    // 创建着色器
    const bgfx::Memory* mem = bgfx::copy(shaderData.data(), static_cast<uint32_t>(shaderData.size()));
    bgfx::ShaderHandle shader = bgfx::createShader(mem);
    
    if (bgfx::isValid(shader)) {
        TINA_INFO("加载着色器成功: {} ({} bytes)", filePath, (int)shaderData.size());
    } else {
        TINA_ERROR("从文件创建着色器失败: {}", filePath);
    }

    return shader;
}

void ShaderManager::cleanup() {
    // 清理程序（程序销毁时会自动销毁附属的着色器，因为 createProgram 传入了 destroyShaders=true）
    for (auto& pair : m_loadedPrograms) {
        if (bgfx::isValid(pair.second)) {
            bgfx::destroy(pair.second);
        }
    }
    m_loadedPrograms.clear();

    TINA_INFO("ShaderManager 清理完成");
}

std::string ShaderManager::getShaderDirectoryForCurrentRenderer() const {
    bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    
    switch (rendererType) {
        case bgfx::RendererType::OpenGL:
            return "glsl";
        case bgfx::RendererType::OpenGLES:
            return "essl";
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12:
            return "dx11";
        case bgfx::RendererType::Vulkan:
            return "spv"; // 与 CMake 输出目录一致
        case bgfx::RendererType::Metal:
#ifdef __APPLE__
            return "metal";
#else
            return "dx11"; // 非 Apple 平台退回 dx11
#endif
        default:
            TINA_WARN("未知的渲染后端，回退 glsl");
            return "glsl";
    }
}

Tina::Container::Vector<uint8_t> ShaderManager::loadShaderFile(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        TINA_ERROR("无法打开着色器文件: {}", filePath);
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size <= 0) {
        TINA_ERROR("着色器文件大小异常: {} (size={})", filePath, (long long)size);
        return {};
    }

    Tina::Container::Vector<uint8_t> buffer((size_t)size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        TINA_ERROR("读取着色器文件失败: {}", filePath);
        return {};
    }

    return buffer;
}

std::string ShaderManager::buildShaderPath(const std::string& shaderName, Stage stage) const {
    namespace fs = std::filesystem;
    const std::string rendererDir = getShaderDirectoryForCurrentRenderer();
    const std::string typeStr = (stage == Stage::Vertex) ? "vs" : "fs";

    // 兼容多种命名：
    // 1) <name>_<vs|fs>.sc.bin
    // 2) <vs|fs>_<name>.sc.bin
    // 3) <name>_<vs|fs>.bin
    // 4) <vs|fs>_<name>.bin
    const Tina::Container::Vector<std::string> candidates = {
        shaderName + "_" + typeStr + ".sc.bin",
        typeStr + "_" + shaderName + ".sc.bin",
        shaderName + "_" + typeStr + ".bin",
        typeStr + "_" + shaderName + ".bin",
    };

    for (const auto& fn : candidates) {
        fs::path p = fs::path(m_shaderRootPath) / rendererDir / fn;
        if (fs::exists(p)) return p.string();
    }

    // 默认返回第一候选（便于日志提示）
    fs::path fallback = fs::path(m_shaderRootPath) / rendererDir / candidates.front();
    return fallback.string();
}

} // namespace Renderer
} // namespace Tina
