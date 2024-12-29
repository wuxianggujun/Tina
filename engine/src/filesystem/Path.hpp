//
// Created by wuxianggujun on 2024/8/1.
// https://github.com/pocoproject/poco/blob/1eebd46c0487356fc3c89991ddc160ab97a6f001/Foundation/include/Poco/Path.h
//

#ifndef TINA_FILESYSTEM_PATH_HPP
#define TINA_FILESYSTEM_PATH_HPP

#include "FileSystem.hpp"
#include <string>

namespace Tina {
    class Path {
    public:
        explicit Path(const std::string &path = "");

        explicit Path(const ghc::filesystem::path &path);

        ~Path() = default;

        //拷贝构造函数
        Path(const Path &other);

        Path(Path &&other) noexcept;

        Path &operator=(const Path &other);

        Path &operator=(Path &&other) noexcept;


        [[nodiscard]] Path getParentDirectory() const;


#ifdef GHC_OS_WINDOWS
        [[nodiscard]] std::wstring toString() const;
#else
        [[nodiscard]] std::string toString() const;
#endif

        // 返回完整路径的字符串形式
        [[nodiscard]] std::string getFullPath() const;
        
        [[nodiscard]] const ghc::filesystem::path &getPath() const;

        [[nodiscard]] std::string getFileName() const;

        [[nodiscard]] std::string getFileNameWithoutExtension() const;

        [[nodiscard]] std::string getExtension() const;

        [[nodiscard]] Path getChildFile(std::string relativePath) const;

        Path getSiblingFile(const std::string &fileName) const;

        bool exists() const;

        bool isEmpty() const;

    private:
        ghc::filesystem::path m_path;
    };
}


#endif //TINA_FILESYSTEM_PATH_HPP
