#pragma once

#include "tina/resource/Resource.hpp"
#include <bgfx/bgfx.h>
#include <unordered_map>
#include <string>

namespace Tina {

/**
 * @brief 着色器资源类
 * 
 * 管理bgfx着色器程序和相关资源。
 * 支持uniform变量管理和热重载。
 */
class TINA_CORE_API ShaderResource : public Resource {
public:
    TINA_REGISTER_RESOURCE_TYPE(ShaderResource)

    /**
     * @brief 创建着色器资源
     * @param name 资源名称
     * @param path 资源路径
     * @return 着色器资源指针
     */
    static RefPtr<ShaderResource> create(const std::string& name, const std::string& path) {
        return RefPtr<ShaderResource>(new ShaderResource(name, path));
    }

    /**
     * @brief 获取着色器程序句柄
     * @return 着色器程序句柄
     */
    bgfx::ProgramHandle getProgram() const { return m_program; }

    /**
     * @brief 获取uniform变量句柄
     * @param name uniform变量名称
     * @param type uniform变量类型
     * @return uniform变量句柄
     */
    bgfx::UniformHandle getUniform(const std::string& name, bgfx::UniformType::Enum type);

    /**
     * @brief 设置uniform变量值
     * @param name uniform变量名称
     * @param value uniform变量值
     * @param type uniform变量类型
     */
    template<typename T>
    void setUniform(const std::string& name, const T& value, bgfx::UniformType::Enum type) {
        auto handle = getUniform(name, type);
        if (bgfx::isValid(handle)) {
            bgfx::setUniform(handle, &value);
        }
    }

protected:
    ShaderResource(const std::string& name, const std::string& path);
    ~ShaderResource();

    bool load() override;
    void unload() override;

private:
    // 着色器程序句柄
    bgfx::ProgramHandle m_program{BGFX_INVALID_HANDLE};

    // uniform变量缓存
    struct UniformInfo {
        bgfx::UniformHandle handle;
        bgfx::UniformType::Enum type;
    };
    std::unordered_map<std::string, UniformInfo> m_uniforms;

    friend class ShaderLoader;
};

} // namespace Tina 