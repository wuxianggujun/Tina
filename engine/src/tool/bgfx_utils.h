#ifndef TINA_UTILS_HEADER_GUARD
#define TINA_UTILS_HEADER_GUARD

#include <bgfx/bgfx.h>

#include <bx/file.h>
#include <bx/string.h>
#include <bx/readerwriter.h>

static bx::StringView s_currentDir = "./";

class FileReader : public bx::FileReader {
    typedef bx::FileReader super;

public:
    bool open(const bx::FilePath &_filePath, bx::Error *_err) override {
        bx::StringView filePath(s_currentDir);
        filePath.set(_filePath);
        return super::open(filePath.getPtr(), _err);
    }
};

bgfx::ShaderHandle loadShader(const bx::StringView &_name);

bgfx::ProgramHandle loadProgram(bx::FileReaderI *_reader, const bx::StringView &_vsName,
                                const bx::StringView &_fsName);

#endif // TINA_UTILS_HEADER_GUARD
