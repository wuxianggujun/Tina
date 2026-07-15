//
// 路径系统：Path / PathInfo / ResourcePath
// 目标：
// - 规范化路径：统一为正斜杠，去重连续分隔符，处理前缀 "./"
// - 固定缓冲：避免频繁堆分配（使用 Core::MaxPathLength）
// - 稳定哈希：基于 xxHash（见 core/Hash.hpp），用于资源查找与 .res 命名
// - 实用函数：获取目录/基名/扩展名、替换扩展、追加片段

#pragma once

#include "Core.hpp"
#include "Hash.hpp"
#include <string>
#include <cctype>
#include "StringView.hpp"

namespace Tina::Core {

// 文件路径哈希包装
struct FilePathHash {
    u64 value = 0;
    FilePathHash() = default;
    explicit FilePathHash(u64 v) : value(v) {}
    u64 get() const { return value; }
    bool operator==(const FilePathHash& rhs) const { return value == rhs.value; }
    bool operator!=(const FilePathHash& rhs) const { return value != rhs.value; }
    static FilePathHash fromU64(u64 v) { return FilePathHash(v); }
};

struct PathInfo {
    explicit PathInfo(string_view path);
    string_view extension;
    string_view basename;
    string_view dir;
};

struct Path {
    // 规范化：返回写入结束位置指针
    static char* normalize(string_view in_path, char* out, u32 out_capacity);
    static char* normalize(char* in_out_path);

    static string_view getDir(string_view src);
    static string_view getBasename(string_view src);
    static string_view getExtension(string_view src);
    static bool hasExtension(string_view filename, string_view ext);
    static bool replaceExtension(char* path, const char* ext);
    static bool isSame(string_view a, string_view b);

    Path();
    explicit Path(string_view path);

    // 赋值
    void operator=(string_view rhs);
    bool operator==(const char* rhs) const;
    bool operator!=(const char* rhs) const;
    bool operator==(const Path& rhs) const;
    bool operator!=(const Path& rhs) const;

    u32 length() const { return m_length; }
    FilePathHash getHash() const { return m_hash; }
    const char* c_str() const { return m_path; }
    bool isEmpty() const { return m_path[0] == '\0'; }
    static u32 capacity() { return MaxPathLength; }
    operator string_view() const { return string_view(m_path, m_length); }

    // 片段追加（字符串 / u64）
    void append(string_view s);
    void append(const char* s) { append(string_view(s)); }
    void append(u64 v);

    // 手动编辑
    char* beginUpdate() { return m_path; }
    void endUpdate();

private:
    void recalc();

    char m_path[MaxPathLength]{};
    u32 m_length = 0;
    FilePathHash m_hash{};
};

struct ResourcePath {
    static string_view getSubresource(string_view str);
    static string_view getResource(string_view str);
};

} // namespace Tina::Core
