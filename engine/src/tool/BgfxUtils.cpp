#include "BgfxUtils.hpp"

#include <format>
#include <iostream>
#include <stdexcept>
#include <fstream>


namespace Tina::BgfxUtils {
    void *allocate(size_t size) {
        return bx::alloc(getAllocator(), size);
    }

    void unLoad(void *ptr) {
        if (nullptr != _allocator) {
            bx::free(_allocator, ptr);
        }
    }

    bx::AllocatorI *getAllocator() {
        if (nullptr == _allocator) {
            static bx::DefaultAllocator currentAllocator;
            _allocator = &currentAllocator;
        }
        return _allocator;
    }


    static const bgfx::Memory *loadMem(bx::FileReaderI *_reader, const bx::FilePath &_filePath) {
        if (bx::open(_reader, _filePath)) {
            auto size = static_cast<uint32_t>(bx::getSize(_reader));
            const bgfx::Memory *mem = bgfx::alloc(size + 1);
            bx::read(_reader, mem->data, size, bx::ErrorAssert{});
            bx::close(_reader);
            mem->data[mem->size - 1] = '\0';
            return mem;
        }
        return NULL;
    }

    static bgfx::ShaderHandle loadShader(bx::FileReaderI *_reader, const bx::StringView &_name) {
        bx::FilePath filePath("shaders/");

        switch (bgfx::getRendererType()) {
            case bgfx::RendererType::Noop:
            case bgfx::RendererType::Direct3D11:
            case bgfx::RendererType::Direct3D12: filePath.join("dx11");
                break;
            case bgfx::RendererType::Agc:
            case bgfx::RendererType::Gnm: filePath.join("pssl");
                break;
            case bgfx::RendererType::Metal: filePath.join("metal");
                break;
            case bgfx::RendererType::Nvn: filePath.join("nvn");
                break;
            case bgfx::RendererType::OpenGL: filePath.join("glsl");
                break;
            case bgfx::RendererType::OpenGLES: filePath.join("essl");
                break;
            case bgfx::RendererType::Vulkan: filePath.join("spirv");
                break;

            case bgfx::RendererType::Count:
                BX_ASSERT(false, "You should not be here!");
                break;
        }

        char fileName[512];
        bx::strCopy(fileName, BX_COUNTOF(fileName), _name);
        bx::strCat(fileName, BX_COUNTOF(fileName), ".bin");

        filePath.join(fileName);

        bgfx::ShaderHandle handle = bgfx::createShader(loadMem(_reader, filePath.getCPtr()));
        bgfx::setName(handle, _name.getPtr(), _name.getLength());

        return handle;
    }


    void *load(bx::FileReaderI *_reader, bx::AllocatorI *_allocator, const bx::FilePath &_filePath, uint32_t *_size) {
        if (bx::open(_reader, _filePath)) {
            auto size = static_cast<uint32_t>(bx::getSize(_reader));
            void *data = bx::alloc(_allocator, size);
            bx::read(_reader, data, size, bx::ErrorAssert{});
            bx::close(_reader);
            if (_size != nullptr) {
                *_size = size;
            }
            return data;
        } else {
            std::cerr << "Failed to open file: " << _filePath.getCPtr() << std::endl;
        }
        if (_size != nullptr) {
            *_size = 0;
        }
        return nullptr;
    }

    bgfx::TextureHandle loadTexture(const char *filepath) {
        // Opening the file in binary mode
        std::ifstream file(filepath, std::ios::binary);

        if (!file.is_open()) {
            std::cerr << "Failed to open file at filepath: " << filepath << std::endl;
        }

        // Getting the filesize
        file.seekg(0, std::ios::end);
        std::streampos size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Storing the data into data
        char *data = new char[size];
        file.read(data, size);

        // Closing the file
        file.close();

        bx::DefaultAllocator allocator;
        bimg::ImageContainer *img_container = bimg::imageParse(&allocator, data, size);

        if (img_container == nullptr)
            return BGFX_INVALID_HANDLE;

        const bgfx::Memory *mem = bgfx::makeRef(img_container->m_data, img_container->m_size, 0, img_container);
        delete[] data;

        bgfx::TextureHandle handle = bgfx::createTexture2D(static_cast<uint16_t>(img_container->m_width),
                                                           static_cast<uint16_t>(img_container->m_height),
                                                           1 < img_container->m_numMips, img_container->m_numLayers,
                                                           static_cast<bgfx::TextureFormat::Enum>(img_container->
                                                               m_format),
                                                           BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR, mem
        );

        std::cout << "Image width: " << static_cast<uint16_t>(img_container->m_width) <<
                ", Image height: " << static_cast<uint16_t>(img_container->m_height) << std::endl;

        return handle;
    }
    

