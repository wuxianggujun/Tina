//
// Created by wuxianggujun on 2024/8/1.
// https://github.com/pocoproject/poco/blob/1eebd46c0487356fc3c89991ddc160ab97a6f001/Foundation/include/Poco/Path.h
//

#pragma once

#include "base/String.hpp"

namespace Tina {
    class Path {
    public:
        explicit Path(const std::string &path);
        explicit Path(const String &path);  // 添加 String 构造函数
        explicit Path(const char* path);    // 添加 const char* 构造函数

        ~Path();

        //拷贝构造函数
        Path(const Path &other);
        Path(Path &&other) noexcept;
        Path &operator=(const Path &other);
        Path &operator=(Path &&other) noexcept;

        [[nodiscard]] Path getParentDirectory() const;
        [[nodiscard]] std::string toString() const;
        [[nodiscard]] String toTinaString() const;  // 添加转换到 String 的方法
        [[nodiscard]] std::string getFullPath() const;
        [[nodiscard]] std::string getFileName() const;
        [[nodiscard]] std::string getFileNameWithoutExtension() const;
        [[nodiscard]] std::string getExtension() const;

        [[nodiscard]] Path getChildFile(const std::string& relativePath) const;
        [[nodiscard]] Path getChildFile(const String& relativePath) const;  // 添加 String 版本
        [[nodiscard]] Path getChildFile(const char* relativePath) const;    // 添加 const char* 版本

        [[nodiscard]] Path getSiblingFile(const std::string &fileName) const;
        [[nodiscard]] Path getSiblingFile(const String &fileName) const;  // 添加 String 版本

        [[nodiscard]] bool exists() const;
        [[nodiscard]] bool isEmpty() const;

    private:
        class Impl; // 前向声明
        Impl* m_impl{}; // 使用指针，避免包含 ghc/filesystem 头文件
    };
}
