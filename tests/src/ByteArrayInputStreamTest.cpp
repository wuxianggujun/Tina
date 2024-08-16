#include <gtest/gtest.h>
#include "io/ByteArrayInputStream.hpp"

using namespace Tina;
using Bytes = Buffer<Byte>;

class ByteArrayInputStreamTest : public testing::Test
{
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};


// 测试ByteArrayInputStream的构造函数和基本读取功能
TEST_F(ByteArrayInputStreamTest, BasicReadTest)
{
    // 准备测试数据
    Buffer<Byte> testData(4);
    testData.resize(0);
    testData.append(Byte(0x01));
    testData.append(Byte(0x02));
    testData.append(Byte(0x03));
    testData.append(Byte(0x04));

    auto test = testData;


    //= {0x01, 0x02, 0x03, 0x04};
    ByteArrayInputStream stream(testData);

    auto data = stream.read();

    if (data.getData() != 0x01)
    {
    }

    stream.close();
    /*// 测试读取单个字节
    EXPECT_EQ(stream.read(), 0x01);
    EXPECT_EQ(stream.read(), 0x02);

    // 测试读取多个字节
    Bytes bytesRead = stream.read(2);
    EXPECT_EQ(bytesRead.size(), 2);
    EXPECT_EQ(bytesRead[0], 0x03);
    EXPECT_EQ(bytesRead[1], 0x04);*/
}
