#ifndef TINA_FILESYSTEM_FILE_HPP
#define TINA_FILESYSTEM_FILE_HPP

#include <fstream>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <dirent.h>
#include <cassert>
#include <sys/stat.h>

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
#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif
    
    enum class FileMode
    {
        Read,
        Write,
        ReadWrite
    };

    enum class ContentType
    {
        Text,
        Binary
    };

    class FileStream
    {
    private:
        FILE* file;

    public:
        FileStream();
        ~FileStream();

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

    class File
    {
    public:
        File(std::string filename, FileMode mode);
        ~File();

        bool read(std::string& data, bool allowWrite = false);
        bool write(const std::string& data, bool append = false, bool allowRead = false);
        bool flush();
        bool close();
        [[nodiscard]] bool isOpen() const;
        [[nodiscard]] bool exists() const;

        [[nodiscard]] std::string getDirectoryPath() const;
        [[nodiscard]] std::string getFileName() const;

    private:
        static std::ios::openmode convertMode(FileMode mode)
        {
            switch (mode)
            {
            case FileMode::Read:
                return std::ios::in;
            case FileMode::Write:
                return std::ios::out;
            case FileMode::ReadWrite:
                return std::ios::in | std::ios::out;
            default:
                return std::ios::in;
            }
        }

    private:
        std::string fileName_;
        FileMode mode_;
        std::fstream fileStream_;
        bool isOpen_;
    };
} // namespace Tina

#endif // TINA_FILESYSTEM_FILE_HPP
