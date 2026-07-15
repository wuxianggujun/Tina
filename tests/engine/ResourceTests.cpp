#include <gtest/gtest.h>

#include "engine/Resource.hpp"

#include <deque>
#include <utility>

namespace Tina::Engine {
namespace {

class FakeFileSystem final : public FileSystem {
public:
    AsyncHandle getContent(const Path&, ContentCallback callback) override
    {
        const AsyncHandle handle{++m_nextId};
        Content content;
        content.push_back(static_cast<Tina::u8>(handle.id));
        m_completions.push_back(Completion{handle, std::move(content), std::move(callback)});
        return handle;
    }

    void cancel(AsyncHandle) override
    {
        // 模拟无法中止已经完成的后台 I/O；Resource generation 仍必须丢弃旧 completion。
        ++cancelCount;
    }

    size_t processCallbacks(size_t maxCallbacks) override
    {
        ++pumpCount;
        size_t processed = 0;
        while (!m_completions.empty() && (maxCallbacks == 0 || processed < maxCallbacks)) {
            Completion completion = std::move(m_completions.front());
            m_completions.pop_front();
            completion.callback(completion.content, true);
            ++processed;
        }
        return processed;
    }

    size_t pumpCount = 0;
    size_t cancelCount = 0;

private:
    struct Completion {
        AsyncHandle handle;
        Content content;
        ContentCallback callback;
    };

    Tina::u64 m_nextId = 0;
    std::deque<Completion> m_completions;
};

TEST(ResourceTest, HubPumpsSharedFileSystemOnceAndHonorsBudget)
{
    FakeFileSystem fileSystem;
    BlobManager firstManager(fileSystem);
    BlobManager secondManager(fileSystem);
    ResourceManagerHub hub(fileSystem, 1);
    hub.add(BlobResource::TYPE, &firstManager);
    hub.add(ResourceType{"secondary-test-manager"}, &secondManager);

    auto* first = static_cast<BlobResource*>(firstManager.load(Path{"asset-a"}));
    auto* second = static_cast<BlobResource*>(firstManager.load(Path{"asset-b"}));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->getState(), Resource::State::LOADING);
    EXPECT_EQ(second->getState(), Resource::State::LOADING);

    EXPECT_EQ(hub.update(), 1u);
    EXPECT_EQ(fileSystem.pumpCount, 1u);
    EXPECT_EQ(first->getState(), Resource::State::READY);
    EXPECT_EQ(second->getState(), Resource::State::LOADING);

    EXPECT_EQ(hub.update(), 1u);
    EXPECT_EQ(fileSystem.pumpCount, 2u);
    EXPECT_EQ(second->getState(), Resource::State::READY);

    firstManager.unload(*first);
    firstManager.unload(*second);
}

TEST(ResourceTest, CancelledGenerationCannotOverwriteReloadedResource)
{
    FakeFileSystem fileSystem;
    BlobManager manager(fileSystem);
    ResourceManagerHub hub(fileSystem, 1);
    hub.add(BlobResource::TYPE, &manager);

    auto* resource = static_cast<BlobResource*>(manager.load(Path{"asset"}));
    ASSERT_NE(resource, nullptr);
    const Tina::u64 firstGeneration = resource->generation();

    manager.unload(*resource);
    EXPECT_EQ(resource->getState(), Resource::State::UNLOADED);
    EXPECT_EQ(fileSystem.cancelCount, 1u);

    auto* reloaded = static_cast<BlobResource*>(manager.load(Path{"asset"}));
    ASSERT_EQ(reloaded, resource);
    EXPECT_GT(resource->generation(), firstGeneration);
    EXPECT_EQ(resource->getState(), Resource::State::LOADING);

    // 第一项是已取消 generation 的迟到 completion，不能改变当前状态。
    EXPECT_EQ(hub.update(), 1u);
    EXPECT_EQ(resource->getState(), Resource::State::LOADING);

    EXPECT_EQ(hub.update(), 1u);
    EXPECT_EQ(resource->getState(), Resource::State::READY);
    ASSERT_EQ(resource->data.size(), 1u);
    EXPECT_EQ(resource->data[0], 2u);

    manager.unload(*resource);
}

} // namespace
} // namespace Tina::Engine
