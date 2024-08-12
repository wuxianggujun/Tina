//
// Created by wuxianggujun on 2024/8/1.
// https://github.com/pocoproject/poco/blob/1eebd46c0487356fc3c89991ddc160ab97a6f001/Foundation/include/Poco/Path.h
//

#ifndef TINA_FILESYSTEM_PATH_HPP
#define TINA_FILESYSTEM_PATH_HPP

#include <string>

namespace Tina
{
#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif
    
    class InputStream;

    class Path
    {
    public:
        explicit Path(std::string path = "");
        Path(const Path& path);

        [[nodiscard]] Path getParentDirectory() const;
        [[nodiscard]] std::string getPathUpToLastSlash() const;


        [[nodiscard]] std::string getFullPath() const { return fullPath; }

        [[nodiscard]] std::string getFileName() const;
        [[nodiscard]] std::string getFileNameWithoutExtension() const;
        [[nodiscard]] std::string getExtension() const;
        [[nodiscard]] Path getChildFile(std::string relativePath) const;
        Path getSibingFile(std::string& fileName) const;
        [[nodiscard]] std::string addTrailingSparator(const std::string& path) const;

        static const char pathSeparator;
        static const std::string pathSeparatorString;

        InputStream* createInputStream() const;
        bool exists();
        bool isEmpty();

    private:
        std::string fullPath;
    };
}


#endif //TINA_FILESYSTEM_PATH_HPP
