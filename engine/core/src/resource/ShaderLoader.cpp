#include "tina/resource/ShaderLoader.hpp"
#include "tina/log/Log.hpp"
#include "tina/core/filesystem.hpp"
#include <fstream>
#include <fmt/format.h>

namespace Tina {

bool ShaderLoader::loadSync(Resource* resource, const ResourceLoadProgressCallback& progressCallback) {
    if (!resource) {
        TINA_ENGINE_ERROR("Invalid resource pointer");
        return false;
    }

    auto* shaderResource = dynamic_cast<ShaderResource*>(resource);
    if (!shaderResource) {
        TINA_ENGINE_ERROR("Resource is not a ShaderResource");
        return false;
    }

    try {
        // 更新资源状态
        updateResourceState(resource, ResourceState::Loading);
        if (progressCallback) {
            progressCallback(0.0f, "开始加载着色器");
        }

        // 构建着色器文件路径
        std::filesystem::path sourcePath(resource->getPath());
        std::filesystem::path shaderName(resource->getName());  // 获取着色器名称
        
        // 根据渲染器类型选择着色器目录
        std::string rendererDir;
        switch (bgfx::getRendererType())
        {
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
            return false;
        }

        std::filesystem::path vsPath = sourcePath / rendererDir / 
            (shaderName.string() + "_vs.bin");
        std::filesystem::path fsPath = sourcePath / rendererDir / 
            (shaderName.string() + "_fs.bin");

        TINA_ENGINE_DEBUG("Loading shader binaries from:\n  VS: {}\n  FS: {}", 
            vsPath.string(), fsPath.string());

        // 加载着色器二进制数据
        if (progressCallback) {
            progressCallback(0.2f, "加载顶点着色器");
        }
        auto vsData = loadShaderBinary(vsPath);

        if (progressCallback) {
            progressCallback(0.4f, "加载片段着色器");
        }
        auto fsData = loadShaderBinary(fsPath);

        if (vsData.empty() || fsData.empty()) {
            updateResourceState(resource, ResourceState::Failed);
            return false;
        }

        // 创建着色器程序
        if (progressCallback) {
            progressCallback(0.6f, "创建着色器程序");
        }
        bgfx::ProgramHandle program = createShaderProgram(vsData, fsData);

        if (!bgfx::isValid(program)) {
            updateResourceState(resource, ResourceState::Failed);
            return false;
        }

        // 更新资源
        if (progressCallback) {
            progressCallback(0.8f, "更新资源状态");
        }
        shaderResource->m_program = program;
        updateResourceState(resource, ResourceState::Loaded);
        updateResourceSize(resource, vsData.size() + fsData.size());

        if (progressCallback) {
            progressCallback(1.0f, "着色器加载完成");
        }

        TINA_ENGINE_INFO("Successfully loaded shader '{}'", resource->getName());
        return true;
    }
    catch (const std::exception& e) {
        TINA_ENGINE_ERROR("Failed to load shader '{}': {}", resource->getName(), e.what());
        updateResourceState(resource, ResourceState::Failed);
        return false;
    }
}

std::future<bool> ShaderLoader::loadAsync(Resource* resource, 
    const ResourceLoadProgressCallback& progressCallback) {
    return std::async(std::launch::async, [this, resource, progressCallback]() {
        return loadSync(resource, progressCallback);
    });
}

void ShaderLoader::unload(Resource* resource) {
    if (!resource) return;

    auto* shaderResource = dynamic_cast<ShaderResource*>(resource);
    if (shaderResource) {
        shaderResource->unload();
    }
}

bool ShaderLoader::validate(Resource* resource) {
    if (!resource) return false;
    auto* shaderResource = dynamic_cast<ShaderResource*>(resource);
    return shaderResource && bgfx::isValid(shaderResource->getProgram());
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
        filePath.string(), size);
    return data;
}

bgfx::ProgramHandle ShaderLoader::createShaderProgram(
    const std::vector<uint8_t>& vsData, const std::vector<uint8_t>& fsData) {
    // 创建着色器
    bgfx::ShaderHandle vs = bgfx::createShader(
        bgfx::copy(vsData.data(), static_cast<uint32_t>(vsData.size())));
    bgfx::ShaderHandle fs = bgfx::createShader(
        bgfx::copy(fsData.data(), static_cast<uint32_t>(fsData.size())));

    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        TINA_ENGINE_ERROR("Failed to create shader");
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

    return program;
}

} // namespace Tina 