#include "tina/utils/BgfxUtils.hpp"
#include <bx/readerwriter.h>
#include <bx/file.h>
#include <bimg/decode.h>
#include <bx/allocator.h>

namespace Tina::Utils
{
    namespace
    {
        // 文件读取器
        bx::FileReaderI* getFileReader()
        {
            static bx::FileReader s_fileReader;
            return &s_fileReader;
        }

        // 默认分配器
        class DefaultAllocator : public bx::AllocatorI
        {
        public:
            virtual ~DefaultAllocator()
            {
            }

            virtual void* realloc(void* _ptr, size_t _size, size_t _align, const char* _file, uint32_t _line) override
            {
                if (0 == _size)
                {
                    if (NULL != _ptr)
                    {
                        // 获取原始分配的指针
                        uint8_t* ptr = (uint8_t*)_ptr;
                        ptr -= sizeof(size_t);
                        delete[] ptr;
                    }
                    return NULL;
                }

                // 计算需要的总大小，包括存储原始大小的空间
                size_t totalSize = _size + _align + sizeof(size_t);
                uint8_t* ptr = new uint8_t[totalSize];
                
                // 存储分配的大小
                *(size_t*)ptr = _size;
                
                // 计算对齐后的数据指针
                uint8_t* aligned = (uint8_t*)bx::alignPtr(ptr + sizeof(size_t), _align);

                if (_ptr != NULL)
                {
                    // 如果是重新分配，复制原有数据
                    uint8_t* oldPtr = (uint8_t*)_ptr;
                    oldPtr -= sizeof(size_t);
                    size_t oldSize = *(size_t*)oldPtr;
                    memcpy(aligned, _ptr, bx::min(oldSize, _size));
                    delete[] oldPtr;
                }

                return aligned;
            }
        };

        static DefaultAllocator s_allocator;

        // 内存释放回调
        void releaseMemoryCb(void* ptr, void* userData)
        {
            BX_UNUSED(ptr);
            if (userData != nullptr)
            {
                bimg::ImageContainer* imageContainer = (bimg::ImageContainer*)userData;
                bimg::imageFree(imageContainer);
            }
        }
    }

    const bgfx::Memory* BgfxUtils::loadMem(const std::string& filePath)
    {
        bx::FileReaderI* reader = getFileReader();
        const bgfx::Memory* mem = nullptr;

        if (bx::open(reader, filePath.c_str()))
        {
            uint32_t size = (uint32_t)bx::getSize(reader);
            mem = bgfx::alloc(size + 1);
            bx::read(reader, mem->data, size, bx::ErrorAssert{});
            bx::close(reader);
            mem->data[mem->size - 1] = '\0';
        }
        else
        {
            TINA_LOG_ERROR("Failed to load file: {}", filePath);
        }

        return mem;
    }

    bgfx::ShaderHandle BgfxUtils::loadShader(const std::string& name)
    {
        bx::FilePath filePath("shaders/");
        
        // 根据渲染器类型选择着色器目录
        switch (bgfx::getRendererType())
        {
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12: 
            filePath.join("dx11"); 
            break;
        case bgfx::RendererType::OpenGL:     
            filePath.join("glsl"); 
            break;
        case bgfx::RendererType::OpenGLES:   
            filePath.join("essl"); 
            break;
        case bgfx::RendererType::Vulkan:     
            filePath.join("spirv"); 
            break;
        case bgfx::RendererType::Metal:      
            filePath.join("metal"); 
            break;
        default:
            TINA_LOG_ERROR("Unsupported renderer type: {}", bgfx::getRendererName(bgfx::getRendererType()));
            return BGFX_INVALID_HANDLE;
        }

        std::string fileName = name + ".bin";
        filePath.join(fileName.c_str());

        TINA_LOG_DEBUG("Loading shader '{}' from path: {}", name, filePath.getCPtr());

        const bgfx::Memory* mem = loadMem(filePath.getCPtr());
        if (!mem)
        {
            TINA_LOG_ERROR("Failed to load shader memory for '{}'", name);
            return BGFX_INVALID_HANDLE;
        }

        bgfx::ShaderHandle handle = bgfx::createShader(mem);
        if (!bgfx::isValid(handle))
        {
            TINA_LOG_ERROR("Failed to create shader '{}' from memory", name);
            return BGFX_INVALID_HANDLE;
        }

        bgfx::setName(handle, name.c_str());
        TINA_LOG_DEBUG("Successfully loaded shader '{}'", name);

        return handle;
    }

