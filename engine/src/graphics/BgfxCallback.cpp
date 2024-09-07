#include "BgfxCallback.hpp"
#include <bx/debug.h>
#include <bx/file.h>
#include <bx/bx.h>
#include <bimg/bimg.h>

#include "core/Config.hpp"

namespace Tina {
    void BgfxCallback::fatal(const char *_filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char *_str) {
        trace(_filePath, _line, "BGFX FATAL 0x%08x: %s \n", _code, _str);
        if (bgfx::Fatal::DebugCheck == _code) {
            bx::debugBreak();
        } else {
            abort();
        }
    }

    void BgfxCallback::traceVargs(const char *_filePath, uint16_t _line, const char *_format, va_list _argList) {
        char temp[2048];
        char *out = temp;
        va_list argListCopy;
        va_copy(argListCopy, _argList);
        int32_t len = bx::snprintf(out, sizeof(temp), "%s (%d): ", _filePath, _line);
        int32_t total = len + bx::vsnprintf(out + len, sizeof(temp) - len, _format, argListCopy);
        va_end(argListCopy);
        if (static_cast<int32_t>(sizeof(temp)) < total) {
            out = static_cast<char *>(alloca(total + 1));
            bx::memCopy(out, temp, len);
            bx::vsnprintf(out + len, total - len, _format, _argList);
        }
        out[total] = '\0';
        if (Config::isBgfxDebugLogEnabled)
            bx::debugOutput(out);
    }

    void BgfxCallback::profilerBegin(const char *_name, uint32_t _abgr, const char *_filePath, uint16_t _line) {
    }

    void BgfxCallback::profilerBeginLiteral(const char *_name, uint32_t _abgr, const char *_filePath, uint16_t _line) {
    }

    void BgfxCallback::profilerEnd() {
    }

    uint32_t BgfxCallback::cacheReadSize(uint64_t _id) {
        return 0;
    }

    bool BgfxCallback::cacheRead(uint64_t _id, void *_data, uint32_t _size) {
        return false;
    }

    void BgfxCallback::cacheWrite(uint64_t _id, const void *_data, uint32_t _size) {
    }

    void BgfxCallback::screenShot(const char *_filePath, uint32_t _width, uint32_t _height, uint32_t _pitch,
                                  const void *_data, uint32_t _size, bool _yflip) {
        const int32_t len = bx::strLen(_filePath) + 5;
        char *filePath = static_cast<char *>(alloca(len));
        bx::strCopy(filePath, len, _filePath);
        bx::strCat(filePath, len, ".png");

        bx::FileWriter writer;
        if (bx::open(&writer, filePath)) {
            bimg::imageWritePng(&writer, _width, _height, _pitch, _data, bimg::TextureFormat::RGBA8, _yflip);
            bx::close(&writer);
        }
    }

    void BgfxCallback::captureBegin(uint32_t _width, uint32_t _height, uint32_t _pitch,
        bgfx::TextureFormat::Enum _format, bool _yflip) {
        BX_TRACE("Warning: using capture without callback (a.k.a. pointless).");
    }

    void BgfxCallback::captureEnd() {
    }

    void BgfxCallback::captureFrame(const void *_data, uint32_t _size) {
    }

    void BgfxCallback::trace(const char *_filePath, uint16_t _line, const char *_format, ...) {
        va_list argList;
        va_start(argList, _format);
        traceVargs(_filePath, _line, _format, argList);
        va_end(argList);
    }
}
