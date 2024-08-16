//
// Created by wuxianggujun on 2024/8/12.
//

#include "FileInputStream.hpp"

#include "OutputStream.hpp"

namespace Tina
{
    FileInputStream::FileInputStream(const Path& path)
    {
        file = new File(path, Read | Binary);

        if (!file->isOpen())
        {
            throw std::runtime_error("Failed to open file" + path.getFileName());
        }

        if (!file->exists())
        {
            throw std::runtime_error("File does not exist" + path.getFileName());
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

    size_t FileInputStream::transferTo(OutputStream& out)
    {
        size_t transferred = 0;
        auto buffer = new Buffer<Byte>(DEFAULT_BUFFER_SIZE);
        buffer->resize(0);
        size_t read;
        while ((read = file->getFileStream()->read(buffer->begin(), sizeof(uint8_t), DEFAULT_BUFFER_SIZE)) > 0)
        {
            out.write(buffer->begin(), sizeof(uint8_t), read);
            transferred += read;
        }
        delete buffer;
        return transferred;
    }

    Scope<Buffer<Byte>> FileInputStream::read(size_t size)
    {
        if (!file->isOpen())
        {
            throw std::runtime_error("Failed to read from file stream." + filePath.getFileName());
        }
        auto bytes = createScope<Buffer<Byte>>(size);
        if (file->getFileStream()->read(bytes.get()->begin(), sizeof(uint8_t), size) == 0)
        {
            throw std::runtime_error("Failed to read from file stream." + filePath.getFileName());
        }
        return bytes;
    }

    bool FileInputStream::eof() const
    {
        return file->getFileStream()->eof();
    }

    long FileInputStream::tell() const
    {
        return file->getFileStream()->tell();
    }
    
} // Tina
