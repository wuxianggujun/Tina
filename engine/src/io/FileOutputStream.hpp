//
// Created by wuxianggujun on 2024/7/28.
//
#ifndef TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP
#define TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP


#include "filesystem/Path.hpp"
#include "filesystem/File.hpp"
#include "OutputStream.hpp"
#include "Byte.hpp"

namespace Tina {
    class FileOutputStream : public OutputStream {
    public:
        explicit FileOutputStream(const Path& path);

        explicit FileOutputStream(File* file);

        ~FileOutputStream() override;
        
        void write(const Byte &byte) override; 

        void write(const Bytes &buffer) override;

        void write(const Bytes &buffer, size_t size) override;

        void write(const Bytes& buffer, size_t offset, size_t size) override;
        
        void close() override;

        void flush() override;
        
        void writeAndFlush(const Buffer<Byte> &buffer);
    
    private:
        Path path_;
        File *file_;
    };
} // Tina

#endif //TINA_FILESYSTEM_FILEOUTPUTSTREAM_HPP
