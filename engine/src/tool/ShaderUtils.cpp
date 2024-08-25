#include "ShaderUtils.hpp"
#include <filesystem>


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
        std::string arguments = "-f shaders/"+ name+".sc" + " -o " + endFile +
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
}
