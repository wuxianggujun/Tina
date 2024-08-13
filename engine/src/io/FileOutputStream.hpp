//
// Created by wuxianggujun on 2024/7/28.
//
/*
#ifndef TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP
#define TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP

#include "File.hpp"
#include "Path.hpp"
#include "io/OutputStream.hpp"

namespace Tina
{
    class FileOutputStream : public OutputStream
    {

    public:
        explicit FileOutputStream(std::string path);
        explicit FileOutputStream(File file);
        ~FileOutputStream() override;

        void close() override;
        void flush() override;
        void write(const std::string& data) const;
        void write(const std::string& data, bool append) const;

        void writeAndFlush(const Byte& byte);

        void writeTo(OutputStream& outputStream);
    private:
        void writeBytes(const Bytes& bytes) const;
    private:
        File* file;
        std::string filePath;
    };
} // Tina

#endif //TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP
*/