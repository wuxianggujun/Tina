#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <optional>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

constexpr usize DeepNodeCount = 50'000;

struct DeepTreeNodes final {
    UI::UINodeId firstChild;
    UI::UINodeId leaf;
};

class UIDeepTreeStressTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));

        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;
    }

    [[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
        UI::UIContextCapacityConfig capacities)
    {
        capacities.applyDefaultProductChrome = false;
        auto contextResult = UI::UIContext::Create(window, capacities);
        EXPECT_TRUE(contextResult.has_value())
            << (contextResult ? "" : contextResult.error().message);
        return contextResult ? std::move(*contextResult) : nullptr;
    }

    [[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
    {
        auto rootResult = context.rootBuilder().createRoot();
        EXPECT_TRUE(rootResult.has_value())
            << (rootResult ? "" : rootResult.error().message);
        return rootResult ? std::move(*rootResult) : UI::UIRootOwner{};
    }

    [[nodiscard]] std::optional<DeepTreeNodes> buildDeepTree(
        UI::UIContext& context,
        UI::UINodeId rootNode)
    {
        UI::UIRootBuilder builder = context.rootBuilder();
        UI::UINodeId parent = rootNode;
        UI::UINodeId firstChild;
        for (usize nodeIndex = 1; nodeIndex < DeepNodeCount; ++nodeIndex)
        {
            auto childResult = builder.createElement(parent, UI::makePanelElement());
            if (!childResult)
            {
                ADD_FAILURE() << "nodeIndex=" << nodeIndex
                              << ": " << childResult.error().message;
                return std::nullopt;
            }
            parent = *childResult;
            if (nodeIndex == 1)
            {
                firstChild = parent;
            }
        }
        return DeepTreeNodes{.firstChild = firstChild, .leaf = parent};
    }

    void assertOk(Core::Status status)
    {
        ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
};

TEST_F(UIDeepTreeStressTest, StructureCommitAndDestroyAreNonRecursive)
{
    auto context = createContext({
        .nodeCapacity = DeepNodeCount,
        .rootCapacity = 1,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const auto nodes = buildDeepTree(*context, root.rootNodeId());
    ASSERT_TRUE(nodes.has_value());

    assertOk(context->commitStructure());
    const UI::UICommittedStructureView committed = context->committedStructure();
    ASSERT_EQ(committed.size(), DeepNodeCount);
    EXPECT_EQ(committed.entries().back().node, nodes->leaf);
    EXPECT_EQ(committed.entries().back().depth, DeepNodeCount - 1);

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    assertOk(updaterResult->destroy(nodes->firstChild));
    EXPECT_EQ(context->liveNodeCount(), 1U);
    EXPECT_FALSE(context->contains(nodes->leaf));

    assertOk(context->commitStructure());
    EXPECT_EQ(context->committedStructure().size(), 1U);
    root.reset();
    EXPECT_EQ(context->liveNodeCount(), 0U);
}

TEST_F(UIDeepTreeStressTest, LayoutSnapshotBuildIsNonRecursive)
{
    auto context = createContext({
        .nodeCapacity = DeepNodeCount,
        .rootCapacity = 1,
        .dirtyQueueCapacity = DeepNodeCount,
        .layoutSnapshotCapacity = DeepNodeCount,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const auto nodes = buildDeepTree(*context, root.rootNodeId());
    ASSERT_TRUE(nodes.has_value());

    assertOk(context->commitStructure());
    assertOk(context->commitLayout({.width = 1.0F, .height = 1.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();
    ASSERT_EQ(layout.size(), DeepNodeCount);
    EXPECT_EQ(layout.entries().back().node, nodes->leaf);
    EXPECT_EQ(layout.entries().back().layoutOrdinal, DeepNodeCount - 1);
    EXPECT_EQ(context->statistics().lastLayoutPassCount, 1U);
    EXPECT_EQ(context->statistics().lastLayoutArrangedNodeCount, DeepNodeCount);
}

TEST_F(UIDeepTreeStressTest, HitSnapshotBuildIsNonRecursive)
{
    auto context = createContext({
        .nodeCapacity = DeepNodeCount,
        .rootCapacity = 1,
        .dirtyQueueCapacity = DeepNodeCount,
        .layoutSnapshotCapacity = DeepNodeCount,
        .hitSnapshotCapacity = DeepNodeCount,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const auto nodes = buildDeepTree(*context, root.rootNodeId());
    ASSERT_TRUE(nodes.has_value());

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    assertOk(updaterResult->setPointerHitPolicy(
        nodes->leaf,
        UI::UIPointerHitPolicy::Targetable));
    assertOk(context->commitLayout({.width = 1.0F, .height = 1.0F}));

    const UI::UICommittedHitView hit = context->committedHit();
    ASSERT_EQ(hit.size(), DeepNodeCount);
    EXPECT_EQ(hit.entries().back().node, nodes->leaf);
    EXPECT_EQ(hit.entries().back().parentEntryIndex, DeepNodeCount - 2U);
    EXPECT_EQ(hit.entries().back().rootEntryIndex, 0U);
    EXPECT_EQ(context->statistics().committedHitTargetCount, 1U);
}

TEST_F(UIDeepTreeStressTest, PaintSnapshotBuildIsNonRecursive)
{
    auto context = createContext({
        .nodeCapacity = DeepNodeCount,
        .rootCapacity = 1,
        .dirtyQueueCapacity = DeepNodeCount,
        .layoutSnapshotCapacity = DeepNodeCount,
        .hitSnapshotCapacity = DeepNodeCount,
        .paintSnapshotCapacity = DeepNodeCount,
    });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const auto nodes = buildDeepTree(*context, root.rootNodeId());
    ASSERT_TRUE(nodes.has_value());

    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value());
    assertOk(updaterResult->setBoxPaint(
        nodes->leaf,
        UI::UIBoxPaint{
            .solidFill = UI::UISolidFill{.color = UI::rgba8(1, 2, 3, 4)},
        }));
    assertOk(context->commitLayout({.width = 1.0F, .height = 1.0F}));

    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 1U);
    EXPECT_EQ(paint.entries().front().node, nodes->leaf);
    EXPECT_EQ(context->statistics().lastPaintCacheRebuildCount, 1U);
}

} // namespace
} // namespace Tina::Tests
