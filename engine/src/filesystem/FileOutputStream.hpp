//
// Created by wuxianggujun on 2024/7/28.
//

#ifndef TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP
#define TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP

#include <string>
#include <fstream>
#include "OutputStream.hpp"

namespace Tina
{
    class FileOutputStream : public OutputStream
    {
    public:
        explicit FileOutputStream(const std::string& path);
        ~FileOutputStream() override;
        
        void close() override;
        void flush() override;
        void write(Byte byte) override;
        void write(Byte* bytes,size_t size) override;
        void write(const uint8_t* bytes, size_t size) override;

        //void write(Byte[] bytes, size_t size);
        
        void writeAndFlush(Byte byte);

    private:
        std::string filePath;
        std::ofstream fileStream;
    };
} // Tina

#endif //TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP
