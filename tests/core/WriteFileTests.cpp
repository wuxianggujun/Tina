#include <tina/core/io/ReadFile.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <array>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

namespace Tina::Core {
namespace {

class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] Tina::Core::usize outstandingAllocations() const noexcept
    {
        return m_outstanding;
    }

  private:
    void* do_allocate(Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        ++m_outstanding;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* p, Tina::Core::usize bytes, Tina::Core::usize alignment) override
    {
        --m_outstanding;
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
    Tina::Core::usize m_outstanding = 0;
};

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

TEST(WriteFileTests, AtomicWriteThenReadRoundTrip)
{
    TrackingMemoryResource resource;
    const auto path = std::filesystem::temp_directory_path() / "tina_writefile_tests"
        / std::filesystem::path{u8"\u76ee\u5f55"} / std::filesystem::path{u8"\u6570\u636e.bin"};
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path().parent_path(), ec);

    constexpr char Content[] = "tina-writefile-atomic";
    const auto bytes = std::as_bytes(std::span<const char>(Content, sizeof(Content) - 1U));
    const auto status = writeFile(toUtf8(path), bytes, WriteFileConfig{.atomicReplace = true, .createParents = true});
    ASSERT_TRUE(status.has_value()) << status.error().message;

    auto read = readFile(toUtf8(path), ReadFileConfig{.maxBytes = 64, .memoryResource = &resource});
    ASSERT_TRUE(read.has_value()) << read.error().message;
    ASSERT_EQ(read->size(), sizeof(Content) - 1U);
    EXPECT_EQ((*read)[0], std::byte{'t'});
    EXPECT_EQ((*read)[read->size() - 1U], std::byte{'c'});

    std::filesystem::remove_all(path.parent_path().parent_path(), ec);
}

TEST(WriteFileTests, RejectsEmptyPath)
{
    constexpr std::byte Byte{1};
    const auto status = writeFile("", std::span<const std::byte>(&Byte, 1U));
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, CoreErrorCode::InvalidArgument);
}

TEST(WriteFileTests, AtomicPartsWriteConcatenatesWithoutIntermediateBuffer)
{
    const auto path = std::filesystem::temp_directory_path() / "tina_writefile_parts_tests" / "parts.bin";
    const std::array first{std::byte{1}, std::byte{2}};
    const std::array last{std::byte{3}};
    const std::array parts{std::span<const std::byte>{first}, std::span<const std::byte>{},
                          std::span<const std::byte>{last}};
    ASSERT_TRUE(writeFileParts(toUtf8(path), parts));
    auto bytes = readFile(toUtf8(path), {.memoryResource = std::pmr::new_delete_resource()});
    ASSERT_TRUE(bytes);
    EXPECT_EQ(*bytes, (std::pmr::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}}));
    ASSERT_TRUE(writeFileParts(toUtf8(path), {}));
    EXPECT_EQ(std::filesystem::file_size(path), 0U);
    std::error_code error;
    std::filesystem::remove_all(path.parent_path(), error);
    EXPECT_FALSE(error);
}

TEST(WriteFileTests, OverwriteExistingAtomically)
{
    TrackingMemoryResource resource;
    const auto path = std::filesystem::temp_directory_path() / "tina_writefile_tests" / "overwrite.bin";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    constexpr char First[] = "first";
    constexpr char Second[] = "second-value";
    ASSERT_TRUE(writeFile(toUtf8(path), std::as_bytes(std::span<const char>(First, sizeof(First) - 1U))).has_value());
    ASSERT_TRUE(writeFile(toUtf8(path), std::as_bytes(std::span<const char>(Second, sizeof(Second) - 1U))).has_value());

    auto read = readFile(toUtf8(path), ReadFileConfig{.maxBytes = 64, .memoryResource = &resource});
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->size(), sizeof(Second) - 1U);
    EXPECT_EQ((*read)[0], std::byte{'s'});

    std::filesystem::remove_all(path.parent_path(), ec);
}

TEST(WriteFileTests, FailedAtomicReplacePreservesExistingTargetDirectory)
{
    const auto path = std::filesystem::temp_directory_path() / "tina_writefile_tests" / "target-directory";
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);
    ASSERT_TRUE(std::filesystem::create_directories(path, ec));
    ASSERT_FALSE(ec);

    constexpr char Content[] = "must-not-replace-a-directory";
    const auto bytes = std::as_bytes(std::span<const char>(Content, sizeof(Content) - 1U));
    const auto status = writeFile(toUtf8(path), bytes);
    EXPECT_FALSE(status.has_value());
    EXPECT_TRUE(std::filesystem::is_directory(path));

    std::filesystem::remove_all(path.parent_path(), ec);
}

TEST(WriteFileTests, AtomicReplacementUsesExactUtf8TargetAcrossPathLengths)
{
    const auto root = std::filesystem::temp_directory_path() / "tina_writefile_path_lengths";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const std::array<usize, 8> lengths{1, 7, 15, 16, 31, 32, 63, 64};
    const std::array first{std::byte{1}};
    const std::array second{std::byte{2}, std::byte{3}};
    for (const auto length : lengths) {
        const auto path = root / std::filesystem::path{u8"\u76ee\u5f55"} / (std::string(length, 'x') + ".bin");
        // Public UTF-8 paths also accept forward slashes on Windows.
        const auto utf8 = path.generic_u8string();
        const std::string name(utf8.begin(), utf8.end());
        SCOPED_TRACE(name);
        ASSERT_TRUE(writeFile(name, first));
        ASSERT_TRUE(writeFile(name, second));
        auto read = readFile(name, {.memoryResource = std::pmr::new_delete_resource()});
        ASSERT_TRUE(read) << read.error().message;
        EXPECT_TRUE(std::ranges::equal(*read, second));
        usize files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
            EXPECT_EQ(entry.path().filename(), path.filename());
            ++files;
        }
        EXPECT_EQ(files, 1U);
        ASSERT_TRUE(std::filesystem::remove(path, error));
        ASSERT_FALSE(error);
    }
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error);
}

} // namespace
} // namespace Tina::Core
