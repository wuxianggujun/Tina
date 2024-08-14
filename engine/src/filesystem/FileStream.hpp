//
// Created by wuxianggujun on 2024/8/9.
//

#ifndef TINA_FILESYSTEM_FILESTREAM_HPP
#define TINA_FILESYSTEM_FILESTREAM_HPP

#include <fstream>
#include <cstdio>
#include <string>
#include <dirent.h>
#include <cassert>

#ifdef _WIN32
// Windows 环境下使用的头文件
#include <io.h> // 提供与 unistd.h 类似的功能
#include <windows.h>
#else
// Linux 或其他类 Unix 系统环境下的头文件
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace Tina
{
    class FileStream
    {
    private:
        FILE* file = nullptr;

    public:
        FileStream();

        ~FileStream()
        {
            if (file)
            {
                fclose(file);
            }
        }

        explicit FileStream(FILE* file): file(file)
        {
        }

        FileStream(const char* path, const char* mode)
        {
            file = fopen(path, mode);
        }

        void reopen(const char* path, const char* mode) const
        {
            freopen(path, mode, file);
        }

        void seek(const long offset, const int origin) const
        {
            fseek(file, offset, origin);
        }

        size_t read(void* ptr, const size_t itemSIze, const size_t itemCount) const
        {
            return fread(ptr, itemSIze, itemCount, file);
        }

        size_t write(const void* ptr, const size_t itemSize, const size_t itemCount) const
        {
            return fwrite(ptr, itemSize, itemCount, file);
        }

        [[nodiscard]] long tell() const
        {
            return ftell(file);
        }

        [[nodiscard]] size_t close() const
        {
            return fclose(file);
        }

        [[nodiscard]] size_t putChar(const char c) const
        {
            return fputc(c, file);
        }

        [[nodiscard]] size_t getChar() const
        {
            return fgetc(file);
        }

        size_t scanf(const char* format, ...) const
        {
            va_list args;
            va_start(args, format);
            return vfscanf(file, format, args);
        }

        size_t printf(const char* format, ...) const
        {
            va_list args;
            va_start(args, format);
            return vfprintf(file, format, args);
        }

        [[nodiscard]] bool eof() const
        {
            return feof(file);
        }

        [[nodiscard]] size_t flush() const
        {
            return fflush(file);
        }

        [[nodiscard]] size_t error() const
        {
            return ferror(file);
        }

        void clearError() const
        {
            ::clearerr(file);
        }

        void rewind() const
        {
            ::rewind(file);
        }

        [[nodiscard]] FILE* getFile() const
        {
            return file;
        }
    };
}

#endif //TINA_FILESYSTEM_FILESTREAM_HPP
