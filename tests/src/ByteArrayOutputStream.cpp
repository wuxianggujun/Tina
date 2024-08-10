//
// Created by wuxianggujun on 2024/8/10.
//
#include <gtest/gtest.h>
#include "filesystem/ByteArrayOutputStream.hpp"
#include "filesystem/File.hpp"

using namespace Tina;

class ByteArrayOutputStreamTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_byteArrayOutputStream = std::make_unique<ByteArrayOutputStream>();
    }

    void TearDown() override
    {
        m_byteArrayOutputStream.reset();
    }

    std::unique_ptr<ByteArrayOutputStream> m_byteArrayOutputStream;
};

TEST_F(ByteArrayOutputStreamTest, test_write_byte)
{
    
    auto* bytes = new Buffer<Byte>(512);
    
    for (int i = 0; i < 1024; ++i)
    {
        m_byteArrayOutputStream->write(Byte(0x01));
    }

    m_byteArrayOutputStream->writeBytes(bytes);

    
    
}
