//
// Created by wuxianggujun on 2024/8/12.
//

#include "Path.hpp"

#include <algorithm>
#include <fstream>

namespace Tina
{
    const char Path::pathSeparator = PATH_SEPARATOR;
    const std::string Path::pathSeparatorString(1,PATH_SEPARATOR);

    Path::Path(const std::string& path)
    {
        fullPath = path;

        std::replace(fullPath.begin(), fullPath.end(), '\\', PATH_SEPARATOR);
    }

    Path::Path(const Path& path)
    {
        fullPath = path.fullPath;
    }

    std::string Path::getPathUpToLastSlash() const
    {
        // find last occurrence of characters
        const size_t lastSlash = fullPath.find_last_of(pathSeparator);
        if (lastSlash > 0)
        {
            return fullPath.substr(0, lastSlash);
        }
        if (lastSlash == 0)
        {
            return pathSeparatorString;
        }
        return fullPath;
    }

    Path Path::getParentDirectory() const
    {
        Path parentDirectory;
        parentDirectory.fullPath = getPathUpToLastSlash();
        return parentDirectory;
    }

    std::string Path::getFileName() const
    {
        return fullPath.substr(fullPath.find_last_of(pathSeparator) + 1);
    }

    std::string Path::getFileNameWithoutExtension() const
    {
        const size_t lastSlash = fullPath.find_last_of(pathSeparator) + 1;
        const size_t lastDot = fullPath.find_last_of('.');
        if (lastDot > lastSlash)
        {
            return fullPath.substr(lastSlash, lastDot);
        }
        return fullPath.substr(lastSlash);
    }

    std::string Path::getExtension() const
    {
        const size_t lastDot = fullPath.find_last_of('.');

        if (lastDot == std::string::npos)
            return "";
        std::string ext = fullPath.substr(lastDot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }

    Path Path::getChildFile(std::string relativePath) const
    {
        //if (isAbsolutePath (relativePath))
        //return File (relativePath);

        std::string path(fullPath);

        // It's relative, so remove any ../ or ./ bits at the start..
        if (relativePath[0] == '.')
        {
            while (relativePath[0] == '.')
            {
                const char secondChar = relativePath[1];

                if (secondChar == '.')
                {
                    const char thirdChar = relativePath[2];

                    if (thirdChar == 0 || thirdChar == pathSeparator)
                    {
                        const int lastSlash = path.find_last_of(pathSeparator);
                        if (lastSlash >= 0)
                            path = path.substr(0, lastSlash);

                        relativePath = relativePath.substr(3);
                    }
                    else
                    {
                        break;
                    }
                }
                else if (secondChar == pathSeparator)
                {
                    relativePath = relativePath.substr(2);
                }
                else
                {
                    break;
                }
            }
        }

        return Path(addTrailingSeparator(path) + relativePath);
    }

    Path Path::getSiblingFile(const std::string& fileName) const
    {
        return getParentDirectory().getChildFile (fileName);
    }


    std::string Path::addTrailingSeparator(const std::string& path) const
    {
        if (path.empty())
            return pathSeparatorString;

        if (path[path.length() - 1] != pathSeparator)
            return path + pathSeparator;

        return path;
    }

    bool Path::exists() const
    {
        std::ifstream my_file(fullPath.c_str());
        return my_file.good();
    }

    bool Path::isEmpty() const
    {
        return fullPath.empty();
    }
    
}
