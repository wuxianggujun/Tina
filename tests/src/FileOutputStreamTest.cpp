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
        /*// 创建一个临时文件用于测试
        testFileName = "test_output.bin";*/
        // 创建一个用于测试的文件，并打开它
        testFile = std::make_unique<File>(testFilePath, Write | Binary);
        ASSERT_TRUE(testFile->isOpen());
    }

    void TearDown() override
    {
        // 清理工作
        if (testOutputStream)
        {
            testOutputStream->close();
        }
        if (testFile)
        {
            testFile->close();
        }
        // 移除测试文件
        //remove(testFilePath.c_str());
    }

    const std::string testFilePath = "test_output.txt";
    std::unique_ptr<File> testFile;
    std::unique_ptr<FileOutputStream> testOutputStream;
};


TEST_F(FileOutputStreamTest, WriteString)
{
    testOutputStream = std::make_unique<FileOutputStream>(testFilePath);
    std::string testData = "Hello, World!";
    testOutputStream->write(testData);
    testOutputStream->flush();
    testOutputStream->close();

    std::string context;
    File file(testFilePath, Read | Write);
    (void)file.read(context);
    file.close();
    ASSERT_FALSE(file.isOpen());
}

TEST_F(FileOutputStreamTest, WriteByteArray)
{
    auto testStream2 = std::make_unique<FileOutputStream>("test_output.bin");

    ByteBuffer testData(1024);

    // 使用索引操作直接写入数据到 ByteBuffer
    for (size_t i = 0; i < testData.capacity(); ++i)
    {
        testData.put(i, static_cast<uint8_t>(i));
    }

    testData.resize(1024);  // 确保缓冲区大小正确

    testStream2->write(testData);
    testStream2->flush();
    testStream2->close();
}
