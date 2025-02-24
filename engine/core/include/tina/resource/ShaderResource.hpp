#pragma once

#include "tina/resource/Resource.hpp"
#include "tina/core/RefPtr.hpp"
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
    static RefPtr<ShaderResource> create(const std::string& name, const std::string& path);

    ~ShaderResource() override;

    /**
     * @brief 检查着色器是否已加载
     * @return 如果着色器已加载且程序句柄有效则返回true
     */
    bool isLoaded() const override;

    /**
     * @brief 获取着色器程序句柄
     * @return 着色器程序句柄
     */
    bgfx::ProgramHandle getProgram() const;

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
    void setUniform(const std::string& name, const T& value, bgfx::UniformType::Enum type);

protected:
    ShaderResource(const std::string& name, const std::string& path);

    bool load() override;
    void unload() override;

private:
    // 着色器程序句柄
    bgfx::ProgramHandle m_program{BGFX_INVALID_HANDLE};

    // 加载状态标志
    bool m_loaded{false};

    // uniform变量缓存
    struct UniformInfo {
        bgfx::UniformHandle handle{BGFX_INVALID_HANDLE};
        bgfx::UniformType::Enum type;
    };
    std::unordered_map<std::string, UniformInfo> m_uniforms;

    friend class ShaderLoader;
};

} // namespace Tina 