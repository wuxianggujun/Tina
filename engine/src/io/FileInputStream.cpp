//
// Created by wuxianggujun on 2024/8/12.
//

#include "FileInputStream.hpp"

namespace Tina
{
    FileInputStream::FileInputStream(const Path& path)
    {
        file = new File(path, Binary);

        if (!file->isOpen())
        {
            throw std::runtime_error("Failed to open file" + path.getFileName());
        }
    }

    FileInputStream::FileInputStream(File* file_ptr)
    {
        this->file = file_ptr;
        filePath = file->getPath();
        if (!file->isOpen())
        {
            throw std::runtime_error("Failed to open file" + filePath.getFileName());
        }
    }

    FileInputStream::~FileInputStream()
    {
        FileInputStream::close();
    }

    void FileInputStream::close()
    {
        if (file && file->isOpen())
        {
            file->close();
            delete file;
            file = nullptr;
        }
    }

    Byte FileInputStream::read()
    {
        auto result = file->getFileStream()->getChar();
        return static_cast<Byte>(result);
    }

    Buffer<Byte> FileInputStream::read(size_t size)
    {
        auto bytes = Buffer<Byte>(size);
        bytes.resize(0);
        if (file->getFileStream()->read(bytes.begin(), sizeof(uint8_t), size) == 0)
        {
            throw std::runtime_error("Failed to read from file stream." + filePath.getFileName());
        }
        return bytes;
    }
} // Tina
