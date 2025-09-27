#include "Path.hpp"
#include <cstring>

namespace Tina::Core {

static inline bool is_slash(char c) { return c == '/' || c == '\\'; }

Path::Path() : m_path{} { recalc(); }

Path::Path(StringView path) {
    m_length = u32(normalize(path, m_path, MAX_PATH) - m_path);
    recalc();
}

void Path::recalc() {
    // 使用小写哈希，保证跨平台稳定
    m_hash = FilePathHash(Hash::StringLower64(std::string_view(m_path, m_length)));
}

char* Path::normalize(char* path) {
    bool prev_slash = false;
    char* dst = path;
    const char* src = dst;
    // 跳过 "./"
    if (src[0] == '.' && (src[1] == '\\' || src[1] == '/')) src += 2;
#ifdef _WIN32
    while (*src && is_slash(*src)) ++src; // 跳过开头的斜杠
#endif
    while (*src) {
        const bool cur_slash = is_slash(*src);
        if (cur_slash && prev_slash) { ++src; continue; }
        *dst = (*src == '\\') ? '/' : *src;
        prev_slash = cur_slash;
        ++src; ++dst;
    }
    *dst = '\0';
    return dst;
}

char* Path::normalize(StringView in_path, char* out, u32 out_capacity) {
    if (!out || out_capacity == 0) return out;
    char* dst = out;
    const char* src = in_path.data();
    const char* const end = in_path.data() + in_path.size();
    if (!src) { *dst = '\0'; return dst; }
    if (in_path.size() == 0) { *dst = '\0'; return dst; }

    // skip "./"
    if ((end - src) > 1 && src[0] == '.' && is_slash(src[1])) src += 2;
#ifdef _WIN32
    while (src < end && is_slash(*src)) ++src;
#endif
    bool prev_slash = false;
    u32 written = 0;
    while (src < end && written + 1 < out_capacity) {
        const bool cur_slash = is_slash(*src);
        if (cur_slash && prev_slash) { ++src; continue; }
        *dst = (*src == '\\') ? '/' : *src;
        prev_slash = cur_slash;
        ++src; ++dst; ++written;
    }
    *dst = '\0';
    return dst;
}

void Path::endUpdate() {
    m_length = u32(normalize(m_path) - m_path);
    recalc();
}

void Path::operator=(StringView rhs) {
    m_length = u32(normalize(rhs, m_path, MAX_PATH) - m_path);
    recalc();
}

bool Path::operator==(const char* rhs) const { return std::strcmp(rhs ? rhs : "", m_path) == 0; }
bool Path::operator!=(const char* rhs) const { return !(*this == rhs); }
bool Path::operator==(const Path& rhs) const { return m_hash == rhs.m_hash; }
bool Path::operator!=(const Path& rhs) const { return !(*this == rhs); }

void Path::append(StringView s) {
    const size_t cur = m_length;
    const size_t cap = MAX_PATH;
    if (!s.begin) return;
    size_t n = s.size(); if (n == 0) return;
    size_t to_copy = (cur + n + 1 <= cap) ? n : (cap - cur - 1);
    if (to_copy > 0) {
        std::memcpy(m_path + cur, s.begin, to_copy);
        m_length = u32(cur + to_copy);
        m_path[m_length] = '\0';
        endUpdate();
    }
}

static inline char* u64ToDec(char* out, char* end, u64 v) {
    char buf[32];
    int i = 0; if (v == 0) buf[i++] = '0';
    while (v && i < 32) { buf[i++] = char('0' + (v % 10)); v /= 10; }
    while (i-- > 0 && out < end - 1) { *out++ = buf[i]; }
    *out = '\0'; return out;
}

void Path::append(u64 v) {
    char* out = m_path + m_length;
    char* end = m_path + MAX_PATH;
    out = u64ToDec(out, end, v);
    m_length = u32(out - m_path);
    endUpdate();
}

// 工具：目录、基名、扩展名
StringView Path::getDir(StringView src) {
    if (src.empty()) return src;
    const char* begin = src.data();
    const char* end = begin + src.size();
    if (end > begin) --end;
    while (end > begin && !is_slash(*(end - 1))) --end;
    return StringView(begin, (size_t)(end - begin));
}

StringView Path::getBasename(StringView src) {
    if (src.empty()) return src;
    const char* begin = src.data();
    const char* end = begin + src.size();
    if (end > begin && is_slash(*(end - 1))) --end;
    const char* b = end - 1;
    while (b != begin && !is_slash(*b)) --b;
    if (is_slash(*b)) ++b;
    const char* e = b; while (e != end && *e != '.') ++e;
    return StringView(b, (size_t)(e - b));
}

StringView Path::getExtension(StringView src) {
    if (src.empty()) return StringView();
    const char* begin = src.data();
    const char* end = begin + src.size();
    const char* b = end - 1;
    while (b != begin && *b != '.') --b;
    if (*b != '.') return StringView();
    return StringView(b + 1, (size_t)(end - (b + 1)));
}

bool Path::hasExtension(StringView filename, StringView ext) {
    StringView e = getExtension(filename);
    if (!e.begin || !ext.begin) return false;
    if (e.size() != ext.size()) return false;
    for (size_t i = 0; i < e.size(); ++i) {
        char a = (char)std::tolower((unsigned char)e[i]);
        char b = (char)std::tolower((unsigned char)ext[i]);
        if (a != b) return false;
    }
    return true;
}

bool Path::replaceExtension(char* path, const char* ext) {
    if (!path || !ext) return false;
    char* end = path + std::char_traits<char>::length(path);
    while (end > path && *end != '.') --end;
    if (*end != '.') return false;
    ++end; while (*ext && *end) { *end++ = *ext++; }
    if (*ext) return false; *end = '\0'; return true;
}

bool Path::isSame(StringView a, StringView b) {
    if (a.size() > 0 && is_slash(a.back())) a.removeSuffix(1);
    if (b.size() > 0 && is_slash(b.back())) b.removeSuffix(1);
    if (a.size() == 0 && b.size() == 1 && b[0] == '.') return true;
    if (b.size() == 0 && a.size() == 1 && a[0] == '.') return true;
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
    return true;
}

// PathInfo
PathInfo::PathInfo(StringView path) {
    extension = Path::getExtension(path);
    basename = Path::getBasename(path);
    dir = Path::getDir(path);
}

// ResourcePath：解析 "sub:resource"
StringView ResourcePath::getResource(StringView str) {
    const char* b = str.data();
    const char* e = b + str.size();
    const char* c = b;
    while (c != e) { if (*c == ':') return StringView(c + 1, (size_t)(e - (c + 1))); ++c; }
    return str;
}

StringView ResourcePath::getSubresource(StringView str) {
    const char* b = str.data();
    const char* e = b + str.size();
    const char* c = b;
    while (c != e && *c != ':') ++c;
    return StringView(b, (size_t)(c - b));
}

} // namespace Tina::Core
