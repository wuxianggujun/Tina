#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/text/Utf8.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Path predicates shared by the asset pipeline, the editor and the cooking tools. This lives in
// src/ rather than include/ because every function here names std::filesystem::path, and no public
// Tina header depends on <filesystem>; a path is published as UTF-8 text instead (see
// core/io/ApplicationPaths.hpp). Consumers reach this by having ${PROJECT_SOURCE_DIR}/src on their
// private include path, the way src/desktop and src/runtime already do.
//
// What was here before: pathComponentEquals had ten copies in two mutually incompatible Windows
// spellings -- four folding case with std::towlower and six calling ::CompareStringOrdinal. Both
// fed pathIsSameOrDescendant, which guards whether a path escapes an authoring root, so the asset
// pipeline and the editor could reach opposite verdicts on the same pair. See ADR 0040.
namespace Tina::Core::Detail {

#if defined(_WIN32)
// Ordinal case-insensitive compare of two equal-length UTF-16 sequences. Out of line so that
// <windows.h> stays in the .cpp: this header is included by ~20 translation units. Only called
// once a component is known to hold a non-ASCII code unit; pathComponentEquals handles the rest
// inline.
[[nodiscard]] bool pathComponentEqualsOrdinal(const std::wstring& left,
                                              const std::wstring& right) noexcept;
#endif

// Compares one path component the way the host filesystem does: ordinal case-insensitive on
// Windows, exact on POSIX.
//
// Windows folding is ordinal (locale-invariant, whole-string), matching NTFS. std::towlower is
// what this replaces and is wrong twice over: it is locale-sensitive, so a Turkish locale folds
// 'I' to a different code point, and it folds one wchar_t at a time, so it cannot fold a surrogate
// pair at all.
//
// The ASCII loop below is a fast path, not a second semantics. Asset paths are almost entirely
// ASCII, and it keeps the common case out of a cross-DLL call. One pass suffices because ordinal
// folding is positional: if both units at some index are ASCII and differ after folding, the
// strings differ regardless of what follows.
[[nodiscard]] inline bool pathComponentEquals(const std::filesystem::path& left,
                                              const std::filesystem::path& right) noexcept
{
#if defined(_WIN32)
    const std::wstring& leftText = left.native();
    const std::wstring& rightText = right.native();
    // Ordinal folding maps code units one for one, so unequal lengths cannot compare equal. All
    // ten previous copies relied on this too.
    if (leftText.size() != rightText.size())
    {
        return false;
    }
    for (usize index = 0; index < leftText.size(); ++index)
    {
        const wchar_t leftUnit = leftText[index];
        const wchar_t rightUnit = rightText[index];
        if (((static_cast<unsigned>(leftUnit) | static_cast<unsigned>(rightUnit)) & 0xFF80U) != 0U)
        {
            return pathComponentEqualsOrdinal(leftText, rightText);
        }
        const wchar_t leftFolded =
            (leftUnit >= L'A' && leftUnit <= L'Z') ? static_cast<wchar_t>(leftUnit + 32) : leftUnit;
        const wchar_t rightFolded = (rightUnit >= L'A' && rightUnit <= L'Z')
                                        ? static_cast<wchar_t>(rightUnit + 32)
                                        : rightUnit;
        if (leftFolded != rightFolded)
        {
            return false;
        }
    }
    return true;
#else
    return left == right;
#endif
}

// True when candidate is ancestor itself or sits below it, compared component by component.
//
// Two properties are inherited from the nine identical copies this replaces, and callers depend on
// them, so they are contract rather than accident:
//
//   1. An empty ancestor returns true, because the loop never runs. A caller that has not proven
//      its root non-empty therefore permits everything.
//   2. A trailing separator on ancestor yields an extra empty final component, which fails against
//      a candidate that has none.
//
// On (2), note that lexically_normal does NOT remove the trailing separator -- it is a purely
// lexical rewrite and preserves it, so normalizing both sides leaves the same mismatch. A caller
// that needs the two spellings to compare equal has to drop the empty final component itself
// (parent_path on the normalized value does it), or use weakly_canonical, which resolves against
// the filesystem and therefore requires the path to exist. Neither is done here because the choice
// depends on whether the path must exist. See PathUtilTests for both behaviours pinned.
[[nodiscard]] inline bool pathIsSameOrDescendant(const std::filesystem::path& candidate,
                                                 const std::filesystem::path& ancestor) noexcept
{
    auto candidatePart = candidate.begin();
    for (auto ancestorPart = ancestor.begin(); ancestorPart != ancestor.end();
         ++ancestorPart, ++candidatePart)
    {
        if (candidatePart == candidate.end() || !pathComponentEquals(*candidatePart, *ancestorPart))
        {
            return false;
        }
    }
    return true;
}

// Mutual containment: the two paths name the same location. Neither side is normalized here, for
// the reason given on pathIsSameOrDescendant.
[[nodiscard]] inline bool pathsReferToSameLocation(const std::filesystem::path& left,
                                                   const std::filesystem::path& right) noexcept
{
    return pathIsSameOrDescendant(left, right) && pathIsSameOrDescendant(right, left);
}

// True when either path contains the other, including equality. Use this for output roots and
// state files that must be independent: a one-way containment check misses the equally dangerous
// case where the nominal file/root is an ancestor of the other path.
[[nodiscard]] inline bool pathsOverlap(const std::filesystem::path& left,
                                       const std::filesystem::path& right) noexcept
{
    return pathIsSameOrDescendant(left, right) || pathIsSameOrDescendant(right, left);
}

// The remainder of source below root, or nullopt when source is not under root. Separate from
// pathIsSameOrDescendant because building the remainder needs the iterator where the prefix walk
// stopped, which that predicate discards.
[[nodiscard]] inline std::optional<std::filesystem::path>
pathRelativeToAncestor(const std::filesystem::path& source, const std::filesystem::path& root)
{
    auto sourcePart = source.begin();
    for (auto rootPart = root.begin(); rootPart != root.end(); ++rootPart, ++sourcePart)
    {
        if (sourcePart == source.end() || !pathComponentEquals(*rootPart, *sourcePart))
        {
            return std::nullopt;
        }
    }

    std::filesystem::path relative;
    for (; sourcePart != source.end(); ++sourcePart)
    {
        relative /= *sourcePart;
    }
    return relative;
}

// True when relative contains a ".." component. Deliberately narrow: it says nothing about
// absolute paths or drive letters, so a caller guarding a sandbox needs its own is_absolute check
// alongside it, or pathEscapesRoot below.
[[nodiscard]] inline bool pathHasParentComponent(const std::filesystem::path& relative) noexcept
{
    for (const auto& component : relative)
    {
        if (component == "..")
        {
            return true;
        }
    }
    return false;
}

// True when relative cannot be trusted to stay under a root: empty, absolute, carrying a root name
// or root directory, or containing "..". This is the whole guard, unlike
// pathHasParentComponent -- prefer it wherever the answer decides filesystem access.
[[nodiscard]] inline bool pathEscapesRoot(const std::filesystem::path& relative) noexcept
{
    if (relative.empty() || relative.is_absolute() || relative.has_root_path())
    {
        return true;
    }
    return pathHasParentComponent(relative);
}

// Decodes UTF-8 bytes into a path without going through the deprecated std::filesystem::u8path.
//
// This does not validate, and on malformed input it does not pass the bytes through either: it
// throws. MSVC reports the u8string-to-native conversion failure as a std::system_error ("No
// mapping for the Unicode character exists in the target multi-byte code page"). That is why this
// is not noexcept.
//
// Callers must therefore either validate with Core::isStrictUtf8 first or sit inside a try block. A
// noexcept caller doing neither turns a malformed path -- and these arrive from file dialogs, i.e.
// from outside our control -- into std::terminate.
[[nodiscard]] inline std::filesystem::path pathFromUtf8Bytes(std::string_view text)
{
    std::u8string utf8Path;
    utf8Path.reserve(text.size());
    for (const char byte : text)
    {
        utf8Path.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path{std::move(utf8Path)};
}

// Encodes a path as UTF-8, keeping the platform's own separator.
//
// Note there are deliberately two encoders. This one is for text a human reads or a caller
// re-opens; pathToUtf8Generic is for bytes that get stored or compared. Never call path::string():
// on Windows that is the active narrow code page, not UTF-8, and it is lossy for non-ASCII.
[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

// Encodes a path as UTF-8 with '/' separators on every platform.
//
// Required wherever the result becomes an identity or persisted bytes, because '\' and '/' would
// otherwise hash and compare differently across hosts: the .tmeta source-import path (length-
// checked against SourceImportWire::MaxPathBytes), the glTF identityLocator, recipe-resolved
// payload paths, and the workspace roots the editor stores and later compares. Switching any of
// those to pathToUtf8 would silently invalidate every existing artifact.
[[nodiscard]] inline std::string pathToUtf8Generic(const std::filesystem::path& path)
{
    const std::u8string encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

// Whether a UTF-8 relative path is safe to join below a shipped-content root.
//
// This is the guard behind both Core::applicationFilePath and Core::ContentRoot::resolve.
// It has one definition because the two must agree exactly: a caller moving between them
// should not discover that one accepts "assets/" or "a/../b" and the other does not, and a
// root-escape rule that differs by call site is the shape of bug ADR 0040 documents.
//
// Unlike pathEscapesRoot above, this works on UTF-8 text rather than a std::filesystem::path
// and is therefore usable before any path object exists -- which matters because the
// rejection has to happen before the bytes reach a filesystem that might interpret them
// differently from us.
//
// Rejected, all as InvalidArgument at the call site: empty text, non-strict UTF-8, an
// embedded NUL, a '\' separator, a ':' (drive letter or ADS), and any empty, "." or ".."
// component. '\' and ':' are rejected rather than translated because accepting both
// separators would give one logical path two spellings, and only one of them survives
// reaching a POSIX filesystem.
[[nodiscard]] inline bool isSafeRelativeContentPath(std::string_view relativePath) noexcept
{
    if (relativePath.empty() || !isStrictUtf8WithoutNul(relativePath))
    {
        return false;
    }
    if (relativePath.find('\\') != std::string_view::npos ||
        relativePath.find(':') != std::string_view::npos)
    {
        return false;
    }
    // Every component is examined, including the one after a trailing separator: stopping
    // once the remainder runs out would accept "assets/", whose final component is empty and
    // names no file. A leading '/' is absolute and shows up here as an empty first
    // component, and "//" as an empty middle one.
    usize start = 0;
    for (;;)
    {
        const usize separator = relativePath.find('/', start);
        const std::string_view component = separator == std::string_view::npos
                                               ? relativePath.substr(start)
                                               : relativePath.substr(start, separator - start);
        if (component.empty() || component == "." || component == "..")
        {
            return false;
        }
        if (separator == std::string_view::npos)
        {
            return true;
        }
        start = separator + 1U;
    }
}

// Joins a validated relative path below a base directory, in the one spelling both
// applicationFilePath and ContentRoot::resolve produce.
//
// Only a root directory arrives already separator-terminated, and stripping that separator
// would change which directory it names, so this adds one rather than normalising the base
// to a single spelling. The caller must have already run isSafeRelativeContentPath.
[[nodiscard]] inline std::string joinContentPath(std::string base, std::string_view relativePath)
{
    if (!base.empty() && base.back() != '/' && base.back() != '\\')
    {
        base.push_back('/');
    }
    base.append(relativePath);
    return base;
}

} // namespace Tina::Core::Detail
