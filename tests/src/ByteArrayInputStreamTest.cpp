#include <ctrack.hpp>
#include <gtest/gtest.h>
#include "io/ByteArrayInputStream.hpp"

using namespace Tina;
using Bytes = Buffer<Byte>;

class ByteArrayInputStreamTest : public testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};


// 测试ByteArrayInputStream的构造函数和基本读取功能
TEST_F(ByteArrayInputStreamTest, BasicReadTest) {
    CTRACK;
    Bytes bytes(1024 * 1024 * 10);
    for (int i = 0; i < 1024 * 1024 * 10; i++) {
        bytes.append(Bytes(i));
    }
    ByteArrayInputStream inputStream(bytes);
    ctrack::result_print();
}