    void imageReleaseCb(void *_ptr, void *_userData) {
        if (nullptr != _ptr) {
            auto *imageContainer = static_cast<bimg::ImageContainer *>(_userData);
            bimg::imageFree(imageContainer);
        }
    }

    bgfx::ProgramHandle loadProgram(bx::FileReaderI *_reader, const bx::StringView &_vsName,
                                    const bx::StringView &_fsName) {
        bgfx::ShaderHandle vsh = loadShader(_reader, _vsName);
        bgfx::ShaderHandle fsh = BGFX_INVALID_HANDLE;
        if (!_fsName.isEmpty()) {
            fsh = loadShader(_reader, _fsName);
        }

        return bgfx::createProgram(vsh, fsh, true /* destroy shaders when program is destroyed */);
    }

    bgfx::TextureHandle loadTexture(bx::FileReaderI *_reader, const bx::FilePath &_filePath, uint64_t _flags,
                                    uint8_t _skip, bgfx::TextureInfo *_info,
                                    bimg::Orientation::Enum *_orientation) {
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        uint32_t size;

        void *data = load(_reader, getAllocator(), _filePath, &size);
        if (data != nullptr) {
            bimg::ImageContainer *imageContainer = bimg::imageParse(getAllocator(), data, size);

            if (imageContainer != nullptr) {
                if (_orientation != nullptr) {
                    *_orientation = imageContainer->m_orientation;
                }
                const bgfx::Memory *mem = bgfx::makeRef(imageContainer->m_data, imageContainer->m_size, imageReleaseCb,
                                                        imageContainer);
                unLoad(data);

                if (imageContainer->m_cubeMap) {
                    handle = bgfx::createTextureCube(static_cast<uint16_t>(imageContainer->m_width),
                                                     1 < imageContainer->m_numMips,
                                                     imageContainer->m_numLayers,
                                                     static_cast<bgfx::TextureFormat::Enum>(imageContainer->m_format),
                                                     _flags, mem);
                } else if (1 < imageContainer->m_depth) {
                    handle = bgfx::createTexture3D(static_cast<uint16_t>(imageContainer->m_width),
                                                   static_cast<uint16_t>(imageContainer->m_height),
                                                   static_cast<uint16_t>(imageContainer->m_depth),
                                                   1 < imageContainer->m_numMips,
                                                   static_cast<bgfx::TextureFormat::Enum>(imageContainer->m_format),
                                                   _flags, mem);
                } else if (bgfx::isTextureValid(0, false, imageContainer->m_numLayers,
                                                static_cast<bgfx::TextureFormat::Enum>(imageContainer->m_format),
                                                _flags)) {
                    handle = bgfx::createTexture2D(static_cast<uint16_t>(imageContainer->m_width),
                                                   static_cast<uint16_t>(imageContainer->m_height),
                                                   1 < imageContainer->m_numMips, imageContainer->m_numLayers,
                                                   static_cast<bgfx::TextureFormat::Enum>(imageContainer->m_format),
                                                   _flags, mem);
                }
                if (bgfx::isValid(handle)) {
                    const bx::StringView name_(_filePath);
                    bgfx::setName(handle, name_.getPtr(), name_.getLength());
                }

                if (_info != nullptr) {
                    bgfx::calcTextureSize(*_info, static_cast<uint16_t>(imageContainer->m_width),
                                          static_cast<uint16_t>(imageContainer->m_height),
                                          static_cast<uint16_t>(imageContainer->m_depth), imageContainer->m_cubeMap,
                                          1 < imageContainer->m_numMips, imageContainer->m_numLayers,
                                          static_cast<bgfx::TextureFormat::Enum>(imageContainer->m_format));
                }
            }
        }
        return handle;
    }
}
