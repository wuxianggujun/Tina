#include "File.hpp"
#include "FileSystem.hpp"
#include <iostream>
#include <utility>

namespace Tina
{
    File::File(const Path& filename, size_t mode) : fileName_(filename),
                                                    fileStream_(nullptr),
                                                    isOpen_(false)
    {
        mode_ = static_cast<FileMode>(mode);

        std::string cMode;
        // 判断读写追加模式
        if ((mode_ & Write) && (mode_ & Read))
        {
            cMode = "r+";
        }
        else if (mode_ & Write)
        {
            cMode = "w";
        }
        else if (mode_ & Read)
        {
            cMode = "r";
        }
        else if (mode_ & Append)
        {
            cMode = "a";
        }
        else
        {
            throw std::invalid_argument("Invalid file mode");
        }

        // 添加文本或二进制模式
        if (mode_ & Binary)
        {
            cMode += "b";
        }
        else if (mode_ & Text)
        {
            cMode += "t";
        }

        fileStream_ = new FileStream(fileName_.getFullPath().c_str(), cMode.c_str());
        if (!fileStream_->getFile())
        {
            throw std::runtime_error("Failed file not exists or cannot open file");
        }
        isOpen_ = true;
    }

    File::~File()
    {
        File::close();
    }

    auto File::read(std::string& data) const -> bool
    {
        if (!isOpen_ || mode_ & Binary)
        {
            return false;
        }
        constexpr size_t bufferSize = 1024; // 定义缓冲区大小
        std::vector<char> buffer(bufferSize);
        size_t bytesRead;

        while ((bytesRead = fileStream_->read(buffer.data(), 1, bufferSize)) > 0)
        {
            data.append(buffer.data(), bytesRead); // 将读取的数据追加到数据字符串
        }
        return true;
    }

    bool File::write(const std::string& data, bool append) const
    {
        if (!isOpen_ || mode_ == FileMode::Read)
            return false;
        if (append)
            fileStream_->seek(0, SEEK_END);

        auto bytesWritten = fileStream_->write(data.c_str(), sizeof(char), data.size());
        return bytesWritten == data.size();
    }

    void File::close()
    {
        if (isOpen_ && fileStream_)
        {
            if (fileStream_->close()) throw std::runtime_error("Failed to close file");
            isOpen_ = false;
            delete fileStream_;
            fileStream_ = nullptr;
        }
    }

    bool File::isFile() const
    {
        return ghc::filesystem::is_regular_file(fileName_.getFullPath());
    }

    bool File::isDirectory() const
    {
        return ghc::filesystem::is_directory(fileName_.getFullPath());
    }

    bool File::isOpen() const
    {
        return isOpen_;
    }

    bool File::exists() const
    {
        ghc::filesystem::path path(fileName_.getFullPath());
        return ghc::filesystem::exists(path) && ghc::filesystem::is_regular_file(path);
    }

    bool File::mkdirs() const {
        if (ghc::filesystem::exists(fileName_.getFullPath())) {
            return true;
        }
        return ghc::filesystem::create_directory(fileName_.getFullPath());
    }

    Path File::getPath() const
    {
        return fileName_;
    }

    FileMode File::getMode() const
    {
        return mode_;
    }

    FileStream* File::getFileStream() const
    {
        return fileStream_;
    }

    std::vector<File> File::listFiles() const
    {
        std::vector<File> files;
        for (const auto& entry : ghc::filesystem::directory_iterator(fileName_.getFullPath()))
        {
            files.emplace_back(Path(entry.path().string()), FileMode::Read);
        }
        return files;
    }

    [[nodiscard]] std::string File::getDirectoryPath() const
    {
        size_t pos = fileName_.getFullPath().find_last_of(PATH_SEPARATOR);
        return (pos != std::string::npos) ? fileName_.getFullPath().substr(0, pos) : "";
    }

    [[nodiscard]] std::string File::getFileName() const
    {
        size_t pos = fileName_.getFullPath().find_last_of(PATH_SEPARATOR);
        return (pos != std::string::npos) ? fileName_.getFullPath().substr(pos + 1) : fileName_.getFullPath();
    }
} // namespace Tina
