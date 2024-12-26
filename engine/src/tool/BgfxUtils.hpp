#ifndef TINA_UTILS_HEADER_GUARD
#define TINA_UTILS_HEADER_GUARD

#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bx/file.h>
#include <bx/macros.h>
#include <bx/string.h>
#include <bx/readerwriter.h>
#include <bx/bounds.h>
#include <bx/pixelformat.h>
#include <bx/filepath.h>
#include <bimg/decode.h>

namespace Tina::BgfxUtils {
    static bx::StringView s_currentDir = "./";

    bx::AllocatorI *getAllocator();

    void *allocate(size_t size);

    void unLoad(void *ptr);

    static bx::AllocatorI *_allocator = getAllocator();

    void *load(bx::FileReaderI *_reader, bx::AllocatorI *_allocator, const bx::FilePath &_filePath, uint32_t *size);

    static void imageReleaseCb(void* _ptr, void* _userData);
    
    bgfx::ShaderHandle loadShader(const bx::StringView &_name);

    bgfx::ProgramHandle loadProgram(bx::FileReaderI* _reader,
                                  const bx::StringView& _vsName,
                                  const bx::StringView& _fsName);

    bgfx::ProgramHandle loadProgram(const char* _vsName, const char* _fsName);

    bgfx::TextureHandle loadTexture(bx::FileReaderI *_reader, const bx::FilePath &_filePath, uint64_t _flags = BGFX_TEXTURE_NONE|BGFX_SAMPLER_NONE,
                                    uint8_t _skip = 0, bgfx::TextureInfo *_info= nullptr, bimg::Orientation::Enum *_orientation = nullptr);

    bgfx::TextureHandle loadTexture(const char* fileName);
    
}


#endif // TINA_UTILS_HEADER_GUARD
