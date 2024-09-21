#include "ShaderUtils.hpp"
#include <filesystem>

#include "bimg/bimg.h"


namespace Tina {
    bool ShaderUtils::compileShader(std::string &name) {
        bool flag = true;
        std::filesystem::path shaderCFilePath = "shaderc.exe";
        if (!std::filesystem::exists(shaderCFilePath) || name.empty()) {
            return false;
        }

        // std::string rp = flag ? "spirv" : "d3d";
        // std::string rcmd = flag ? " -p spirv" : " -p s_5_0 -o 3";
        std::string rcmd = " -p s_5_0 -O 3 --type vertex --verbose  -i ../../dependencies/bgfx.cmake/bgfx/src";
        // std::string rcmd = " -p vs_5_0 --type vertex --verbose  -i ../../dependencies/bgfx.cmake/bgfx/src";


        std::string endFile = "shaders/" + name + ".bin";
        std::string arguments = "-f shaders/" + name + ".sc" + " -o " + endFile +
                                " --stdout --varyingdef shaders/varying.def.sc  --platform windows" + rcmd;

        std::ostringstream command;
        command << "shaderc.exe " << arguments;

        FILE *pipe = _popen(command.str().c_str(), "r");
        if (!pipe) {
            printf("Failed to run command: %s\n", command.str().c_str());
            return false;
        }
        std::ostringstream output;
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output << buffer;
        }
        if (_pclose(pipe) == -1) {
            printf("Failed to close command: %s\n", command.str().c_str());
            return false;
        }
        printf("%s", output.str().c_str());
        return true;
    }

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
