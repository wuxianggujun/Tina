#ifndef TINA_FILESYSTEM_FILE_HPP
#define TINA_FILESYSTEM_FILE_HPP

#include "FileStream.hpp"
#include "Closeable.hpp"

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

        bool read(std::string& data, bool allowWrite = false) const;
        [[nodiscard]] bool write(const std::string& data, bool append = false, bool allowRead = false) const;
        void close() override;
        [[nodiscard]] bool isOpen() const;
        [[nodiscard]] bool exists() const;

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
