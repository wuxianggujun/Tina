//
// Created by wuxianggujun on 2024/5/20.
//

#include "BgfxUtils.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include <bgfx/bgfx.h>
#include <bx/commandline.h>
#include <bx/endian.h>
#include <bx/math.h>
#include <bx/readerwriter.h>
#include <bx/string.h>

namespace Tina {

    static std::vector<std::shared_ptr<char[]>> loadedFiles;

    static const bgfx::Memory *loadFile(const std::string &_filePath) {
        auto filePath = std::filesystem::path(_filePath);

        if (filePath.is_relative()) {
            filePath = std::filesystem::current_path() / filePath;
        }

        if (std::filesystem::is_symlink(filePath)) {
            filePath = std::filesystem::read_symlink(filePath);
        }

        std::ifstream is(filePath, std::ios::binary);
        if (!is) {
            std::cout << "Invalid filepath: " << _filePath << std::endl;
            return NULL;
        }

        is.seekg(0, std::ios::end);
        uint32_t size = is.tellg();
        is.seekg(0, std::ios::beg);

        std::shared_ptr<char[]> buffer{new char[size]};
        is.read(buffer.get(), size);

        loadedFiles.push_back(buffer);

        return bgfx::makeRef(buffer.get(), size);
    }

   std::string BgfxUtils::getFilepath(const std::string &_from_resources) {
        std::string filepath = "./resources/" + _from_resources;

        return filepath;
    }

    bgfx::ShaderHandle BgfxUtils::loadShader(const std::string &_name) {
        std::string shaderPath = "???";

        switch (bgfx::getRendererType()) {
            case bgfx::RendererType::Noop:
            case bgfx::RendererType::Direct3D11:
            case bgfx::RendererType::Direct3D12:
                shaderPath = "shaders/dx11/";
                break;
            case bgfx::RendererType::Gnm:
                shaderPath = "shaders/pssl/";
                break;
            case bgfx::RendererType::Metal:
                shaderPath = "shaders/metal/";
                break;
            case bgfx::RendererType::Nvn:
                shaderPath = "shaders/nvn/";
                break;
            case bgfx::RendererType::OpenGL:
                shaderPath = "shaders/glsl/";
                break;
            case bgfx::RendererType::OpenGLES:
                shaderPath = "shaders/essl/";
                break;
            case bgfx::RendererType::Vulkan:
                shaderPath = "shaders/spirv/";
                break;
                break;
            case bgfx::RendererType::Agc:
            case bgfx::RendererType::Count:
                BX_ASSERT(false, "You should not be here!");
                break;
        }

        std::string filepath = getFilepath(shaderPath + _name + ".bin");

        bgfx::ShaderHandle handle = bgfx::createShader(loadFile(filepath));
        bgfx::setName(handle, _name.c_str());

        return handle;
    }

    bgfx::ProgramHandle BgfxUtils::loadProgram(const std::string &_vsName, const std::string &_fsName) {
        bgfx::ShaderHandle vsh = loadShader(_vsName);
        bgfx::ShaderHandle fsh = BGFX_INVALID_HANDLE;
        if (!_fsName.empty()) {
            fsh = loadShader(_fsName);
        }

        return bgfx::createProgram(vsh, fsh, true /* destroy shaders when program is destroyed */);
    }

    bgfx::TextureHandle BgfxUtils::loadTexture(const std::string &_name, uint64_t _flags, uint8_t _skip,
                                               bgfx::TextureInfo *_info, bimg::Orientation::Enum *_orientation) {
        std::string filepath = BgfxUtils::getFilepath("textures/" + _name + ".dds");

        auto data_mem = loadFile(filepath);
        return bgfx::createTexture(
                data_mem,
                _flags,
                _skip,
                _info
        );
    }


} // Tina