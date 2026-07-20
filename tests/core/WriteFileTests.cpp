#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

namespace Tina::Core {
namespace {

class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] std::size_t outstandingAllocations() const noexcept
    {
        return m_outstanding;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++m_outstanding;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
    {
        --m_outstanding;
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
    std::size_t m_outstanding = 0;
};

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

TEST(WriteFileTests, AtomicWriteThenReadRoundTrip)
{
    TrackingMemoryResource resource;
    const auto path = std::filesystem::temp_directory_path() / "tina_writefile_tests" / "nested" / "blob.bin";
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

} // namespace
} // namespace Tina::Core
