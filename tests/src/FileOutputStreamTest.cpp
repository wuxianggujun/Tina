#include <gtest/gtest.h>
#include "io/FileOutputStream.hpp"
#include "io/FileInputStream.hpp"

using namespace Tina;

TEST(FileOutputStreamTest, Constructor) {
    Path path("test_file.bin");
    FileOutputStream fos(path);

    Buffer<Byte> buffer(2152698);
    Path path2("小说.txt");

    auto *input_stream = new FileInputStream(path2);
    // input_stream.skip(512);
    for (int i = 0; i < 2152698; i++) {
        auto text = input_stream->read();
        buffer.append(text);
    }
    fos.write(buffer);
    fos.flush();
    fos.close();
}
