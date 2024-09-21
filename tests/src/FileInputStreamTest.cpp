#include "io/FileInputStream.hpp"
#include "filesystem/Path.hpp"
#include "filesystem/File.hpp"
#include <gtest/gtest.h>

using namespace Tina;

class FileInputStreamTest : public ::testing::Test
{
protected:
    FileInputStream* stream = nullptr;
    Path testFilePath;

    void SetUp() override
    {
        // 设置测试环境，例如创建一个测试文件
        testFilePath = Path("FileInputStreamTest.bin");
        stream = new FileInputStream(testFilePath);
    }

    void TearDown() override
    {
        // 清理测试环境，例如关闭文件流和删除测试文件
        delete stream;
    }
};

TEST_F(FileInputStreamTest, ReadByte)
{
    Byte byte;
    bool eofReached = false;

    while (!eofReached)
    {
        byte = stream->read();
        // 读取一个字节后，检查是否到达 EOF
        eofReached = stream->eof();

        if (eofReached)
        {
            printf("EOF reached after reading %ld bytes\n", stream->tell());
            break;
        }
    }
}
