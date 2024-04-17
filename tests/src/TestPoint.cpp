#include <gtest/gtest.h>
#include <vector>

TEST(abc_test,test_1) {
    EXPECT_EQ(std::abs(-1), 1);
    std::cout << "Hello" << std::endl;
}


GTEST_API_ int main(int argc,char** argv) {
    std::cout << "Running main() for gtest_main.cc\n";
    testing::InitGoogleTest(&argc,argv);
    return RUN_ALL_TESTS();
}
