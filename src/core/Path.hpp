//
// 路径系统：Path / PathInfo / ResourcePath
// 目标：
// - 规范化路径：统一为正斜杠，去重连续分隔符，处理前缀 "./"
// - 固定缓冲：避免频繁堆分配（使用 Core.hpp 的 MAX_PATH）
// - 稳定哈希：基于 xxHash（见 core/Hash.hpp），用于资源查找与 .res 命名
// - 实用函数：获取目录/基名/扩展名、替换扩展、追加片段

#pragma once

#include "Core.hpp"
#include "Hash.hpp"
#include <string>
#include <string_view>
#include <cctype>

namespace Tina::Core {

// 轻量 StringView：指向 [begin, end)；可从 std::string_view 构造
struct StringView {
    const char* begin = nullptr;
    const char* end = nullptr;

    StringView() = default;
    StringView(const char* b, const char* e) : begin(b), end(e) {}
    explicit StringView(const char* cstr)
        : begin(cstr), end(cstr ? cstr + std::char_traits<char>::length(cstr) : nullptr) {}
    explicit StringView(std::string_view sv) : begin(sv.data()), end(sv.data() + sv.size()) {}

    bool empty() const { return !begin || begin == end || size() == 0; }
    size_t size() const { return begin && end ? size_t(end - begin) : 0; }
    char back() const { return *(end - 1); }
    const char& operator[](size_t i) const { return begin[i]; }

    void removeSuffix(size_t n) { if (size() >= n) end -= n; }
};

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
    explicit PathInfo(StringView path);
    StringView extension;
    StringView basename;
    StringView dir;
};

struct Path {
    // 规范化：返回写入结束位置指针
    static char* normalize(StringView in_path, char* out, u32 out_capacity);
    static char* normalize(char* in_out_path);

    static StringView getDir(StringView src);
    static StringView getBasename(StringView src);
    static StringView getExtension(StringView src);
    static bool hasExtension(StringView filename, StringView ext);
    static bool replaceExtension(char* path, const char* ext);
    static bool isSame(StringView a, StringView b);

    Path();
    explicit Path(StringView path);

    // 赋值
    void operator=(StringView rhs);
    bool operator==(const char* rhs) const;
    bool operator!=(const char* rhs) const;
    bool operator==(const Path& rhs) const;
    bool operator!=(const Path& rhs) const;

    u32 length() const { return m_length; }
    FilePathHash getHash() const { return m_hash; }
    const char* c_str() const { return m_path; }
    bool isEmpty() const { return m_path[0] == '\0'; }
    static u32 capacity() { return MAX_PATH; }
    operator StringView() const { return StringView(m_path, m_path + m_length); }

    // 片段追加（字符串 / u64）
    void append(StringView s);
    void append(const char* s) { append(StringView(s)); }
    void append(u64 v);

    // 手动编辑
    char* beginUpdate() { return m_path; }
    void endUpdate();

private:
    void recalc();

    char m_path[MAX_PATH]{};
    u32 m_length = 0;
    FilePathHash m_hash{};
};

struct ResourcePath {
    static StringView getSubresource(StringView str);
    static StringView getResource(StringView str);
};

} // namespace Tina::Core

