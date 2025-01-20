#include "File.hpp"
#include "FileSystem.hpp"
#include <iostream>
#include <utility>

namespace Tina
{
    File::File(Path filename, size_t mode) : m_path(std::move(filename)), m_isOpen(false)
    {
        m_mode = static_cast<FileMode>(mode);

        std::ios_base::openmode openMode = std::ios_base::in; // 默认以读取模式打开

        if ((m_mode & Write) && (m_mode & Read))
        {
            openMode |= std::ios_base::out; // 读写模式
        }
        else if (m_mode & Write)
        {
            openMode = std::ios_base::out; // 只写模式
        }
        else if (m_mode & Read)
        {
            openMode = std::ios_base::in; // 只读模式
        }

        if (m_mode & Append)
        {
            openMode |= std::ios_base::app; // 追加模式
        }

        if (m_mode & Binary)
        {
            openMode |= std::ios_base::binary; // 二进制模式
        }
        else if (m_mode & Text)
        {
            // 文本模式（默认）
        }

        m_fileStream.open(m_path.toString(), openMode);

        if (!m_fileStream.is_open())
        {
            throw std::runtime_error("Failed to open file: " + m_path.toString());
        }

        m_isOpen = true;
    }

    File::~File()
    {
        File::close();
    }

    File::File(File&& other) noexcept: m_path(std::move(other.m_path)),
                                       m_mode(other.m_mode),
                                       m_fileStream(std::move(other.m_fileStream)),
                                       m_isOpen(other.m_isOpen)
    {
        // 使移动后的对象处于有效但未指定的状态
        other.m_isOpen = false;
    }

    File& File::operator=(File&& other) noexcept
    {
        if (this != &other)
        {
            close();
            m_path = std::move(other.m_path);
            m_mode = other.m_mode;
            m_fileStream = std::move(other.m_fileStream);
            m_isOpen = other.m_isOpen;
            // 使移动后的对象处于有效但未指定的状态
            other.m_isOpen = false;
        }
        return *this;
    }

    bool File::read(std::string& data) const
    {
        if (!m_isOpen || !(m_mode & Read))
        {
            return false;
        }

        // 将文件指针移动到文件开头
        m_fileStream.seekg(0, std::ios::beg);

        // 清空 data
        data.clear();
        // 使用 std::istreambuf_iterator 读取文件内容
        data.assign((std::istreambuf_iterator<char>(m_fileStream)),
                    std::istreambuf_iterator<char>());

        return m_fileStream.eof();
    }

    bool File::write(const std::string& data, const bool append) const
    {
        if (!m_isOpen || !(m_mode & Write))
        {
            return false;
        }

        // 使用 const_cast 移除 const 限定，因为 seekp 和 operator<< 不是 const 成员函数
        if (append)
        {
            m_fileStream.seekp(0, std::ios_base::end);
        }

        const_cast<std::fstream&>(m_fileStream) << data;
        return !m_fileStream.fail();
    }


    void File::close()
    {
        if (m_isOpen)
        {
            m_fileStream.close();
            m_isOpen = false;
        }
    }

    bool File::isFile() const
    {
        return ghc::filesystem::is_regular_file(m_path.toString());
    }

    bool File::isDirectory() const
    {
        return ghc::filesystem::is_directory(m_path.toString());
    }

    bool File::isOpen() const
    {
        return m_isOpen;
    }

    bool File::exists() const
    {
        return ghc::filesystem::exists(m_path.toString());
    }

    bool File::mkdirs() const
    {
        if (ghc::filesystem::exists(m_path.toString()))
        {
            return true;
        }
        return ghc::filesystem::create_directories(m_path.toString());
    }

    Path File::getPath() const
    {
        return m_path;
    }

    FileMode File::getMode() const
    {
        return m_mode;
    }

    std::vector<File> File::listFiles() const
    {
        std::vector<File> files;
        if (isDirectory())
        {
            for (const auto& entry : ghc::filesystem::directory_iterator(m_path.toString()))
            {
                files.emplace_back(Path(entry.path().string()), FileMode::Read);
            }
        }
        return files;
    }

    std::string File::getDirectoryPath() const
    {
        return m_path.getParentDirectory().getFullPath();
    }

    std::string File::getFileName() const
    {
        return m_path.getFileName();
    }
} // namespace Tina
