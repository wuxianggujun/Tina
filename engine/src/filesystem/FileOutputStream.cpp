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
            fileStream.put(byte.getChar());
        }
    }

    void FileOutputStream::write(Byte* bytes, size_t size)
    {
        if (fileStream.is_open() && bytes && size > 0)
        {
            for (size_t i = 0; i < size; ++i)
            {
                fileStream.put(bytes[i].getChar());
            }
        }
    }

    void FileOutputStream::write(ByteBuffer& buffer)
    {
    }
    

    void FileOutputStream::writeAndFlush(Byte byte)
    {
        write(byte);
        flush();
    }
} // Tina
