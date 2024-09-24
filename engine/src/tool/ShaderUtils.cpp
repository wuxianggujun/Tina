#include "ShaderUtils.hpp"
#include "filesystem/FileSystem.hpp"
#include "bimg/bimg.h"
#include <sstream>
#include <cstdio>
#include <stdexcept>

#include "core/Platform.hpp"

namespace Tina {
    /*
    bool ShaderUtils::compileShader(std::string &name) {
        if (name.empty()) {
            throw std::invalid_argument("Shader name cannot be empty");
        }

        ghc::filesystem::path shaderCFilePath = "./shaderc"; // 假设 shaderc 在同一目录下
        if (!ghc::filesystem::exists(shaderCFilePath)) {
            throw std::runtime_error("shaderc executable does not exist");
        }

        // 构建 shader 源文件路径
        ghc::filesystem::path shaderSourcePath = "shaders/" + name + ".sc";
        if (!ghc::filesystem::exists(shaderSourcePath)) {
            throw std::runtime_error("Shader source file does not exist: " + shaderSourcePath.string());
        }

        // 构建输出文件路径
        ghc::filesystem::path outputFilePath = "shaders/" + name + ".bin";

        // 构建 shaderc 编译命令
        std::string arguments = "-f " + shaderSourcePath.string() + " -o " + outputFilePath.string() +
                                " --stdout --varyingdef shaders/varying.def.sc --platform linux -p s_5_0 -O 3 --type vertex --verbose -i ../../dependencies/bgfx.cmake/bgfx/src";

        std::ostringstream command;
        command << shaderCFilePath.string() << " " << arguments;

        // 使用 popen 调用 shaderc
#ifdef  TINA_PLATFORM_LINUX
        FILE *pipe = popen(command.str().c_str(), "r");
#elif  TINA_PLATFORM_WINDOWS
        FILE *pipe = _popen(command.str().c_str(), "r");
#endif

        if (!pipe) {
            throw std::runtime_error("Failed to run shaderc command: " + command.str());
        }

        // 捕获编译输出
        std::ostringstream output;
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), fmt::pipe) != nullptr) {
            output << buffer;
        }


#ifdef  TINA_PLATFORM_LINUX
        // 检查 shaderc 编译器是否成功
        if (pclose(pipe) != 0) {
            throw std::runtime_error("Shader compilation failed: " + output.str());
        }
#elif  TINA_PLATFORM_WINDOWS
        // 检查 shaderc 编译器是否成功
        if (_pclose(pipe) != 0) {
            throw std::runtime_error("Shader compilation failed: " + output.str());
        }

#endif

        
      
        std::printf("Shader compiled successfully: %s\n", outputFilePath.string().c_str());
        return true;
    }

    */
    
    /*bgfx::TextureHandle ShaderUtils::loadPng(const char *fileName, uint64_t state) {
        bx::FileReader fFileReader;
        bx::FilePath fFilePath(fileName);
        if (!fFileReader.open(fFilePath,bx::ErrorAssert())) {
            printf("Failed to load %s.fsb: %s\n", fileName);
        }
        const bgfx::Memory *memory = loadMemory(&fFileReader, fileName);
     auto image = bimg::imageParse(bx::alloc(1024),memory->data,memory->size,bimg::TextureFormat::Enum(bgfx::TextureFormat::Count), nullptr);
        
        
    }*/

    bool ShaderUtils::isPngFile(std::string &fileName) {
        for (auto &c: fileName) c = static_cast<char>(std::tolower(c));
        return fileName.rfind(".png") != std::string::npos;
    }

    const bgfx::Memory *ShaderUtils::loadMemory(bx::FileReaderI *_render, const char *_filePath) {
        if (bx::open(_render, _filePath)) {
            const uint32_t size = bx::getSize(_render);
            const bgfx::Memory *memory = bgfx::alloc(size + 1);
            bx::read(_render, memory->data, size, bx::ErrorAssert());
            bx::close(_render);
            memory->data[memory->size - 1] = '\0';
            return memory;
        }
        throw std::runtime_error("Failed to load memory");
    }
}
