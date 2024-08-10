//
// Created by wuxianggujun on 2024/7/28.
//

#ifndef TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP
#define TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP

#include <string>
#include "OutputStream.hpp"
#include "File.hpp"

namespace Tina
{
    class FileOutputStream : public OutputStream
    {

    public:
        explicit FileOutputStream(std::string path);
        explicit FileOutputStream(File* file_ptr);
        ~FileOutputStream() override;

        void close() override;
        void flush() override;
        void write(const std::string& data) const;
        void write(const std::string& data, bool append) const;
        void write(Byte byte) override;
        void write(Byte* bytes, size_t size) override;
        void write(Bytes& buffer) override;

        void writeAndFlush(Byte byte);

    private:
        void writeBytes(const Bytes& bytes) const;
    private:
        File* file;
        std::string filePath;
    };
} // Tina

#endif //TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP
