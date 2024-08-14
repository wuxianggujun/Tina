//
// Created by wuxianggujun on 2024/8/12.
//

#ifndef FILEINPUTSTREAM_HPP
#define FILEINPUTSTREAM_HPP

#include "filesystem/Path.hpp"
#include "filesystem/File.hpp"
#include "InputStream.hpp"

namespace Tina
{
    class FileInputStream : public InputStream
    {
    public:
        explicit FileInputStream(const Path& path);
        explicit FileInputStream(File* file);
        ~FileInputStream() override;

        void close() override;
        Byte read() override;
        Buffer<Byte> read(size_t size) override;
        [[nodiscard]] bool eof() const;
        [[nodiscard]] long tell() const;

    private:
        Path filePath;
        File* file;
    };
} // Tina

#endif //FILEINPUTSTREAM_HPP
