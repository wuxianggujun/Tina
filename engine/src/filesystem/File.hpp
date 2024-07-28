#ifndef TINA_FILESYSTEM_FILE_HPP
#define TINA_FILESYSTEM_FILE_HPP

#include <string>
#include <fstream>

namespace Tina
{
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

    class File
    {
    public:
        File(std::string filename, FileMode mode);
        ~File();

        bool read(std::string &data, bool allowWrite = false);
        bool write(const std::string &data, bool append = false, bool allowRead = false);
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
