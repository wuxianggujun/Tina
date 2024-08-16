#include "io/Buffer.hpp"
#include<gtest/gtest.h>

using namespace Tina;

class BufferTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(BufferTest, Constructor) {
    Buffer<int> buffer(10);
    EXPECT_EQ(buffer.size(), 0);
    EXPECT_EQ(buffer.capacity(), 10);

    Buffer<int> buffer2(0);
    EXPECT_EQ(buffer2.capacity(), 0);
    EXPECT_EQ(buffer2.size(), 0);

    // 边界测试：外部内存
    int externalMem[5] = {1, 2, 3, 4, 5};
    Buffer<int> buf3(externalMem, 5);
    EXPECT_EQ(buf3.capacity(), 5);
    EXPECT_EQ(buf3.size(), 0);
}

TEST_F(BufferTest, Resize) {
    Buffer<int> buf(10);

    // 正常路径测试：扩大容量
    buf.resize(20);
    EXPECT_EQ(buf.capacity(), 20);
    EXPECT_EQ(buf.size(), 0);

    // 正常路径测试：缩小容量
    buf.resize(5);
    EXPECT_EQ(buf.capacity(), 20);
    EXPECT_EQ(buf.size(), 5);

    // 边界测试：调整到0
    buf.setCapacity(0);
    EXPECT_EQ(buf.capacity(), 0);
    EXPECT_EQ(buf.size(), 0);
}

// 测试Buffer的assign函数
TEST_F(BufferTest, Assign) {
    Buffer<int> buf(10);
    int data[5] = {1, 2, 3, 4, 5};

    // 正常路径测试
    buf.assign(data, 5);
    EXPECT_EQ(buf.size(), 5);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(buf[i], data[i]);
    }

    // 边界测试：assign空数据
    buf.assign(data, 0);
    EXPECT_EQ(buf.size(), 5);
}

// 测试Buffer的append函数
TEST_F(BufferTest, Append) {
    Buffer<int> buf(10);
    int data[5] = {1, 2, 3, 4, 5};

    // 正常路径测试
    buf.append(data, 5);
    EXPECT_EQ(buf.size(), 5);
    for (int i = 0; i < 5; ++i) {
        auto var = buf[i];
        auto var2 = data[i];
        EXPECT_EQ(var, var2);
    }

    // 边界测试：append空数据
    buf.append(data, 0);
    EXPECT_EQ(buf.size(), 5);

    // 边界测试：append到满
    buf.append(data, 5);
    EXPECT_EQ(buf.size(), 10);
}

// 测试Buffer的clear函数
TEST_F(BufferTest, Clear) {
    Buffer<int> buf(10);
    int data[5] = {1, 2, 3, 4, 5};
    buf.assign(data, 5);

    // 正常路径测试
    buf.clear();
    EXPECT_EQ(buf.size(), 0);
    EXPECT_EQ(buf.capacity(), 0);
}

TEST_F(BufferTest, Read) {
    Buffer<int> buf(10);
    int data[5] = {1, 2, 3, 4, 5};
    buf.append(data, 5);
    

    for (int i = 0; i < 5; ++i) {
        auto var = buf[i];
        auto bufVar = buf.read();
    }

    buf.setReadPos();

    int temp[5];
    buf.read(temp, 5);
    for (int i = 0; i < 5; ++i) {
        auto bufVar = temp[i];
    }
    

    
    
}


