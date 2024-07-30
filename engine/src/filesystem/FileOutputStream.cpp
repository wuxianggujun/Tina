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
        if (fileStream.is_open())
        {
            // 假设buffer.size()返回的是unsigned long long类型
            unsigned long long bufferSize = buffer.size();
            std::streamsize maxStreamSize = std::numeric_limits<std::streamsize>::max();

            std::streamsize safeSize;
            // 确保转换安全，避免溢出
            if (bufferSize <= static_cast<unsigned long long>(maxStreamSize))
            {
                safeSize = static_cast<std::streamsize>(bufferSize);
            }
            else
            {
                // 处理超出范围的情况，例如设置为最大值
                safeSize = maxStreamSize;
            }
            fileStream.write(reinterpret_cast<const char*>(buffer.peek()), safeSize);
        }
    }


    void FileOutputStream::writeAndFlush(Byte byte)
    {
        write(byte);
        flush();
    }
} // Tina
