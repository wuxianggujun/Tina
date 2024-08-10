// Created by wuxianggujun on 2024/7/28.
//

#include "FileOutputStream.hpp"
#include <cstdio>
#include <utility>


namespace Tina
{
    FileOutputStream::FileOutputStream(std::string path) : file(new File(path, FileMode::Write)),
                                                           filePath(std::move(path))
    {
    }

    FileOutputStream::FileOutputStream(File* file_ptr)
    {
        if (file_ptr)
        {
            if (!(file_ptr->getMode() & Read))
            {
                throw std::runtime_error("FileOutputStream: FileMode must be Write");
            }
            file = file_ptr;
            filePath = file->getDirectoryPath();
        }
    }

    FileOutputStream::~FileOutputStream()
    {
        FileOutputStream::close();
    }

    void FileOutputStream::close()
    {
        if (file && file->isOpen())
        {
            file->close();
            delete file;
            file = nullptr;
        }
    }

    void FileOutputStream::flush()
    {
       (void)file->getFileStream()->flush();
    }

    void FileOutputStream::write(const std::string& data) const
    {
        (void)file->write(data, false);
    }

    void FileOutputStream::write(const std::string& data, bool append) const
    {
        (void)file->write(data, append);
    }

    void FileOutputStream::write(Byte byte)
    {
    }

    void FileOutputStream::write(Byte* bytes, size_t size)
    {
    }

    void FileOutputStream::write(Bytes& buffer)
    {
        writeBytes(buffer);
    }


    void FileOutputStream::writeAndFlush(Byte byte)
    {
        write(byte);
        flush();
    }

    void FileOutputStream::writeBytes(const Bytes& bytes) const
    {
        if (file->isOpen())
        {
            if (const auto fileStream = file->getFileStream())
            {
                const auto* data = reinterpret_cast<const uint8_t*>(bytes.begin());
                const size_t size = bytes.size();
                if (const size_t written = fileStream->write(data, sizeof(uint8_t), size); written != size)
                {
                    throw std::runtime_error("FileOutputStream: write bytes failed");
                }
                (void)fileStream->flush();
            }
        }
    }
} // Tina