    bgfx::ProgramHandle BgfxUtils::loadProgram(const std::string& vsName, const std::string& fsName)
    {
        TINA_LOG_DEBUG("Loading shader program - VS: '{}', FS: '{}'", vsName, fsName);

        bgfx::ShaderHandle vsh = loadShader(vsName);
        if (!bgfx::isValid(vsh))
        {
            TINA_LOG_ERROR("Failed to load vertex shader '{}'", vsName);
            return BGFX_INVALID_HANDLE;
        }

        bgfx::ShaderHandle fsh = BGFX_INVALID_HANDLE;
        if (!fsName.empty())
        {
            fsh = loadShader(fsName);
            if (!bgfx::isValid(fsh))
            {
                TINA_LOG_ERROR("Failed to load fragment shader '{}'", fsName);
                bgfx::destroy(vsh);
                return BGFX_INVALID_HANDLE;
            }
        }

        bgfx::ProgramHandle program = bgfx::createProgram(vsh, fsh, true);
        if (!bgfx::isValid(program))
        {
            TINA_LOG_ERROR("Failed to create program from shaders - VS: '{}', FS: '{}'", vsName, fsName);
            return BGFX_INVALID_HANDLE;
        }

        TINA_LOG_DEBUG("Successfully created shader program - VS: '{}', FS: '{}'", vsName, fsName);
        return program;
    }

    TextureLoadResult BgfxUtils::loadTexture(const std::string& filePath, uint64_t flags)
    {
        TextureLoadResult result = {
            BGFX_INVALID_HANDLE,  // handle
            0,                    // width
            0,                    // height
            1,                    // depth
            false,               // hasMips
            1,                   // layers
            bgfx::TextureFormat::RGBA8  // format
        };

        bx::FileReaderI* reader = getFileReader();

        if (bx::open(reader, filePath.c_str()))
        {
            uint32_t size = (uint32_t)bx::getSize(reader);
            TINA_LOG_DEBUG("Loading texture file '{}' of size: {} bytes", filePath, size);
            
            void* data = bx::alloc(&s_allocator, size);
            if (data == nullptr)
            {
                TINA_LOG_ERROR("Failed to allocate memory for texture data: {} bytes", size);
                bx::close(reader);
                return result;
            }

            TINA_LOG_DEBUG("Memory allocated for texture data: {} bytes", size);

            try
            {
                bx::read(reader, data, size, bx::ErrorAssert{});
                bx::close(reader);

                bimg::ImageContainer* imageContainer = bimg::imageParse(&s_allocator, data, size);
                if (imageContainer)
                {
                    const bgfx::Memory* mem = bgfx::makeRef(
                        imageContainer->m_data,
                        imageContainer->m_size,
                        releaseMemoryCb,
                        imageContainer
                    );

                    if (imageContainer->m_cubeMap)
                    {
                        result.handle = bgfx::createTextureCube(
                            uint16_t(imageContainer->m_width),
                            1 < imageContainer->m_numMips,
                            imageContainer->m_numLayers,
                            bgfx::TextureFormat::Enum(imageContainer->m_format),
                            flags,
                            mem
                        );
                    }
                    else if (1 < imageContainer->m_depth)
                    {
                        result.handle = bgfx::createTexture3D(
                            uint16_t(imageContainer->m_width),
                            uint16_t(imageContainer->m_height),
                            uint16_t(imageContainer->m_depth),
                            1 < imageContainer->m_numMips,
                            bgfx::TextureFormat::Enum(imageContainer->m_format),
                            flags,
                            mem
                        );
                    }
                    else
                    {
                        result.handle = bgfx::createTexture2D(
                            uint16_t(imageContainer->m_width),
                            uint16_t(imageContainer->m_height),
                            1 < imageContainer->m_numMips,
                            imageContainer->m_numLayers,
                            bgfx::TextureFormat::Enum(imageContainer->m_format),
                            flags,
                            mem
                        );
                    }

                    if (bgfx::isValid(result.handle))
                    {
                        bgfx::setName(result.handle, filePath.c_str());
                        result.width = uint16_t(imageContainer->m_width);
                        result.height = uint16_t(imageContainer->m_height);
                        result.depth = uint16_t(imageContainer->m_depth);
                        result.hasMips = 1 < imageContainer->m_numMips;
                        result.layers = imageContainer->m_numLayers;
                        result.format = bgfx::TextureFormat::Enum(imageContainer->m_format);
                        
                        TINA_LOG_DEBUG("Texture created successfully");
                    }
                    else
                    {
                        TINA_LOG_ERROR("Failed to create texture from loaded data");
                        bimg::imageFree(imageContainer);
                    }
                }
                else
                {
                    TINA_LOG_ERROR("Failed to parse image data");
                }

                bx::free(&s_allocator, data);
            }
            catch (const std::exception& e)
            {
                TINA_LOG_ERROR("Exception during texture loading: {}", e.what());
                bx::free(&s_allocator, data);
            }
        }
        else
        {
            TINA_LOG_ERROR("Failed to open texture file: {}", filePath);
        }

        return result;
    }
} // namespace Tina::Utils 