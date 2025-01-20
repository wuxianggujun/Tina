#ifndef TINA_FILESYSTEM_FILE_HPP
#define TINA_FILESYSTEM_FILE_HPP

#include <fstream>
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
    
    class File : Closeable
    {
    public:
        explicit File(Path  filename, size_t mode = FileMode::Binary);
        ~File() override;

        File(const File&) = delete;
        File& operator=(const File&) = delete;

        File(File&& other) noexcept;
        File& operator=(File&& other) noexcept;

        [[nodiscard]] bool read(std::string& data) const;
        [[nodiscard]] bool write(const std::string& data, bool append = false) const;
        void close() override;

        [[nodiscard]] bool isFile() const;
        [[nodiscard]] bool isDirectory() const;
        [[nodiscard]] bool isOpen() const;
        
        [[nodiscard]] bool exists() const;

        bool mkdirs() const;

        [[nodiscard]] Path getPath() const;
        
        [[nodiscard]] FileMode getMode() const;
        
        [[nodiscard]] std::vector<File> listFiles() const;

        [[nodiscard]] std::string getDirectoryPath() const;
        [[nodiscard]] std::string getFileName() const;

    private:
        Path m_path;
        FileMode m_mode;
       mutable std::fstream m_fileStream;
        bool m_isOpen;
    };
} // namespace Tina

#endif // TINA_FILESYSTEM_FILE_HPP
