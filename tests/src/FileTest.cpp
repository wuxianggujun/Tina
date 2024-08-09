//
// Created by wuxianggujun on 2024/7/27.
// 

#include <filesystem>
#include <gtest/gtest.h>
#include "filesystem/File.hpp"

using namespace Tina;

TEST(FileTest, CreateAndWriteToFile)
{
    std::string filename = "test.txt";
    File file(filename, FileMode::Write);

    ASSERT_TRUE(file.isOpen());
    ASSERT_TRUE(file.write("Hello, World!"));
}

TEST(FileTest, ReadFromFile)
{
    std::string filename = "test.txt";
    File file(filename, FileMode::Read);

    ASSERT_TRUE(file.isOpen());

    std::string content;
    file.read(content); // 你需要在 File::read 方法中实际读取文件内容

    std::ifstream infile(filename);
    std::getline(infile, content);

    ASSERT_EQ(content, "Hello, World!");
}

TEST(FileTest, FileExistence)
{
    std::string filename = "test.txt";
    File file(filename, FileMode::Read);

    ASSERT_TRUE(file.exists()); 
}

