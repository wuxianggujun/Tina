#include "File.hpp"
#include <filesystem>
#include <iostream>
#include <utility>

namespace Tina
{
    File::File(std::string filename, FileMode mode) : fileName_(std::move(filename)), mode_(mode), isOpen_(false)
    {
        std::ios::openmode openMode = convertMode(mode);
        fileStream_.open(fileName_, openMode);
        if (!fileStream_.is_open())
        {
            std::cerr << "Failed to open file " << fileName_ << std::endl;
        }
        else
        {
            isOpen_ = true;
        }
    }

    File::~File()
    {
        close();
    }

    bool File::read(std::string &data, bool allowWrite)
    {
        if (!isOpen_ || (mode_ == FileMode::Write && !allowWrite))
        {
            return false;
        }

        data.assign((std::istreambuf_iterator<char>(fileStream_)),
                     std::istreambuf_iterator<char>());

        return true;
    }

    bool File::write(const std::string &data, bool append, bool allowRead)
    {
        if (!isOpen_ || (mode_ == FileMode::Read && !allowRead))
        {
            return false;
        }

        if (append)
        {
            fileStream_.seekp(0, std::ios::end);
        }

        fileStream_ << data;
        return fileStream_.good();
    }

    bool File::flush()
    {
        if (fileStream_.is_open())
        {
            fileStream_.flush();
            return true;
        }
        return false;
    }

    bool File::close()
    {
        if (fileStream_.is_open())
        {
            fileStream_.close();
            isOpen_ = false;
            return true;
        }
        return false;
    }

    bool File::isOpen() const
    {
        return isOpen_;
    }

    bool File::exists() const
    {
        return std::filesystem::exists(fileName_);
    }

    std::string File::getDirectoryPath() const
    {
        return std::filesystem::path(fileName_).parent_path().string();
    }

    std::string File::getFileName() const
    {
        return std::filesystem::path(fileName_).filename().string();
    }
} // namespace Tina
