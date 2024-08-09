#include "File.hpp"
#include <filesystem>
#include <iostream>
#include <utility>

namespace Tina
{
    File::File(std::string filename, FileMode mode): fileName_(std::move(filename)), mode_(mode), fileStream_(nullptr),
                                                     isOpen_(false)
    {
        const char* cMode = nullptr;
        switch (mode)
        {
        case FileMode::Read:
            cMode = "r";
            break;
        case FileMode::ReadWrite:
            cMode = "r+";
            break;
        case FileMode::Write:
            cMode = "w";
            break;
        default:
            throw std::invalid_argument("Invalid File Mode");
        }
        fileStream_ = new FileStream(fileName_.c_str(), cMode);
        if (!fileStream_->getFile())
        {
            throw std::runtime_error("Failed to open file");
        }
        isOpen_ = true;
    }

    File::~File()
    {
        close();
    }

    bool File::read(std::string& data, bool allowWrite) const
    {
        if (!isOpen_ || mode_ == FileMode::Write)
            return false;
        char buffer[1024];
        size_t bytesRead = fileStream_->read(buffer, sizeof(char), sizeof(buffer) - 1);
        if (bytesRead == 0)
            return false;
        buffer[bytesRead] = '\0';
        data = buffer;
        return true;
    }

    bool File::write(const std::string& data, bool append, bool allowRead) const
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
        if (isOpen_)
        {
            if (fileStream_->close()) throw std::runtime_error("Failed to close file");
            isOpen_ = false;
            delete fileStream_;
            fileStream_ = nullptr;
        }
    }

    bool File::isOpen() const
    {
        return isOpen_;
    }

    bool File::exists() const
    {
        struct _stat buffer{};
        return _stat(fileName_.c_str(), &buffer) == 0;
    }

    [[nodiscard]] std::string File::getDirectoryPath() const
    {
        size_t pos = fileName_.find_last_of(PATH_SEPARATOR);
        return (pos != std::string::npos) ? fileName_.substr(0, pos) : "";
    }

    [[nodiscard]] std::string File::getFileName() const
    {
        size_t pos = fileName_.find_last_of(PATH_SEPARATOR);
        return (pos != std::string::npos) ? fileName_.substr(pos + 1) : fileName_;
    }
} // namespace Tina
