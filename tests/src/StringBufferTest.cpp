#include <gtest/gtest.h>
#include "base/StringBuffer.hpp"

using namespace Tina;


TEST(StringBufferTest, Constructor) {
    StringBuffer sb1;
    StringBuffer sb2(10);
    StringBuffer sb3("test");
    StringBuffer sb4(std::string("test"));

    EXPECT_THROW(StringBuffer sb5(-1), std::invalid_argument);
}


TEST(StringBufferTest, Append) {
    StringBuffer sb("initial");
    sb.append(" appended");
    sb.append(std::string("std::string"));
    sb.append('c');
    sb.append(" partial",2,3);

    sb.append("");
    EXPECT_THROW(sb.append("test",10,1), std::out_of_range);
    EXPECT_THROW(sb.append("test",1,10), std::out_of_range);
}

TEST(StringBufferTest, ConvertEncoding) {
    StringBuffer sb;
    auto result = sb.convertEncoding("test", "GBK");
    EXPECT_EQ(result, "test");
}