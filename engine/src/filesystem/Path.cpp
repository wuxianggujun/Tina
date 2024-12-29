//
// Created by wuxianggujun on 2024/8/12.
//

#include "Path.hpp"

#include <algorithm>
#include <fstream>

namespace Tina {
    Path::Path(const std::string &path): m_path(path) {
    }

    Path::Path(const ghc::filesystem::path &path): m_path(path) {
    }

    Path::Path(const Path &other): m_path(other.m_path) {
    }

    Path::Path(Path &&other) noexcept : m_path(std::move(other.m_path)) {
    }

    Path &Path::operator=(const Path &other) {
        if (this != &other) {
            m_path = other.m_path;
        }
        return *this;
    }

    Path &Path::operator=(Path &&other) noexcept {
        if (this != &other) {
            m_path = std::move(other.m_path);
        }
        return *this;
    }

    Path Path::getParentDirectory() const {
        return Path(m_path.parent_path());
    }

#ifdef GHC_OS_WINDOWS
    std::wstring Path::toString() const {
        return m_path.wstring();
    }
#else
    std::string Path::toString() const {
        return m_path.string();
    }
#endif

    std::string Path::getFullPath() const {
        return m_path.string();
    }
    
    const ghc::filesystem::path & Path::getPath() const {
        return m_path;
    }

    std::string Path::getFileName() const {
        return m_path.filename().string();
    }

    std::string Path::getFileNameWithoutExtension() const {
        return m_path.stem().string();
    }

    std::string Path::getExtension() const {
        return m_path.extension().string();
    }

    Path Path::getChildFile(std::string relativePath) const {
        return Path(m_path / relativePath);
    }

    Path Path::getSiblingFile(const std::string &fileName) const {
        return Path(m_path.parent_path() / fileName); // 显式构造 Path 对象
    }


    bool Path::exists() const {
        return ghc::filesystem::exists(m_path);
    }

    bool Path::isEmpty() const {
        return m_path.empty();
    }
}
