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
    }

    void TearDown() override
    {
    }

    std::string testFileName = "Test.b";
};


TEST_F(FileOutputStreamTest, WriteSingleByte)
{
    FileOutputStream ostrm(testFileName);
    Byte bytes[] = { Byte(65), Byte(66), Byte(67), Byte(68) };
    ostrm.write(bytes,4);
    ostrm.flush();
    ostrm.close();
      
}
