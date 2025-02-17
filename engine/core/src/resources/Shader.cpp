#include "tina/resources/Shader.hpp"
#include "tina/log/Logger.hpp"
#include <filesystem>
#include <fstream>
#include <vector>

namespace Tina {

bool Shader::reload() {
    ShaderLoader loader;
    return loader.reload(this);
}

std::shared_ptr<Resource> ShaderLoader::load(const std::string& path) {
    // 路径应该是一个JSON文件，包含顶点和片段着色器的路径
    auto vertexPath = path + ".vert";
    auto fragmentPath = path + ".frag";

    if (!std::filesystem::exists(vertexPath) || !std::filesystem::exists(fragmentPath)) {
        TINA_CORE_LOG_ERROR("Shader files not found: {}", path);
        return nullptr;
    }

    auto shader = std::make_shared<Shader>();
    if (loadShaderData(vertexPath, fragmentPath, shader.get())) {
        shader->m_vertexPath = vertexPath;
        shader->m_fragmentPath = fragmentPath;
        return shader;
    }
    return nullptr;
}

bool ShaderLoader::reload(Resource* resource) {
    auto* shader = dynamic_cast<Shader*>(resource);
    if (!shader) return false;

    // 保存旧的程序句柄
    auto oldProgram = shader->m_program;

    // 尝试加载新的着色器数据
    if (!loadShaderData(shader->m_vertexPath, shader->m_fragmentPath, shader)) {
        return false;
    }

    // 如果加载成功，销毁旧的程序
    if (bgfx::isValid(oldProgram)) {
        bgfx::destroy(oldProgram);
    }

    return true;
}

bool ShaderLoader::loadShaderData(const std::string& vertexPath, const std::string& fragmentPath, Shader* shader) {
    // 加载顶点着色器
    bgfx::ShaderHandle vsh = loadShaderFile(vertexPath, true);
    if (!bgfx::isValid(vsh)) {
        return false;
    }

    // 加载片段着色器
    bgfx::ShaderHandle fsh = loadShaderFile(fragmentPath, false);
    if (!bgfx::isValid(fsh)) {
        bgfx::destroy(vsh);
        return false;
    }

    // 创建着色器程序
    shader->m_program = bgfx::createProgram(vsh, fsh, true);
    if (!bgfx::isValid(shader->m_program)) {
        TINA_CORE_LOG_ERROR("Failed to create shader program: {} {}", vertexPath, fragmentPath);
        return false;
    }

    TINA_CORE_LOG_INFO("Loaded shader program: {} {}", vertexPath, fragmentPath);
    return true;
}

bgfx::ShaderHandle ShaderLoader::loadShaderFile(const std::string& path, bool isVertex) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        TINA_CORE_LOG_ERROR("Failed to open shader file: {}", path);
        return BGFX_INVALID_HANDLE;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) {
        TINA_CORE_LOG_ERROR("Failed to read shader file: {}", path);
        return BGFX_INVALID_HANDLE;
    }

    const bgfx::Memory* mem = bgfx::copy(buffer.data(), size);
    bgfx::ShaderHandle handle = bgfx::createShader(mem);

    if (!bgfx::isValid(handle)) {
        TINA_CORE_LOG_ERROR("Failed to create shader: {}", path);
        return BGFX_INVALID_HANDLE;
    }

    bgfx::setName(handle, path.c_str());
    return handle;
}

}
