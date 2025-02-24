#include "tina/resource/ShaderLoader.hpp"
#include "tina/log/Log.hpp"
#include "tina/core/filesystem.hpp"
#include <fstream>
#include <fmt/format.h>

namespace Tina {

bool ShaderLoader::loadSync(Resource* resource, const ResourceLoadProgressCallback& progressCallback) {
    try {
        auto* shaderResource = dynamic_cast<ShaderResource*>(resource);
        if (!shaderResource) {
            TINA_ENGINE_ERROR("Invalid resource type");
            return false;
        }

        TINA_ENGINE_DEBUG("Starting shader load for '{}', current refCount: {}", 
            resource->getName(), shaderResource->getRefCount());

        // 1. 设置加载状态
        shaderResource->m_state = ResourceState::Loading;
        shaderResource->m_program = BGFX_INVALID_HANDLE;

        if (progressCallback) {
            progressCallback(0.0f, "开始加载着色器");
        }

        // 2. 加载着色器文件
        auto shaderPath = std::filesystem::path(resource->getPath());
        auto vsPath = shaderPath / getShaderPath(resource->getName() + "_vs");
        auto fsPath = shaderPath / getShaderPath(resource->getName() + "_fs");

        TINA_ENGINE_DEBUG("Loading shader binaries from:\n  VS: {}\n  FS: {}", 
            vsPath.string(), fsPath.string());

        if (progressCallback) {
            progressCallback(0.2f, "读取顶点着色器");
        }

        // 加载顶点着色器
        std::vector<uint8_t> vsData;
        try {
            vsData = loadShaderBinary(vsPath);
            TINA_ENGINE_DEBUG("Vertex shader loaded successfully, size: {} bytes", vsData.size());
        } catch (const std::exception& e) {
            TINA_ENGINE_ERROR("Failed to load vertex shader: {}", e.what());
            shaderResource->m_state = ResourceState::Failed;
            return false;
        }

        if (progressCallback) {
            progressCallback(0.4f, "读取片段着色器");
        }

        // 加载片段着色器
        std::vector<uint8_t> fsData;
        try {
            fsData = loadShaderBinary(fsPath);
            TINA_ENGINE_DEBUG("Fragment shader loaded successfully, size: {} bytes", fsData.size());
        } catch (const std::exception& e) {
            TINA_ENGINE_ERROR("Failed to load fragment shader: {}", e.what());
            shaderResource->m_state = ResourceState::Failed;
            return false;
        }

        if (progressCallback) {
            progressCallback(0.6f, "创建着色器程序");
        }

        // 3. 创建bgfx着色器程序
        bgfx::ProgramHandle program = createShaderProgram(vsData, fsData);
        if (!bgfx::isValid(program)) {
            TINA_ENGINE_ERROR("Failed to create shader program");
            shaderResource->m_state = ResourceState::Failed;
            return false;
        }

        TINA_ENGINE_DEBUG("Shader program created successfully, handle: {}", program.idx);

        // 4. 更新资源状态
        if (progressCallback) {
            progressCallback(0.8f, "更新资源状态");
        }

        // 更新资源大小
        updateResourceSize(resource, vsData.size() + fsData.size());
        
        // 设置程序句柄
        shaderResource->m_program = program;
        
        // 设置加载状态
        shaderResource->m_state = ResourceState::Loaded;

        if (progressCallback) {
            progressCallback(1.0f, "着色器加载完成");
        }

        TINA_ENGINE_INFO("Successfully loaded shader '{}', refCount: {}, program: {}", 
            resource->getName(), shaderResource->getRefCount(), program.idx);
        return true;
    }
    catch (const std::exception& e) {
        TINA_ENGINE_ERROR("Failed to load shader '{}': {}", resource->getName(), e.what());
        auto* shaderResource = dynamic_cast<ShaderResource*>(resource);
        if (shaderResource) {
            shaderResource->m_state = ResourceState::Failed;
        }
        return false;
    }
}

std::future<bool> ShaderLoader::loadAsync(Resource* resource, const ResourceLoadProgressCallback& progressCallback) {
    return std::async(std::launch::async, [this, resource, progressCallback]() {
        return loadSync(resource, progressCallback);
    });
}

void ShaderLoader::unload(Resource* resource) {
    if (auto* shaderResource = dynamic_cast<ShaderResource*>(resource)) {
        TINA_ENGINE_DEBUG("ShaderLoader::unload called for '{}', refCount: {}", 
            resource->getName(), shaderResource->getRefCount());
        shaderResource->unload();
    }
}

bool ShaderLoader::validate(Resource* resource) {
    auto* shaderResource = dynamic_cast<ShaderResource*>(resource);
    bool valid = shaderResource && bgfx::isValid(shaderResource->m_program);
    TINA_ENGINE_DEBUG("ShaderLoader::validate for '{}': {}, refCount: {}", 
        resource->getName(), valid, shaderResource ? shaderResource->getRefCount() : 0);
    return valid;
}

std::vector<uint8_t> ShaderLoader::loadShaderBinary(const std::filesystem::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        TINA_ENGINE_ERROR("Failed to open shader binary file: {}", filePath.string());
        return {};
    }

