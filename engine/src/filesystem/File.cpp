#include "File.hpp"
#include "FileSystem.hpp"
#include <iostream>
#include <utility>

namespace Tina {
    File::File(const Path &filename, size_t mode) : m_path(filename), m_isOpen(false) {
        m_mode = static_cast<FileMode>(mode);

        std::ios_base::openmode openMode = std::ios_base::in; // 默认以读取模式打开

        if ((m_mode & Write) && (m_mode & Read)) {
            openMode |= std::ios_base::out; // 读写模式
        } else if (m_mode & Write) {
            openMode = std::ios_base::out; // 只写模式
        } else if (m_mode & Read) {
            openMode = std::ios_base::in; // 只读模式
        }

        if (m_mode & Append) {
            openMode |= std::ios_base::app; // 追加模式
        }

        if (m_mode & Binary) {
            openMode |= std::ios_base::binary; // 二进制模式
        } else if (m_mode & Text) {
            // 文本模式（默认）
        }

        m_fileStream.open(m_path.getPath(), openMode);

        if (!m_fileStream.is_open()) {
            throw std::runtime_error("Failed to open file: " + m_path.getFullPath());
        }

        m_isOpen = true;
    }

    File::~File() {
        File::close();
    }

    bool File::read(std::string &data) const{
        if (!m_isOpen || !(m_mode & Read)) {
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

    bool File::write(const std::string &data, const bool append) const {
        if (!m_isOpen || !(m_mode & Write)) {
            return false;
        }
    
        // 使用 const_cast 移除 const 限定，因为 seekp 和 operator<< 不是 const 成员函数
        if (append) {
            m_fileStream.seekp(0, std::ios_base::end);
        }

        const_cast<std::fstream&>(m_fileStream) << data;
        return !m_fileStream.fail();
    }
    

    void File::close() {
        if (m_isOpen) {
            m_fileStream.close();
            m_isOpen = false;
        }
    }

    bool File::isFile() const {
        return ghc::filesystem::is_regular_file(m_path.getPath());
    }

    bool File::isDirectory() const {
        return ghc::filesystem::is_directory(m_path.getPath());
    }

    bool File::isOpen() const {
        return m_isOpen;
    }

    bool File::exists() const {
        return ghc::filesystem::exists(m_path.getPath());
    }

    bool File::mkdirs() const {
        if (ghc::filesystem::exists(m_path.getPath())) {
            return true;
        }
        return ghc::filesystem::create_directories(m_path.getPath());
    }

    Path File::getPath() const {
        return m_path;
    }

    FileMode File::getMode() const {
        return m_mode;
    }

    std::vector<File> File::listFiles() const {
        std::vector<File> files;
        if (isDirectory()) {
            for (const auto &entry: ghc::filesystem::directory_iterator(m_path.getPath())) {
                files.emplace_back(Path(entry.path()), FileMode::Read);
            }
        }
        return files;
    }

    std::string File::getDirectoryPath() const {
        return m_path.getParentDirectory().getFullPath();
    }

    std::string File::getFileName() const {
        return m_path.getFileName();
    }
} // namespace Tina
