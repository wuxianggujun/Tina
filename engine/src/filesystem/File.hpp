#ifndef TINA_FILESYSTEM_FILE_HPP
#define TINA_FILESYSTEM_FILE_HPP

#include <vector>

#include "Path.hpp"
#include "FileStream.hpp"
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
