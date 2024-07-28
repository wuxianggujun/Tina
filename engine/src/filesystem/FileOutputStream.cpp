// Created by wuxianggujun on 2024/7/28.
//

#include "FileOutputStream.hpp"
#include <cstdio>

namespace Tina
{
    FileOutputStream::FileOutputStream(const std::string& path) : filePath(path)
    {
        fileStream.open(path.c_str(), std::ios::binary | std::ios::out);

        if (!fileStream.is_open())
        {
            printf("Failed to open file %s\n", filePath.c_str());
        }
    }

    FileOutputStream::~FileOutputStream()
    {
        FileOutputStream::close();
    }

    void FileOutputStream::close()
    {
        if (fileStream.is_open())
        {
            fileStream.close();
        }
    }

    void FileOutputStream::flush()
    {
        if (fileStream.is_open())
        {
            fileStream.flush();
        }
    }

    void FileOutputStream::write(Byte byte)
    {
        if (fileStream.is_open() && !byte.isNull())
        {
            fileStream.put(static_cast<char>(byte.getData()));
        }
    }

    void FileOutputStream::write(Byte* bytes, size_t size)
    {
        if (fileStream.is_open() && bytes && size > 0)
        {
            for (size_t i = 0; i < size; ++i)
            {
                fileStream.put(static_cast<char>(bytes[i].getData()));
            }
        }
    }
    

    void FileOutputStream::write(const uint8_t* bytes, size_t size)
    {
        if (fileStream.is_open() && bytes && size > 0)
        {
            fileStream.write(reinterpret_cast<const char*>(bytes), static_cast<long long>(size));
        }
    }

    void FileOutputStream::writeAndFlush(Byte byte)
    {
        write(byte);
        flush();
    }
} // Tina
