#include <tina/core/io/PackageFile.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>

namespace Tina::Core {
namespace {

using Bytes = std::vector<std::byte>;

std::string utf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

void put(Bytes& bytes, usize offset, u64 value, usize length = 8)
{
    for (usize i = 0; i < length; ++i) bytes.at(offset + i) = std::byte((value >> (8 * i)) & 255U);
}

u64 get(const Bytes& bytes, usize offset)
{
    u64 result = 0;
    for (usize i = 0; i < 8; ++i) result |= u64(std::to_integer<u8>(bytes.at(offset + i))) << (8 * i);
    return result;
}

void resealIndex(Bytes& bytes)
{
    auto digest = digestContentHashV1(std::span<const std::byte>(bytes).subspan(
        PackageWire::HeaderBytes, static_cast<usize>(get(bytes, 24))));
    ASSERT_TRUE(digest);
    std::memcpy(bytes.data() + 48, digest->bytes().data(), 16);
}

class PackageFileTests : public testing::Test {
  protected:
    void SetUp() override
    {
        static std::atomic<u64> sequence{};
        root = std::filesystem::temp_directory_path() /
            ("tina_package_io_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "_" + std::to_string(sequence++));
        path = utf8(root / std::filesystem::path{u8"资源.pck"});
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        EXPECT_FALSE(error) << error.message();
    }
    Bytes archive(std::span<const PackageWriteEntry> entries)
    {
        auto status = writePackageFile(path, entries);
        EXPECT_TRUE(status) << (status ? "" : status.error().message);
        if (!status) return {};
        auto result = readFile(path, {.memoryResource = std::pmr::new_delete_resource()});
        EXPECT_TRUE(result) << (result ? "" : result.error().message);
        return result ? Bytes(result->begin(), result->end()) : Bytes{};
    }
    std::filesystem::path root;
    std::string path;
    const std::array<std::byte, 3> first{std::byte{1}, std::byte{2}, std::byte{3}};
    const std::array<std::byte, 5> second{std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6}, std::byte{5}};
};

TEST_F(PackageFileTests, DeterministicSortedRoundTripAndEmptyEntry)
{
    const std::array entries{PackageWriteEntry{"z/file", second}, PackageWriteEntry{"a/file", first},
                             PackageWriteEntry{"empty", {}}};
    auto original = archive(entries);
    const std::array reordered{entries[2], entries[0], entries[1]};
    EXPECT_EQ(original, archive(reordered));
    auto reader = PackageReader::Open(path);
    ASSERT_TRUE(reader) << reader.error().message;
    EXPECT_EQ(reader->fileCount(), 3U);
    ASSERT_TRUE(reader->entry(0));
    EXPECT_EQ(reader->entry(0)->path, "a/file");
    EXPECT_FALSE(reader->entry(3));
    EXPECT_EQ(reader->getFileSize("z/file"), second.size());
    EXPECT_FALSE(reader->hasFile("Z/file"));
    EXPECT_FALSE(reader->getFileSize("absent"));
    auto empty = reader->viewFile("empty");
    ASSERT_TRUE(empty);
    EXPECT_TRUE(static_cast<bool>(*empty));
    EXPECT_TRUE(empty->bytes().empty());
    auto copied = reader->readFile("a/file", std::pmr::new_delete_resource());
    ASSERT_TRUE(copied);
    EXPECT_TRUE(std::ranges::equal(*copied, first));
}

TEST_F(PackageFileTests, EmptyPackageAndInvalidReaderAreSafe)
{
    ASSERT_TRUE(writePackageFile(path, {}));
    auto reader = PackageReader::Open(path);
    ASSERT_TRUE(reader);
    EXPECT_EQ(reader->fileCount(), 0U);
    auto missing = reader->viewFile("missing");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, CoreErrorCode::NotFound);
    PackageReader invalid;
    EXPECT_FALSE(invalid);
    EXPECT_FALSE(invalid.entry(0));
    EXPECT_FALSE(invalid.hasFile("missing"));
}

TEST_F(PackageFileTests, SortedLookupHandlesEdgesSharedPrefixesAndMissingNeighbors)
{
    std::vector<std::string> paths{"a", "a/item", "b", "中文/资源"};
    const auto prefix = "objects/" + std::string(180, 'x') + "/";
    for (usize i = 512; i != 0; --i) paths.push_back(prefix + std::to_string(i));
    std::vector<PackageWriteEntry> entries;
    for (usize i = 0; i < paths.size(); ++i) {
        entries.push_back({paths[i], i % 2 == 0 ? std::span<const std::byte>{first}
                                               : std::span<const std::byte>{second}});
    }
    ASSERT_TRUE(writePackageFile(path, entries));
    auto reader = PackageReader::Open(path);
    ASSERT_TRUE(reader);
    ASSERT_EQ(reader->fileCount(), paths.size());
    EXPECT_EQ(reader->entry(0)->path, *std::ranges::min_element(paths));
    EXPECT_EQ(reader->entry(paths.size() - 1)->path, *std::ranges::max_element(paths));
    for (usize i = 0; i < paths.size(); ++i) {
        EXPECT_TRUE(reader->hasFile(paths[i])) << paths[i];
        EXPECT_EQ(reader->getFileSize(paths[i]), entries[i].bytes.size());
        EXPECT_FALSE(reader->hasFile(paths[i] + "/missing"));
    }
    for (const auto missing : {"", "A", "a/", "aa", "a/ite", "z"})
        EXPECT_FALSE(reader->hasFile(missing)) << missing;
}

TEST_F(PackageFileTests, SupportsLongUtf8VirtualPathsAndUnicodeDiskPath)
{
    const std::string virtualPath = "textures/" + std::string(700, 'x') + "/中文资源";
    const std::array entries{PackageWriteEntry{virtualPath, first}};
    ASSERT_TRUE(writePackageFile(path, entries));
    auto reader = PackageReader::Open(path);
    ASSERT_TRUE(reader) << reader.error().message;
    auto view = reader->viewFile(virtualPath);
    ASSERT_TRUE(view);
    EXPECT_TRUE(std::ranges::equal(view->bytes(), first));
    EXPECT_FALSE(reader->hasFile(virtualPath.substr(0, 255)));
}

TEST_F(PackageFileTests, PinsSurviveMoveDestructionAndAtomicReplacement)
{
    PackageFileView oldView;
    {
        const std::array entries{PackageWriteEntry{"item", first}};
        ASSERT_TRUE(writePackageFile(path, entries));
        auto reader = PackageReader::Open(path);
        ASSERT_TRUE(reader);
        auto view = reader->viewFile("item");
        ASSERT_TRUE(view);
        oldView = std::move(*view);
        EXPECT_TRUE(view->bytes().empty());
        auto copiedReader = *reader;
        PackageReader moved = std::move(copiedReader);
        EXPECT_FALSE(copiedReader);
        EXPECT_EQ(moved.fileCount(), 1U);
        const std::array replacement{PackageWriteEntry{"item", second}};
        ASSERT_TRUE(writePackageFile(path, replacement));
        auto newReader = PackageReader::Open(path);
        ASSERT_TRUE(newReader);
        auto newView = newReader->viewFile("item");
        ASSERT_TRUE(newView);
        EXPECT_TRUE(std::ranges::equal(newView->bytes(), second));
        EXPECT_TRUE(std::ranges::equal(oldView.bytes(), first));
        EXPECT_EQ(moved.getFileSize("item"), first.size());
    }
    EXPECT_TRUE(std::ranges::equal(oldView.bytes(), first));
    oldView = {};
}

TEST_F(PackageFileTests, MemoryPackageOwnsStorageAndSupportsConcurrentReaders)
{
    const std::array entries{PackageWriteEntry{"item", first}};
    auto bytes = archive(entries);
    auto reader = PackageReader::FromMemory(std::move(bytes));
    ASSERT_TRUE(reader);
    std::atomic<bool> success{true};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back([copy = *reader, &success, this] {
            for (int j = 0; j < 512; ++j) {
                auto view = copy.viewFile("item");
                if (!view || !std::ranges::equal(view->bytes(), first)) success = false;
            }
        });
    for (auto& thread : threads) thread.join();
    EXPECT_TRUE(success);
}

TEST_F(PackageFileTests, ReadsBeyondFormer64MiBMappingWindow)
{
    Bytes payload(64U * 1024U * 1024U + 8193U, std::byte{0x35});
    payload.back() = std::byte{0x7f};
    const std::array entries{PackageWriteEntry{"large", payload}, PackageWriteEntry{"tail", second}};
    ASSERT_TRUE(writePackageFile(path, entries));
    auto reader = PackageReader::Open(path);
    ASSERT_TRUE(reader) << reader.error().message;
    auto view = reader->viewFile("large");
    ASSERT_TRUE(view);
    ASSERT_EQ(view->bytes().size(), payload.size());
    EXPECT_TRUE(std::ranges::equal(view->bytes(), payload));
    auto tail = reader->viewFile("tail");
    ASSERT_TRUE(tail);
    EXPECT_TRUE(std::ranges::equal(tail->bytes(), second));
}

TEST_F(PackageFileTests, RejectsUnsafeAndDuplicatePathsWithoutReplacingPublishedFile)
{
    const std::array initial{PackageWriteEntry{"item", first}};
    ASSERT_TRUE(writePackageFile(path, initial));
    const std::array<std::string, 10> invalid{"", "/absolute", "../parent", "a/../b", "a//b",
        "a/./b", "a\\b", "c:drive", std::string{"a\0b", 3}, std::string{"\xc3(", 2}};
    for (const auto& name : invalid) {
        const std::array entries{PackageWriteEntry{name, second}};
        EXPECT_FALSE(writePackageFile(path, entries)) << name;
    }
    const std::array duplicate{PackageWriteEntry{"item", first}, PackageWriteEntry{"item", second}};
    auto status = writePackageFile(path, duplicate);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, CoreErrorCode::AlreadyExists);
    auto reader = PackageReader::Open(path);
    ASSERT_TRUE(reader);
    EXPECT_EQ(reader->getFileSize("item"), first.size());
}

