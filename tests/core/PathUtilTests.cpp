#include "core/io/PathUtil.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace Tina::Tests {
namespace {

using Core::Detail::pathComponentEquals;
using Core::Detail::pathEscapesRoot;
using Core::Detail::pathFromUtf8Bytes;
using Core::Detail::pathHasParentComponent;
using Core::Detail::pathIsSameOrDescendant;
using Core::Detail::pathRelativeToAncestor;
using Core::Detail::pathsReferToSameLocation;
using Core::Detail::pathToUtf8;
using Core::Detail::pathToUtf8Generic;

// u8"" is const char8_t[], and the decoder takes UTF-8 bytes as string_view by contract. Spelling
// the literals as u8 keeps the source encoding explicit rather than trusting the compiler's
// narrow-literal charset.
[[nodiscard]] std::string_view utf8Bytes(const char8_t* text)
{
    return std::string_view{reinterpret_cast<const char*>(text)};
}

TEST(PathUtilTest, ComponentCompareFoldsCaseTheWayTheHostFilesystemDoes)
{
#if defined(_WIN32)
    EXPECT_TRUE(pathComponentEquals("Texture.png", "texture.PNG"));
#else
    EXPECT_FALSE(pathComponentEquals("Texture.png", "texture.PNG"));
#endif
    EXPECT_TRUE(pathComponentEquals("texture.png", "texture.png"));
    EXPECT_FALSE(pathComponentEquals("texture.png", "sprite.png"));
}

// Ordinal folding maps code units one for one, so a length difference is decisive. The ASCII fast
// path relies on this to answer without reaching the out-of-line ordinal compare.
TEST(PathUtilTest, ComponentCompareRejectsUnequalLengthsWithoutFolding)
{
    EXPECT_FALSE(pathComponentEquals("asset", "assets"));
    EXPECT_FALSE(pathComponentEquals("", "a"));
    EXPECT_TRUE(pathComponentEquals("", ""));
}

// This is the case std::towlower could not handle: a surrogate pair is two wchar_t on Windows, and
// folding one unit at a time cannot fold the code point at all. Ten copies of this predicate used
// to disagree about it, and the answer decides whether a path escapes an authoring root.
TEST(PathUtilTest, ComponentCompareHandlesNonAsciiAndSurrogatePairs)
{
    EXPECT_TRUE(pathComponentEquals(pathFromUtf8Bytes(utf8Bytes(u8"资源")),
                                    pathFromUtf8Bytes(utf8Bytes(u8"资源"))));
    EXPECT_FALSE(pathComponentEquals(pathFromUtf8Bytes(utf8Bytes(u8"资源")),
                                     pathFromUtf8Bytes(utf8Bytes(u8"资产"))));

    // U+1D400 MATHEMATICAL BOLD CAPITAL A -- outside the BMP, so a surrogate pair on Windows.
    EXPECT_TRUE(pathComponentEquals(pathFromUtf8Bytes(utf8Bytes(u8"\U0001D400")),
                                    pathFromUtf8Bytes(utf8Bytes(u8"\U0001D400"))));
    EXPECT_FALSE(pathComponentEquals(pathFromUtf8Bytes(utf8Bytes(u8"\U0001D400")),
                                     pathFromUtf8Bytes(utf8Bytes(u8"\U0001D401"))));
}

// A component that starts ASCII and turns non-ASCII must not be decided by the fast path alone.
TEST(PathUtilTest, ComponentCompareDoesNotStopAtTheAsciiPrefix)
{
    EXPECT_FALSE(pathComponentEquals(pathFromUtf8Bytes(utf8Bytes(u8"tex_资")),
                                     pathFromUtf8Bytes(utf8Bytes(u8"tex_产"))));
    EXPECT_TRUE(pathComponentEquals(pathFromUtf8Bytes(utf8Bytes(u8"tex_资")),
                                    pathFromUtf8Bytes(utf8Bytes(u8"tex_资"))));
}

TEST(PathUtilTest, DescendantWalkRespectsComponentBoundaries)
{
    EXPECT_TRUE(pathIsSameOrDescendant("root/a/b", "root/a"));
    EXPECT_TRUE(pathIsSameOrDescendant("root/a", "root/a"));
    EXPECT_FALSE(pathIsSameOrDescendant("root/a", "root/a/b"));

    // "root/ab" is not under "root/a": a string prefix is not a path prefix.
    EXPECT_FALSE(pathIsSameOrDescendant("root/ab", "root/a"));
}

// Inherited from the nine identical copies this predicate replaced, and call sites depend on it,
// so it is contract rather than accident: a caller that has not proven its root non-empty has no
// sandbox at all.
TEST(PathUtilTest, EmptyAncestorPermitsEverything)
{
    EXPECT_TRUE(pathIsSameOrDescendant("anything/at/all", ""));
    EXPECT_TRUE(pathIsSameOrDescendant("", ""));
}

// A trailing separator on the ancestor yields an extra empty final component, which fails against
// a candidate that has none.
//
// lexically_normal does NOT fix this. It is a purely lexical rewrite and preserves the trailing
// separator, so both sides come back with the same mismatch. Callers that need the two spellings to
// compare equal must drop the separator themselves; the assertions below pin the real behaviour so
// nobody repeats the mistake of assuming normalization is enough.
TEST(PathUtilTest, TrailingSeparatorFailsAndLexicallyNormalDoesNotRescueIt)
{
    EXPECT_FALSE(pathIsSameOrDescendant("root/a", "root/a/"));

    const auto normalized = std::filesystem::path("root/a/").lexically_normal();
    EXPECT_FALSE(pathIsSameOrDescendant(std::filesystem::path("root/a").lexically_normal(),
                                        normalized))
        << "lexically_normal(\"root/a/\") = " << pathToUtf8Generic(normalized);

    // What does work: strip the empty final component.
    EXPECT_TRUE(pathIsSameOrDescendant("root/a", normalized.parent_path()));
}

TEST(PathUtilTest, SameLocationIsMutualContainment)
{
    EXPECT_TRUE(pathsReferToSameLocation("root/a", "root/a"));
    EXPECT_FALSE(pathsReferToSameLocation("root/a", "root/a/b"));
    EXPECT_FALSE(pathsReferToSameLocation("root/a/b", "root/a"));
}

TEST(PathUtilTest, RelativeToAncestorReturnsTheRemainderOrNothing)
{
    const auto under = pathRelativeToAncestor("root/a/b/c.png", "root/a");
    ASSERT_TRUE(under.has_value());
    EXPECT_EQ(pathToUtf8Generic(*under), "b/c.png");

    // Equal paths are a descendant with an empty remainder, which is not the same as no match.
    const auto equal = pathRelativeToAncestor("root/a", "root/a");
    ASSERT_TRUE(equal.has_value());
    EXPECT_TRUE(equal->empty());

    EXPECT_FALSE(pathRelativeToAncestor("root/a", "root/a/b").has_value());
    EXPECT_FALSE(pathRelativeToAncestor("other/a", "root/a").has_value());
}

// The two escape predicates are deliberately different strengths. Using the narrow one where the
// answer decides filesystem access would let an absolute path straight through.
TEST(PathUtilTest, ParentComponentIsNarrowerThanTheFullEscapeGuard)
{
    EXPECT_TRUE(pathHasParentComponent("a/../b"));
    EXPECT_TRUE(pathHasParentComponent(".."));
    EXPECT_FALSE(pathHasParentComponent("a/b"));

    // "..foo" is a name, not a parent component.
    EXPECT_FALSE(pathHasParentComponent("..foo/b"));

    EXPECT_FALSE(pathHasParentComponent("/abs/path"));
    EXPECT_TRUE(pathEscapesRoot("/abs/path"));

    EXPECT_TRUE(pathEscapesRoot(""));
    EXPECT_TRUE(pathEscapesRoot("a/../b"));
    EXPECT_FALSE(pathEscapesRoot("a/b"));
}

TEST(PathUtilTest, Utf8RoundTripsIncludingNonAscii)
{
    const std::string text = "assets/textures/sprite.png";
    EXPECT_EQ(pathToUtf8Generic(pathFromUtf8Bytes(text)), text);

    const std::string_view nonAscii = utf8Bytes(u8"assets/资源/sprite.png");
    EXPECT_EQ(pathToUtf8Generic(pathFromUtf8Bytes(nonAscii)), nonAscii);
}

// The two encoders are not interchangeable. Everything that becomes an identity or persisted bytes
// uses the generic one, because '\' and '/' hash and compare differently across hosts; switching
// one of those call sites to pathToUtf8 would silently invalidate every existing artifact.
TEST(PathUtilTest, GenericEncoderAlwaysUsesForwardSlashes)
{
    const std::filesystem::path joined = std::filesystem::path("root") / "a" / "b.png";
    EXPECT_EQ(pathToUtf8Generic(joined), "root/a/b.png");
#if defined(_WIN32)
    EXPECT_EQ(pathToUtf8(joined), "root\\a\\b.png");
#else
    EXPECT_EQ(pathToUtf8(joined), "root/a/b.png");
#endif
}

// Decoding does not validate, and on invalid input it does not pass the bytes through either -- it
// throws. On MSVC the u8string-to-native conversion reports "No mapping for the Unicode character
// exists in the target multi-byte code page" as a std::system_error.
//
// This is why the function is not noexcept, and why callers must either validate with
// Core::isStrictUtf8 first or sit inside a try block. A noexcept caller that does neither turns a
// malformed path -- which arrives from a file dialog, i.e. outside our control -- into
// std::terminate.
TEST(PathUtilTest, DecodingThrowsOnInvalidUtf8RatherThanPassingBytesThrough)
{
    const std::string loneContinuationByte = "a\x80z";
    EXPECT_THROW((void)pathFromUtf8Bytes(loneContinuationByte), std::system_error);

    const std::string embeddedHighBytes = "\xFF\xFE";
    EXPECT_THROW((void)pathFromUtf8Bytes(embeddedHighBytes), std::system_error);

    // Valid UTF-8 of course still decodes.
    EXPECT_NO_THROW((void)pathFromUtf8Bytes(utf8Bytes(u8"资源/a.png")));
}

} // namespace
} // namespace Tina::Tests
