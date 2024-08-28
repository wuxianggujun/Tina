#ifndef TINA_TOOL_SHADERCOMPILE_HPP
#define TINA_TOOL_SHADERCOMPILE_HPP

#include <string>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/file.h>

namespace Tina {

    class ShaderUtils {
        
    public:

        enum ShaderType {
            
        };
        
        static bool compileShader(std::string& name);
        static bgfx::TextureHandle loadPng(const char* fileName,uint64_t state);
        static bool isPngFile(std::string& fileName);
        static const bgfx::Memory* loadMemory(bx::FileReaderI* _render,const char* _filePath);
    };
    
}

#endif
