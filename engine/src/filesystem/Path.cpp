//
// Created by wuxianggujun on 2024/8/12.
//

#include "Path.hpp"
// #define NOMINMAX
#include "FileSystem.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace Tina
{
    class Path::Impl
    {
    public:
        explicit Impl(const std::string& path): m_path(path)
        {
        }

        explicit Impl(const String& path): m_path(path.c_str())
        {
        }

        explicit Impl(const char* path): m_path(path)
        {
        }

        explicit Impl(ghc::filesystem::path path): m_path(std::move(path))
        {
        }

        explicit Impl(const Path& other): m_path(other.m_impl->m_path)
        {
        }

        explicit Impl(Path&& other) noexcept: m_path(std::move(other.m_impl->m_path))
        {
        }

        Impl(const Impl& other) : m_path(other.m_path) {}

        Impl& operator=(const Impl& other)
        {
            if (this != &other)
            {
                m_path = other.m_path;
            }
            return *this;
        }

        Impl& operator=(Impl&& other) noexcept
        {
            if (this != &other)
            {
                m_path = std::move(other.m_path);
            }
            return *this;
        }

        ghc::filesystem::path m_path;
    };


    Path::Path(const std::string& path): m_impl(new Impl(path))
    {

    }

    Path::Path(const String& path): m_impl(new Impl(path))
    {

    }

    Path::Path(const char* path): m_impl(new Impl(path))
    {

    }

    Path::Path(const Path &other): m_impl(new Impl(*other.m_impl)) {
    }

    Path::Path(Path&& other) noexcept : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
    }

    Path::~Path()
    {
        delete m_impl;
    }

    Path& Path::operator=(const Path& other)
    {
        if (this != &other)
        {
            *m_impl = *other.m_impl;
        }
        return *this;
    }

    Path &Path::operator=(Path &&other) noexcept {
        if (this != &other) {
            delete m_impl;
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }
        return *this;
    }

    Path Path::getParentDirectory() const {
        return Path(m_impl->m_path.parent_path().string());
    }


    std::string Path::toString() const {
        return m_impl->m_path.string();
    }

    String Path::toTinaString() const {
        return String(toString());
    }

    std::string Path::getFullPath() const {
        return m_impl->m_path.string();
    }

    std::string Path::getFileName() const {
        return m_impl->m_path.filename().string();
    }

    std::string Path::getFileNameWithoutExtension() const {
        return m_impl->m_path.stem().string();
    }

    std::string Path::getExtension() const {
        return m_impl->m_path.extension().string();
    }

    Path Path::getChildFile(const std::string& relativePath) const {
        return Path((m_impl->m_path / relativePath).string());
    }

    Path Path::getChildFile(const String& relativePath) const {
        return Path((m_impl->m_path / relativePath.c_str()).string());
    }

    Path Path::getChildFile(const char* relativePath) const {
        return Path((m_impl->m_path / relativePath).string());
    }

    Path Path::getSiblingFile(const std::string &fileName) const {
        return Path((m_impl->m_path.parent_path() / fileName).string());
    }

    Path Path::getSiblingFile(const String& fileName) const {
        return Path((m_impl->m_path.parent_path() / fileName.c_str()).string());
    }

    bool Path::exists() const {
        return ghc::filesystem::exists(m_impl->m_path);
    }

    bool Path::isEmpty() const {
        return m_impl->m_path.empty();
    }
}
