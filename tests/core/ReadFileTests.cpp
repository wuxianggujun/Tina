#include <tina/core/io/ReadFile.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <string>
#include <vector>

namespace Tina::Core {
namespace {

class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    [[nodiscard]] std::size_t outstandingAllocations() const noexcept
    {
        return m_outstandingAllocations;
    }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        void* pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++m_outstandingAllocations;
        return pointer;
    }

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        --m_outstandingAllocations;
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    std::size_t m_outstandingAllocations = 0;
};

class TempFile final {
  public:
    explicit TempFile(std::string_view contents)
    {
        const auto directory = std::filesystem::temp_directory_path() / "tina_readfile_tests";
        std::filesystem::create_directories(directory);
        m_path = directory / ("file_" + std::to_string(++s_counter) + ".bin");
        std::ofstream output(m_path, std::ios::binary);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        const auto u8 = m_path.u8string();
        m_utf8.assign(u8.begin(), u8.end());
    }

    ~TempFile()
    {
        std::error_code errorCode;
        std::filesystem::remove(m_path, errorCode);
    }

    [[nodiscard]] std::string_view utf8Path() const noexcept
    {
        return m_utf8;
    }

  private:
    static inline int s_counter = 0;
    std::filesystem::path m_path;
    std::string m_utf8;
};

TEST(ReadFileTests, RejectsInvalidConfigAndEmptyPath)
{
    TrackingMemoryResource resource;
    EXPECT_FALSE(readFile("x", ReadFileConfig{.maxBytes = 0, .memoryResource = &resource}));
    EXPECT_FALSE(readFile("x", ReadFileConfig{.maxBytes = 8, .memoryResource = nullptr}));
    EXPECT_FALSE(readFile("", ReadFileConfig{.maxBytes = 8, .memoryResource = &resource}));
}

TEST(ReadFileTests, RejectsMissingFile)
{
    TrackingMemoryResource resource;
    const auto result = readFile("C:/tina_missing_catalog_file_does_not_exist.bin",
                                 ReadFileConfig{.maxBytes = 64, .memoryResource = &resource});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CoreErrorCode::NotFound);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(ReadFileTests, ReadsRegularFileContents)
{
    TrackingMemoryResource resource;
    constexpr std::string_view Contents = "tina-readfile";
    TempFile file(Contents);
    {
        auto result = readFile(file.utf8Path(), ReadFileConfig{.maxBytes = 64, .memoryResource = &resource});
        ASSERT_TRUE(result.has_value()) << result.error().message;
        ASSERT_EQ(result->size(), Contents.size());
        EXPECT_EQ((*result)[0], std::byte{'t'});
        EXPECT_EQ((*result)[Contents.size() - 1U], std::byte{'e'});
    }
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(ReadFileTests, RejectsFileLargerThanMaxBytes)
{
    TrackingMemoryResource resource;
    TempFile file("0123456789");
    const auto result = readFile(file.utf8Path(), ReadFileConfig{.maxBytes = 4, .memoryResource = &resource});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, CoreErrorCode::CapacityExceeded);
    EXPECT_EQ(resource.outstandingAllocations(), 0U);
}

TEST(ReadFileTests, ReadsEmptyFile)
{
    TrackingMemoryResource resource;
    TempFile file("");
    auto result = readFile(file.utf8Path(), ReadFileConfig{.maxBytes = 8, .memoryResource = &resource});
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->empty());
}

} // namespace
} // namespace Tina::Core
