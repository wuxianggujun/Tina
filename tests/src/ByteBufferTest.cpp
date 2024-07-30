#include "filesystem/ByteBuffer.hpp"
#include "gtest/gtest.h"

#include <cstdio>
#include <string>

using namespace Tina;

class ByteBufferTest : public ::testing::Test
{
protected:
    ByteBuffer buffer_;
};



/*
TEST_F(ByteBufferTest, ReadString)
{
    buffer_.append("Test\0String", 11);

    // 查看初始读取位置的数据
    const uint8_t* initialData = buffer_.peek();
    printf("Initial Data: ");
    for (size_t i = 0; i < buffer_.size(); ++i)
    {
        printf("%c", initialData[i]);
    }
    printf("\n");

    // 读取字符串
    std::string str = buffer_.readString();
    printf("Read String: %s\n", str.c_str());

    // 查看读取后的数据
    const uint8_t* postReadData = buffer_.peek();
    printf("Post Read Data: ");
    for (size_t i = 0; i < buffer_.size(); ++i)
    {
        printf("%c", postReadData[i]);
    }
    printf("\n");

    // 验证字符串内容
    EXPECT_EQ(str, "Test");

    // 验证读取位置
    EXPECT_EQ(buffer_.getReadPos(), 4);
}
*/


TEST_F(ByteBufferTest, ReadSkipString)
{
    buffer_.append("Test\0String", 11);

    // 跳过一部分数据
    buffer_.skipBytes(5); // Skip "Test\0"

    // 读取字符串
    std::string str = buffer_.readString();
    printf("Read String: %s\n", str.c_str());

    // 验证字符串内容
    EXPECT_EQ(str, "String");

    // 验证读取位置
    EXPECT_EQ(buffer_.getReadPos(), 11); // 11 = length of "Test\0String"

    buffer_.skipBytes(5); // Skip "Test\0"
}


int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