TEST_F(PackageFileTests, ValidatesMetadataAndPayloadDigestsSeparately)
{
    const std::array entries{PackageWriteEntry{"item", first}};
    auto bytes = archive(entries);
    auto invalidIndex = bytes;
    invalidIndex[PackageWire::HeaderBytes + PackageWire::EntryBytes] ^= std::byte{1};
    EXPECT_FALSE(PackageReader::FromMemory(std::move(invalidIndex)));
    bytes.back() ^= std::byte{1};
    auto reader = PackageReader::FromMemory(std::move(bytes));
    ASSERT_TRUE(reader); // Opening touches only metadata, not every payload page.
    EXPECT_EQ(reader->getFileSize("item"), first.size());
    auto view = reader->viewFile("item");
    ASSERT_FALSE(view);
    EXPECT_EQ(view.error().code, CoreErrorCode::InvalidArgument);
}

TEST_F(PackageFileTests, RejectsTruncationOldSchemaAndOverflow)
{
    const std::array entries{PackageWriteEntry{"a", first}, PackageWriteEntry{"b", second}};
    const auto bytes = archive(entries);
    ASSERT_FALSE(bytes.empty());
    for (const usize length : {usize{0}, usize{4}, PackageWire::HeaderBytes - 1, bytes.size() - 1}) {
        EXPECT_FALSE(PackageReader::FromMemory(Bytes(bytes.begin(), bytes.begin() + static_cast<isize>(length))));
    }
    for (const usize offset : {usize{16}, usize{24}, usize{32}, usize{40}}) {
        auto invalid = bytes;
        put(invalid, offset, (std::numeric_limits<u64>::max)());
        EXPECT_FALSE(PackageReader::FromMemory(std::move(invalid)));
    }
    auto old = bytes;
    put(old, 4, 1, 4);
    auto rejected = PackageReader::FromMemory(std::move(old));
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, CoreErrorCode::Unsupported);
}

