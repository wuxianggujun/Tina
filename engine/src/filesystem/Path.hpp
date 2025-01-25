//
// Created by wuxianggujun on 2024/8/1.
// https://github.com/pocoproject/poco/blob/1eebd46c0487356fc3c89991ddc160ab97a6f001/Foundation/include/Poco/Path.h
//

#pragma once

#include <string>

namespace Tina {
    class Path {
    public:
        explicit Path(const std::string &path);

        ~Path();

        //拷贝构造函数
        Path(const Path &other);

        Path(Path &&other) noexcept;

        Path &operator=(const Path &other);

        Path &operator=(Path &&other) noexcept;


        [[nodiscard]] Path getParentDirectory() const;

        [[nodiscard]] std::string toString() const;

        // 返回完整路径的字符串形式
        [[nodiscard]] std::string getFullPath() const;

        [[nodiscard]] std::string getFileName() const;

        [[nodiscard]] std::string getFileNameWithoutExtension() const;

        [[nodiscard]] std::string getExtension() const;

        [[nodiscard]] Path getChildFile(const std::string& relativePath) const;

        Path getSiblingFile(const std::string &fileName) const;

        bool exists() const;

        bool isEmpty() const;

    private:
        class Impl; // 前向声明
        Impl* m_impl{}; // 使用指针，避免包含 ghc/filesystem 头文件
    };
}
