#ifndef TINA_FILESYSTEM_FILE_HPP
#define TINA_FILESYSTEM_FILE_HPP

#include <vector>

#include "FileStream.hpp"
#include "Closeable.hpp"
#include <sys/stat.h>

namespace Tina
{
#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif

    enum class FileMode
    {
        Read,
        Write,
        ReadWrite
    };
    
    class File : Closeable
    {
    public:
        File(std::string filename, FileMode mode);
        ~File() override;

        [[nodiscard]] auto read(std::string& data) const -> bool;
        [[nodiscard]] bool write(const std::string& data, bool append = false) const;
        void close() override;
        [[nodiscard]] bool isFile() const;
        [[nodiscard]] bool isDirectory() const;
        [[nodiscard]] bool isOpen() const;
        [[nodiscard]] bool exists() const;

        [[nodiscard]] FileMode getMode() const;

        [[nodiscard]] FileStream* getFileStream() const;
        
        [[nodiscard]] std::vector<File> listFiles() const;

        [[nodiscard]] std::string getDirectoryPath() const;
        [[nodiscard]] std::string getFileName() const;

    private:
        std::string fileName_;
        FileMode mode_;
        FileStream* fileStream_;
        bool isOpen_;
    };
} // namespace Tina

#endif // TINA_FILESYSTEM_FILE_HPP
