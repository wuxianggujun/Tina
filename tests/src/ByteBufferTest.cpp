#include "filesystem/ByteBuffer.hpp"
#include "gtest/gtest.h"

using namespace Tina;

class ByteBufferTest : public ::testing::Test {
protected:
    ByteBuffer buffer_;
};

TEST_F(ByteBufferTest, DefaultConstructor) {
    // 检查默认构造函数是否正确初始化了读/写位置
    EXPECT_EQ(buffer_.getReadPos(), 0u);
    EXPECT_EQ(buffer_.getWritePos(), 0u);
    // 检查buffer是否已经预留了默认大小的空间
    EXPECT_EQ(buffer_.capacity(), ByteBuffer::BUFFER_DEFAULT_SIZE);
}

TEST_F(ByteBufferTest, CustomSizeConstructor) {
    size_t customSize = 1024;
    ByteBuffer buffer(customSize);
    // 检查自定义大小构造函数是否正确初始化了读/写位置
    EXPECT_EQ(buffer.getReadPos(), 0u);
    EXPECT_EQ(buffer.getWritePos(), 0u);
    // 检查buffer是否已经预留了指定大小的空间
    EXPECT_EQ(buffer.capacity(), customSize);
}

TEST_F(ByteBufferTest, Resize) {
    size_t newSize = 2048;
    buffer_.resize(newSize);
    // 检查resize是否改变了buffer的大小
    EXPECT_EQ(buffer_.size(), newSize);
    // 检查读/写位置是否被重置
    EXPECT_EQ(buffer_.getReadPos(), 0u);
    EXPECT_EQ(buffer_.getWritePos(), 0u);
}

TEST_F(ByteBufferTest, Append) {
    const std::string data = "Test data";
    buffer_.append(reinterpret_cast<const uint8_t*>(data.c_str()), data.size());
    // 检查写入位置是否正确更新
    EXPECT_EQ(buffer_.getWritePos(), data.size());
    // 检查buffer大小是否正确更新
    EXPECT_EQ(buffer_.size(), data.size());
}

TEST_F(ByteBufferTest, ReadString) {
    const std::string testData = "Hello, world!";
    buffer_.append(reinterpret_cast<const uint8_t*>(testData.c_str()), testData.size());
    std::string readData = buffer_.readString();
    // 检查读取的字符串是否与写入的相同
    EXPECT_EQ(readData, testData);
    // 检查读取位置是否正确更新
    EXPECT_EQ(buffer_.getReadPos(), testData.size());
}