TEST_F(PackageFileTests, RejectsResealedMalformedIndexRangesOverlapAndOrdering)
{
    const std::array entries{PackageWriteEntry{"a", first}, PackageWriteEntry{"b", second}};
    const auto bytes = archive(entries);
    for (const usize field : {usize{0}, usize{8}, usize{16}, usize{24}}) {
        auto invalid = bytes;
        put(invalid, PackageWire::HeaderBytes + field, (std::numeric_limits<u64>::max)());
        resealIndex(invalid);
        EXPECT_FALSE(PackageReader::FromMemory(std::move(invalid))) << field;
    }
    auto overlap = bytes;
    put(overlap, PackageWire::HeaderBytes + PackageWire::EntryBytes + 16,
        get(bytes, PackageWire::HeaderBytes + 16));
    resealIndex(overlap);
    EXPECT_FALSE(PackageReader::FromMemory(std::move(overlap)));
    for (char replacement : {'a', 'A', '/'}) {
        auto invalid = bytes;
        invalid[PackageWire::HeaderBytes + 2 * PackageWire::EntryBytes + 1] = std::byte(replacement);
        resealIndex(invalid);
        EXPECT_FALSE(PackageReader::FromMemory(std::move(invalid)));
    }
}

TEST_F(PackageFileTests, ByteBudgetsAndInvalidAllocatorsFailWithoutLosingPins)
{
    const std::array entries{PackageWriteEntry{"item", first}};
    const auto bytes = archive(entries);
    auto rejected = PackageReader::FromMemory(bytes, {.maxMetadataBytes = 1});
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, CoreErrorCode::CapacityExceeded);
    auto reader = PackageReader::FromMemory(bytes, {.maxMetadataBytes = 0});
    ASSERT_TRUE(reader);
    EXPECT_FALSE(reader->viewFile("item", 2));
    EXPECT_FALSE(reader->readFile("item", nullptr));
    auto oom = reader->readFile("item", std::pmr::null_memory_resource());
    ASSERT_FALSE(oom);
    EXPECT_EQ(oom.error().code, CoreErrorCode::OutOfMemory);
    EXPECT_TRUE(reader->viewFile("item"));
    EXPECT_FALSE(PackageReader::Open(std::string{"a\0b", 3}));
    auto missing = PackageReader::Open(path + ".absent");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, CoreErrorCode::NotFound);
}

} // namespace
} // namespace Tina::Core
