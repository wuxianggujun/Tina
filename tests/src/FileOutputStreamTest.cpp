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

    std::string fileContent;

    // input_stream.skip(512);
    Byte text;
    /*
    for (int i = 0; i < 2152698; i++) {
        text = input_stream->read();
        buffer.append(text);
        
    }
    */
    while (true) {
        text = input_stream->read();
        fileContent += text.getData();
        buffer.append(text);
        if (text == Byte(0xFF)) {
            break;
        }
    }
    printf("%s\n", fileContent.c_str());
    input_stream->close();
    fos.write(buffer);
    fos.flush();
    fos.close();
}
