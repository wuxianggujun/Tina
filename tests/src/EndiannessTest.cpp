//
// Created by wuxianggujun on 2024/7/29.
//

#include <gtest/gtest.h>
#include "tool/Endianness.hpp"

using namespace Tina::Tool;

bool IsLittleEndian();

TEST(EndiannessTest, ConvertUint32) {
    uint32_t original = 0x12345678;
    uint32_t converted = EndianConvert(original);
    EXPECT_EQ(converted, 0x78563412);
}

TEST(EndiannessTest, ConvertUint16) {
    uint16_t original = 0xABCD;
    uint16_t converted = EndianConvert(original);
    EXPECT_EQ(converted, 0xCDAB);
}

TEST(EndiannessTest, ConvertInt32) {
    int32_t original = -1; // 0xFFFFFFFF
    int32_t converted = EndianConvert(original);
    EXPECT_EQ(converted, -1); // 0xFFFFFFFF on both big-endian and little-endian
}

TEST(EndiannessTest, ConvertUint64) {
    uint64_t original = 0x123456789ABCDEF0;
    uint64_t converted = EndianConvert(original);
    if (IsLittleEndian()) {
        EXPECT_EQ(converted, 0xF0DEBC9A78563412);
    } else {
        EXPECT_EQ(converted, original);
    }
}

// Helper function to check if the system is little-endian
bool IsLittleEndian() {
    uint32_t value = 1;
    return *reinterpret_cast<uint8_t*>(&value) == 1;
}