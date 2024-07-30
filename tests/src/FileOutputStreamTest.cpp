//
// Created by wuxianggujun on 2024/7/28.
//
#include <gtest/gtest.h>
#include "filesystem/FileOutputStream.hpp"

using namespace Tina;

class FileOutputStreamTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 创建一个临时文件用于测试
        testFileName = "test_output.bin";
    }

    void TearDown() override
    {
        // 删除测试文件
        //std::remove(testFileName.c_str());
    }

    std::string testFileName;
};


TEST_F(FileOutputStreamTest, WriteSingleByte)
{
    FileOutputStream ostrm(testFileName);
    Byte bytes[] = {65, 66, 67, 68};
    ostrm.write(bytes, 4);
    ostrm.flush();
    ostrm.close();
}

TEST_F(FileOutputStreamTest, WriteAndReadBack)
{
    // 准备测试数据
    ByteBuffer buffer;
    std::string testData = "Hello, Google Test!";
    buffer.writeString(testData);

    // 写入文件
    FileOutputStream fos(testFileName);
    fos.write(buffer);
    fos.flush();
    fos.close();

    // 读取文件内容并验证
    std::ifstream inputFile(testFileName, std::ios::binary);
    ASSERT_TRUE(inputFile.is_open());

    std::string fileContent((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    inputFile.close();

    EXPECT_EQ(testData, fileContent);
}

TEST_F(FileOutputStreamTest, WriteLargeData)
{
    size_t largeSize = 100 * 1024 * 1024; // 10 MB
    // 准备大量测试数据
    ByteBuffer buffer(largeSize);
    
    for (size_t size = 0; size < largeSize; ++size)
    {
        buffer.append('A');
    }


    // 写入文件
    FileOutputStream fos(testFileName);
    fos.write(buffer);
    fos.flush();
    fos.close();

    // 读取文件内容并验证
    std::ifstream inputFile(testFileName, std::ios::binary);
    ASSERT_TRUE(inputFile.is_open());

    std::vector<uint8_t> fileContent((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    inputFile.close();

    ASSERT_EQ(largeSize, fileContent.size());
    for (size_t i = 0; i < largeSize; ++i)
    {
        EXPECT_EQ('A', fileContent[i]);
    }
}
