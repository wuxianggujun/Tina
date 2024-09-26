#ifndef TINA_FILESYSTEM_FILE_HPP
#define TINA_FILESYSTEM_FILE_HPP

#include <cstdarg>
#include <vector>

#include "FileSystem.hpp"
#include "Path.hpp"
#include "io/Closeable.hpp"

namespace Tina
{
    enum FileMode
    {
        Read = 1 << 0,
        Write = 1 << 1,
        Text = 1 << 2,
        Binary = 1 << 3,
        Append = 1 << 4
    };

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
    
    class File : Closeable
    {
    public:
        explicit File(const Path& filename, size_t mode = Binary);
        ~File() override;

        [[nodiscard]] auto read(std::string& data) const -> bool;
        [[nodiscard]] bool write(const std::string& data, bool append = false) const;
        void close() override;

        [[nodiscard]] bool isFile() const;
        [[nodiscard]] bool isDirectory() const;
        [[nodiscard]] bool isOpen() const;
        
        [[nodiscard]] bool exists() const;

        bool mkdirs() const;

        [[nodiscard]] Path getPath() const;
        
        [[nodiscard]] FileMode getMode() const;

        [[nodiscard]] FileStream* getFileStream() const;

        [[nodiscard]] std::vector<File> listFiles() const;

        [[nodiscard]] std::string getDirectoryPath() const;
        [[nodiscard]] std::string getFileName() const;

    private:
        Path fileName_;
        FileMode mode_;
        FileStream* fileStream_;
        bool isOpen_;
    };
} // namespace Tina

#endif // TINA_FILESYSTEM_FILE_HPP
