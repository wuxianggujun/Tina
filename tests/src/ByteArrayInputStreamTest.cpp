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
    Bytes bytes(1024 * 1024 * 10);
    for (int i = 0; i < 1024 * 1024 * 10; i++) {
        bytes.append(Byte(i));
    }

   auto test = createScopePtr<Buffer<Byte>>(bytes.size());


   bytes.read(test.get(), bytes.size());

   Byte b;
   int t;
   for (int i = 0; i <test.get()->size(); i++)
   {
       b = bytes.read();
       t = i;
   }
   printf("%ld\n", t);

    //ByteArrayInputStream inputStream(bytes);
}