    // 获取文件大小
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    // 读取文件内容
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    if (!file) {
        TINA_ENGINE_ERROR("Failed to read shader binary file: {}", filePath.string());
        return {};
    }

    TINA_ENGINE_DEBUG("Successfully loaded shader binary: {} ({} bytes)", 
        filePath.generic_string(), size);
    return data;
}

bgfx::ProgramHandle ShaderLoader::createShaderProgram(
    const std::vector<uint8_t>& vsData, const std::vector<uint8_t>& fsData) {
    TINA_ENGINE_DEBUG("Creating shader program - VS size: {}, FS size: {}", 
        vsData.size(), fsData.size());

    // 创建着色器
    bgfx::ShaderHandle vs = bgfx::createShader(
        bgfx::copy(vsData.data(), static_cast<uint32_t>(vsData.size())));
    bgfx::ShaderHandle fs = bgfx::createShader(
        bgfx::copy(fsData.data(), static_cast<uint32_t>(fsData.size())));

    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        TINA_ENGINE_ERROR("Failed to create shader handles - VS valid: {}, FS valid: {}", 
            bgfx::isValid(vs), bgfx::isValid(fs));
        if (bgfx::isValid(vs)) bgfx::destroy(vs);
        if (bgfx::isValid(fs)) bgfx::destroy(fs);
        return BGFX_INVALID_HANDLE;
    }

    // 创建着色器程序
    bgfx::ProgramHandle program = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(program)) {
        TINA_ENGINE_ERROR("Failed to create program");
        return BGFX_INVALID_HANDLE;
    }

    TINA_ENGINE_DEBUG("Successfully created shader program: {}", program.idx);
    return program;
}

std::filesystem::path ShaderLoader::getShaderPath(const std::string& shaderName) const {
    // 根据渲染器类型选择着色器目录
    std::string rendererDir;
    switch (bgfx::getRendererType()) {
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12: 
            rendererDir = "dx11";
            break;
        case bgfx::RendererType::OpenGL:     
            rendererDir = "glsl";
            break;
        case bgfx::RendererType::OpenGLES:   
            rendererDir = "essl";
            break;
        case bgfx::RendererType::Vulkan:     
            rendererDir = "spirv";
            break;
        case bgfx::RendererType::Metal:      
            rendererDir = "metal";
            break;
        default:
            TINA_ENGINE_ERROR("Unsupported renderer type: {}", bgfx::getRendererName(bgfx::getRendererType()));
            return {};
    }

    auto path = std::filesystem::path(rendererDir) / (shaderName + ".bin");
    TINA_ENGINE_DEBUG("Generated shader path: {}", path.string());
    return path;
}

void ShaderLoader::updateResourceSize(Resource* resource, size_t size) {
    if (auto* shaderResource = dynamic_cast<ShaderResource*>(resource)) {
        shaderResource->m_size = size;
        TINA_ENGINE_DEBUG("Updated resource size for '{}': {} bytes", resource->getName(), size);
    }
}

} // namespace Tina