#pragma once

#include <bgfx/bgfx.h>
#include <string>
#include "tina/log/Logger.hpp"
#include <bx/filepath.h>

namespace Tina::Utils
{
    struct TextureLoadResult {
        bgfx::TextureHandle handle;
        uint16_t width;
        uint16_t height;
        uint16_t depth;
        bool hasMips;
        uint16_t layers;
        bgfx::TextureFormat::Enum format;
    };

    class BgfxUtils
    {
    public:
        /**
         * @brief 加载文件内容到内存
         * @param filePath 文件路径
         * @param size 可选的输出参数，用于获取加载的数据大小
         * @return bgfx::Memory* 指向加载的内存的指针，如果加载失败则返回nullptr
         */
        static const bgfx::Memory* loadMem(const std::string& filePath);

        /**
         * @brief 加载着色器
         * @param name 着色器名称
         * @return bgfx::ShaderHandle 着色器句柄
         */
        static bgfx::ShaderHandle loadShader(const std::string& name);

        /**
         * @brief 加载着色器程序
         * @param vsName 顶点着色器名称
         * @param fsName 片段着色器名称
         * @return bgfx::ProgramHandle 程序句柄
         */
        static bgfx::ProgramHandle loadProgram(const std::string& vsName, const std::string& fsName);

        /**
         * @brief 加载纹理
         * @param filePath 纹理文件路径
         * @param flags 纹理标志
         * @return TextureLoadResult 包含纹理句柄和信息的结构体
         */
        static TextureLoadResult loadTexture(const std::string& filePath, 
            uint64_t flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE);
    };
} // namespace Tina::Utils 