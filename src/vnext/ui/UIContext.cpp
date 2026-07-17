#include <tina/ui/UIContext.hpp>

#include <tina/core/id/GenerationPool.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <expected>
#include <limits>
#include <mutex>
#include <new>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::UI::Detail {

struct UIContextLifetimeControl final {
    UIContextLifetimeControl(
        std::thread::id threadId,
        usize rootCapacity)
        : ownerThreadId(threadId)
    {
        deferredRootDestroys.reserve(rootCapacity);
    }

    std::mutex mutex;
    UIContext* context = nullptr;
    std::thread::id ownerThreadId{};
    std::vector<UINodeId> deferredRootDestroys;
};

} // namespace Tina::UI::Detail

namespace Tina::UI {
namespace {

using NodeStorageId = Core::GenerationId<Detail::UINodeRegistryTag>;
inline constexpr u32 InvalidNodeIndex = NodeStorageId::InvalidIndex;

struct NormalizedCapacityConfig final {
    usize nodeCapacity = 0;
    usize rootCapacity = 0;
};

struct NodeRecord final {
    u32 parentIndex = InvalidNodeIndex;
    u32 firstChildIndex = InvalidNodeIndex;
    u32 lastChildIndex = InvalidNodeIndex;
    u32 previousSiblingIndex = InvalidNodeIndex;
    u32 nextSiblingIndex = InvalidNodeIndex;
    u32 rootIndex = InvalidNodeIndex;
    u32 depth = 0;
    UIWidgetKind kind = UIWidgetKind::Panel;
};

static_assert(sizeof(NodeRecord) <= 48);
static_assert(std::is_nothrow_destructible_v<NodeRecord>);

using NodePool = Core::GenerationPool<NodeRecord, Detail::UINodeRegistryTag>;

[[nodiscard]] Core::Error makeError(
    Core::ErrorCode code,
    std::string_view message,
    Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::Error{code, message, location};
}

[[nodiscard]] std::unexpected<Core::Error> fail(
    Core::ErrorCode code,
    std::string_view message,
    Core::SourceLocation location = Core::SourceLocation::current())
{
    return Core::failure(makeError(code, message, location));
}

[[nodiscard]] Core::Result<NormalizedCapacityConfig> normalizeCapacity(
    UIContextCapacityConfig config)
{
    if (config.nodeCapacity == 0 || config.rootCapacity == 0) {
        return fail(
            UIErrorCode::InvalidContextConfig,
            "UI context capacities must be greater than zero");
    }
    if (config.nodeCapacity > UIContextCapacityConfig::MaxNodeCapacity
        || config.rootCapacity > UIContextCapacityConfig::MaxRootCapacity) {
        return fail(
            UIErrorCode::InvalidContextConfig,
            "UI context capacity exceeds the configured maximum");
    }
    if (config.rootCapacity > config.nodeCapacity) {
        return fail(
            UIErrorCode::InvalidContextConfig,
            "UI root capacity cannot exceed node capacity");
    }
    if (config.nodeCapacity > static_cast<usize>(InvalidNodeIndex)) {
        return fail(
            UIErrorCode::InvalidContextConfig,
            "UI node capacity exceeds the node index range");
    }

    return NormalizedCapacityConfig{
        .nodeCapacity = config.nodeCapacity,
        .rootCapacity = config.rootCapacity,
    };
}

[[nodiscard]] bool sameNode(UINodeId left, UINodeId right) noexcept
{
    return left == right;
}

} // namespace

struct UIContext::Impl final {
    Platform::WindowId ownerWindow{};
    UIContextCapacityConfig capacityConfig{};
    std::thread::id ownerThreadId{};
    std::shared_ptr<Detail::UIContextLifetimeControl> lifetime;
    NodePool nodes;
    std::pmr::vector<UINodeId> idsByIndex;
    std::array<std::pmr::vector<UICommittedNodeEntry>, 2> committedBuffers;
    std::vector<UINodeId> deferredRootDestroyBuffer;
    usize publishedBufferIndex = 0;
    u64 committedRevision = 0;
    usize liveRootCount = 0;
    u32 firstRootIndex = InvalidNodeIndex;
    u32 lastRootIndex = InvalidNodeIndex;
    bool dirty = false;

    Impl(
        Platform::WindowId owner,
        UIContextCapacityConfig capacities,
        std::thread::id threadId,
        std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl,
        NodePool&& nodePool,
        std::pmr::memory_resource& resource)
        : ownerWindow(owner),
          capacityConfig(capacities),
          ownerThreadId(threadId),
          lifetime(std::move(lifetimeControl)),
          nodes(std::move(nodePool)),
          idsByIndex(&resource),
          committedBuffers{
              std::pmr::vector<UICommittedNodeEntry>(&resource),
              std::pmr::vector<UICommittedNodeEntry>(&resource)}
    {
    }

    [[nodiscard]] static Core::Result<std::unique_ptr<Impl>> Create(
        Platform::WindowId ownerWindow,
        NormalizedCapacityConfig normalized,
        std::shared_ptr<Detail::UIContextLifetimeControl> lifetimeControl,
        std::pmr::memory_resource& resource)
    {
        auto poolResult = NodePool::Create(normalized.nodeCapacity, resource);
        if (!poolResult) {
            const Core::Error& error = poolResult.error();
            if (error.code == Core::CoreErrorCode::CapacityExceeded) {
                return fail(
                    UIErrorCode::CapacityExceeded,
                    "UI node pool capacity could not be reserved");
            }
            return Core::failure(error);
        }

        UIContextCapacityConfig capacities{
            .nodeCapacity = normalized.nodeCapacity,
            .rootCapacity = normalized.rootCapacity,
        };

        auto impl = std::unique_ptr<Impl>(new Impl(
            ownerWindow,
            capacities,
            std::this_thread::get_id(),
            std::move(lifetimeControl),
            std::move(*poolResult),
            resource));
        impl->idsByIndex.resize(normalized.nodeCapacity);
        impl->committedBuffers[0].reserve(normalized.nodeCapacity);
        impl->committedBuffers[1].reserve(normalized.nodeCapacity);
        impl->deferredRootDestroyBuffer.reserve(normalized.rootCapacity);
        return impl;
    }

    void detachLifetime(UIContext* context) noexcept
    {
        if (!lifetime) {
            return;
        }
        const std::scoped_lock lock(lifetime->mutex);
        if (lifetime->context == context) {
            lifetime->context = nullptr;
            lifetime->deferredRootDestroys.clear();
        }
    }

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == ownerThreadId;
    }

    [[nodiscard]] Core::Status ensureOwnerThread() const
    {
        if (!isOwnerThread()) {
            return fail(
                UIErrorCode::WrongOwnerThread,
                "UI context was accessed from a non-owner thread");
        }
        return Core::success();
    }

    void drainDeferredRootDestroys() noexcept
    {
        if (!isOwnerThread() || !lifetime) {
            return;
        }

        deferredRootDestroyBuffer.clear();
        {
            const std::scoped_lock lock(lifetime->mutex);
            deferredRootDestroyBuffer.swap(lifetime->deferredRootDestroys);
        }

        for (const UINodeId root : deferredRootDestroyBuffer) {
            destroyRootImmediately(root);
        }
        deferredRootDestroyBuffer.clear();
    }

    [[nodiscard]] UINodeId idForIndex(u32 index) const noexcept
    {
        if (index == InvalidNodeIndex || index >= idsByIndex.size()) {
            return {};
        }
        return idsByIndex[index];
    }

    [[nodiscard]] NodeRecord* recordByIndex(u32 index) noexcept
    {
        return nodes.tryGet(idForIndex(index).storageId());
    }

    [[nodiscard]] const NodeRecord* recordByIndex(u32 index) const noexcept
    {
        return nodes.tryGet(idForIndex(index).storageId());
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveNode(UINodeId node)
    {
        if (!node.hasValue()) {
            return fail(UIErrorCode::InvalidNode, "UI node id is empty");
        }
        if (node.ownerWindow() != ownerWindow) {
            return fail(
                UIErrorCode::WrongOwnerWindow,
                "UI node belongs to another owner window");
        }
        if (node.storageId().owner() != nodes.owner()) {
            return fail(UIErrorCode::WrongContext, "UI node belongs to another context");
        }
        NodeRecord* record = nodes.tryGet(node.storageId());
        if (record == nullptr) {
            return fail(UIErrorCode::InvalidNode, "UI node is stale or out of range");
        }
        return record;
    }

    [[nodiscard]] Core::Result<NodeRecord*> resolveParent(UINodeId parent)
    {
        if (!parent.hasValue()) {
            return fail(UIErrorCode::InvalidParent, "UI parent id is empty");
        }
        if (parent.ownerWindow() != ownerWindow) {
            return fail(
                UIErrorCode::WrongOwnerWindow,
                "UI parent belongs to another owner window");
        }
        if (parent.storageId().owner() != nodes.owner()) {
            return fail(UIErrorCode::WrongContext, "UI parent belongs to another context");
        }
        NodeRecord* record = nodes.tryGet(parent.storageId());
        if (record == nullptr) {
            return fail(UIErrorCode::InvalidParent, "UI parent is stale or out of range");
        }
        return record;
    }

    [[nodiscard]] bool contains(UINodeId node) const noexcept
    {
        return node.hasValue()
            && node.ownerWindow() == ownerWindow
            && node.storageId().owner() == nodes.owner()
            && nodes.contains(node.storageId());
    }

    [[nodiscard]] bool isNodeWithinRoot(UINodeId root, UINodeId node) const noexcept
    {
        if (!contains(root) || !contains(node)) {
            return false;
        }

        const NodeRecord* nodeRecord = nodes.tryGet(node.storageId());
        if (nodeRecord == nullptr) {
            return false;
        }
        return nodeRecord->rootIndex == root.index();
    }

    [[nodiscard]] Core::Result<UINodeId> createNode(UIWidgetKind kind)
    {
        auto idResult = nodes.tryEmplace();
        if (!idResult) {
            const Core::Error& error = idResult.error();
            if (error.code == Core::CoreErrorCode::CapacityExceeded) {
                return fail(UIErrorCode::CapacityExceeded, "UI node capacity has been exhausted");
            }
            return Core::failure(error);
        }

        const UINodeId node = UINodeId::create(ownerWindow, *idResult);
        idsByIndex[node.index()] = node;
        NodeRecord* record = nodes.tryGet(node.storageId());
        record->kind = kind;
        record->rootIndex = node.index();
        return node;
    }

    [[nodiscard]] Core::Result<UIRootOwner> createRoot(UIContext& context)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();
        if (liveRootCount >= capacityConfig.rootCapacity) {
            return fail(UIErrorCode::CapacityExceeded, "UI root capacity has been exhausted");
        }

        auto nodeResult = createNode(UIWidgetKind::Root);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }

        const UINodeId root = *nodeResult;
        NodeRecord* rootRecord = nodes.tryGet(root.storageId());
        rootRecord->parentIndex = InvalidNodeIndex;
        rootRecord->previousSiblingIndex = lastRootIndex;
        rootRecord->nextSiblingIndex = InvalidNodeIndex;
        rootRecord->depth = 0;

        if (lastRootIndex != InvalidNodeIndex) {
            recordByIndex(lastRootIndex)->nextSiblingIndex = root.index();
        } else {
            firstRootIndex = root.index();
        }
        lastRootIndex = root.index();
        ++liveRootCount;
        dirty = true;
        return UIRootOwner(context.m_impl->lifetime, root);
    }

    [[nodiscard]] Core::Result<UINodeId> createChild(UINodeId parent, UIWidgetKind kind)
    {
        if (kind == UIWidgetKind::Root) {
            return fail(UIErrorCode::InvalidParent, "Root nodes cannot be created as children");
        }
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return Core::failure(ownerThread.error());
        }
        drainDeferredRootDestroys();

        auto parentResult = resolveParent(parent);
        if (!parentResult) {
            return Core::failure(parentResult.error());
        }
        NodeRecord& parentRecord = **parentResult;
        if (parentRecord.depth == (std::numeric_limits<u32>::max)()) {
            return fail(UIErrorCode::InvalidParent, "UI parent depth cannot be represented");
        }

        auto nodeResult = createNode(kind);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }

        const UINodeId node = *nodeResult;
        NodeRecord* childRecord = nodes.tryGet(node.storageId());
        childRecord->parentIndex = parent.index();
        childRecord->previousSiblingIndex = parentRecord.lastChildIndex;
        childRecord->nextSiblingIndex = InvalidNodeIndex;
        childRecord->rootIndex = parentRecord.rootIndex;
        childRecord->depth = parentRecord.depth + 1;

        if (parentRecord.lastChildIndex != InvalidNodeIndex) {
            recordByIndex(parentRecord.lastChildIndex)->nextSiblingIndex = node.index();
        } else {
            parentRecord.firstChildIndex = node.index();
        }
        parentRecord.lastChildIndex = node.index();
        dirty = true;
        return node;
    }

    void unlinkFromTree(u32 index, NodeRecord& record) noexcept
    {
        if (record.parentIndex != InvalidNodeIndex) {
            NodeRecord* parent = recordByIndex(record.parentIndex);
            if (parent != nullptr) {
                if (parent->firstChildIndex == index) {
                    parent->firstChildIndex = record.nextSiblingIndex;
                }
                if (parent->lastChildIndex == index) {
                    parent->lastChildIndex = record.previousSiblingIndex;
                }
            }
        } else {
            if (firstRootIndex == index) {
                firstRootIndex = record.nextSiblingIndex;
            }
            if (lastRootIndex == index) {
                lastRootIndex = record.previousSiblingIndex;
            }
        }

        if (record.previousSiblingIndex != InvalidNodeIndex) {
            if (NodeRecord* previous = recordByIndex(record.previousSiblingIndex);
                previous != nullptr) {
                previous->nextSiblingIndex = record.nextSiblingIndex;
            }
        }
        if (record.nextSiblingIndex != InvalidNodeIndex) {
            if (NodeRecord* next = recordByIndex(record.nextSiblingIndex); next != nullptr) {
                next->previousSiblingIndex = record.previousSiblingIndex;
            }
        }
    }

    void eraseDetachedSubtree(u32 index) noexcept
    {
        u32 currentIndex = index;
        while (currentIndex != InvalidNodeIndex) {
            NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr) {
                return;
            }

            if (record->firstChildIndex != InvalidNodeIndex) {
                currentIndex = record->firstChildIndex;
                continue;
            }

            const u32 parentIndex = record->parentIndex;
            const u32 nextSiblingIndex = record->nextSiblingIndex;
            if (currentIndex != index) {
                unlinkFromTree(currentIndex, *record);
            }

            const UINodeId node = idForIndex(currentIndex);
            idsByIndex[currentIndex] = {};
            static_cast<void>(nodes.erase(node.storageId()));

            if (currentIndex == index) {
                return;
            }
            currentIndex = nextSiblingIndex != InvalidNodeIndex
                ? nextSiblingIndex
                : parentIndex;
        }
    }

    [[nodiscard]] Core::Status destroySubtree(UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }

        auto nodeResult = resolveNode(node);
        if (!nodeResult) {
            return Core::failure(nodeResult.error());
        }

        NodeRecord& record = **nodeResult;
        const bool wasRoot = record.kind == UIWidgetKind::Root;
        unlinkFromTree(node.index(), record);
        eraseDetachedSubtree(node.index());
        if (wasRoot && liveRootCount > 0) {
            --liveRootCount;
        }
        dirty = true;
        return Core::success();
    }

    void destroyRootImmediately(UINodeId root) noexcept
    {
        if (!isOwnerThread() || !contains(root)) {
            return;
        }

        NodeRecord* rootRecord = nodes.tryGet(root.storageId());
        if (rootRecord == nullptr || rootRecord->kind != UIWidgetKind::Root) {
            return;
        }

        unlinkFromTree(root.index(), *rootRecord);
        eraseDetachedSubtree(root.index());
        if (liveRootCount > 0) {
            --liveRootCount;
        }
        dirty = true;
    }

    [[nodiscard]] Core::Status destroyFromUpdater(UINodeId updaterRoot, UINodeId node)
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!updaterRoot.hasValue()) {
            return fail(UIErrorCode::RootRequired, "UI tree updater requires a live root owner");
        }
        if (!contains(updaterRoot)) {
            return fail(UIErrorCode::RootRequired, "UI tree updater root is no longer alive");
        }
        if (!contains(node)) {
            auto nodeResult = resolveNode(node);
            return nodeResult ? Core::success() : Core::failure(nodeResult.error());
        }
        if (!isNodeWithinRoot(updaterRoot, node)) {
            return fail(UIErrorCode::InvalidNode, "UI node is not owned by the updater root");
        }

        if (sameNode(updaterRoot, node)) {
            return fail(
                UIErrorCode::RootRequired,
                "Destroying a root node requires UIRootOwner::reset");
        }
        return destroySubtree(node);
    }

    void appendCommittedTree(
        u32 index,
        u32& ordinal,
        std::pmr::vector<UICommittedNodeEntry>& output) const noexcept
    {
        const u32 rootIndex = index;
        u32 currentIndex = rootIndex;
        while (currentIndex != InvalidNodeIndex) {
            const NodeRecord* record = recordByIndex(currentIndex);
            if (record == nullptr) {
                return;
            }

            const u32 currentOrdinal = ordinal++;
            output.push_back(UICommittedNodeEntry{
                .node = idForIndex(currentIndex),
                .parent = idForIndex(record->parentIndex),
                .depth = record->depth,
                .preorder = currentOrdinal,
                .paintOrdinal = currentOrdinal,
                .kind = record->kind,
            });

            if (record->firstChildIndex != InvalidNodeIndex) {
                currentIndex = record->firstChildIndex;
                continue;
            }

            while (currentIndex != rootIndex) {
                record = recordByIndex(currentIndex);
                if (record == nullptr) {
                    return;
                }
                if (record->nextSiblingIndex != InvalidNodeIndex) {
                    currentIndex = record->nextSiblingIndex;
                    break;
                }
                currentIndex = record->parentIndex;
            }
            if (currentIndex == rootIndex) {
                currentIndex = InvalidNodeIndex;
            }
        }
    }

    [[nodiscard]] Core::Status commitStructure()
    {
        if (Core::Status ownerThread = ensureOwnerThread(); !ownerThread) {
            return ownerThread;
        }
        drainDeferredRootDestroys();
        if (!dirty) {
            return Core::success();
        }

        const usize writeBufferIndex = 1 - publishedBufferIndex;
        std::pmr::vector<UICommittedNodeEntry>& writeBuffer =
            committedBuffers[writeBufferIndex];
        writeBuffer.clear();

        u32 ordinal = 0;
        u32 rootIndex = firstRootIndex;
        while (rootIndex != InvalidNodeIndex) {
            const NodeRecord* root = recordByIndex(rootIndex);
            const u32 nextRootIndex = root == nullptr
                ? InvalidNodeIndex
                : root->nextSiblingIndex;
            appendCommittedTree(rootIndex, ordinal, writeBuffer);
            rootIndex = nextRootIndex;
        }

        publishedBufferIndex = writeBufferIndex;
        ++committedRevision;
        dirty = false;
        return Core::success();
    }

    [[nodiscard]] UICommittedStructureView committedStructure() const noexcept
    {
        const std::pmr::vector<UICommittedNodeEntry>& entries =
            committedBuffers[publishedBufferIndex];
        return UICommittedStructureView{
            std::span<const UICommittedNodeEntry>(entries.data(), entries.size()),
            committedRevision,
        };
    }

    [[nodiscard]] UIContextStatistics statistics() const noexcept
    {
        return UIContextStatistics{
            .nodeCapacity = capacityConfig.nodeCapacity,
            .rootCapacity = capacityConfig.rootCapacity,
            .liveNodeCount = nodes.activeCount(),
            .liveRootCount = liveRootCount,
            .committedNodeCount = committedBuffers[publishedBufferIndex].size(),
            .committedRevision = committedRevision,
            .dirty = dirty,
        };
    }
};

UIRootOwner::UIRootOwner(
    std::weak_ptr<Detail::UIContextLifetimeControl> lifetime,
    UINodeId root) noexcept
    : m_lifetime(std::move(lifetime)), m_root(root)
{
}

UIRootOwner::~UIRootOwner() noexcept
{
    reset();
}

UIRootOwner::UIRootOwner(UIRootOwner&& other) noexcept
    : m_lifetime(std::move(other.m_lifetime)), m_root(other.m_root)
{
    other.m_root = {};
}

UIRootOwner& UIRootOwner::operator=(UIRootOwner&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    reset();
    m_lifetime = std::move(other.m_lifetime);
    m_root = other.m_root;
    other.m_root = {};
    return *this;
}

void UIRootOwner::reset() noexcept
{
    const UINodeId root = m_root;
    if (!root.hasValue()) {
        m_lifetime.reset();
        return;
    }

    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime = m_lifetime.lock();
    if (!lifetime) {
        m_root = {};
        m_lifetime.reset();
        return;
    }

    UIContext* context = nullptr;
    {
        const std::scoped_lock lock(lifetime->mutex);
        context = lifetime->context;
        if (context != nullptr
            && std::this_thread::get_id() != lifetime->ownerThreadId) {
            // One move-only owner exists per live root, so the queue reserved to
            // rootCapacity cannot fill before the owner thread drains it.
            if (lifetime->deferredRootDestroys.size()
                == lifetime->deferredRootDestroys.capacity()) {
                std::terminate();
            }
            lifetime->deferredRootDestroys.push_back(root);
            context = nullptr;
        }
    }

    if (context != nullptr) {
        context->destroyRootFromOwner(root);
    }
    m_root = {};
    m_lifetime.reset();
}

UINodeId UIRootOwner::rootNodeId() const noexcept
{
    return m_root;
}

bool UIRootOwner::hasValue() const noexcept
{
    return m_root.hasValue();
}

UIRootOwner::operator bool() const noexcept
{
    return hasValue();
}

UIRootBuilder::UIRootBuilder(UIContext& context) noexcept
    : m_context(&context)
{
}

Core::Result<UIRootOwner> UIRootBuilder::createRoot()
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createRoot();
}

Core::Result<UINodeId> UIRootBuilder::createPanel(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Panel);
}

Core::Result<UINodeId> UIRootBuilder::createLabel(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Label);
}

Core::Result<UINodeId> UIRootBuilder::createButton(UINodeId parent)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI root builder is not bound to a context");
    }
    return m_context->createChild(parent, UIWidgetKind::Button);
}

UITreeUpdater::UITreeUpdater(UIContext& context, UINodeId root) noexcept
    : m_context(&context), m_root(root)
{
}

UITreeUpdater::UITreeUpdater(UITreeUpdater&& other) noexcept
    : m_context(std::exchange(other.m_context, nullptr)),
      m_root(std::exchange(other.m_root, {}))
{
}

UITreeUpdater& UITreeUpdater::operator=(UITreeUpdater&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    m_context = std::exchange(other.m_context, nullptr);
    m_root = std::exchange(other.m_root, {});
    return *this;
}

bool UITreeUpdater::isAlive(UINodeId node) const noexcept
{
    return m_context != nullptr
        && m_context->isAliveInRoot(m_root, node);
}

Core::Status UITreeUpdater::destroy(UINodeId node)
{
    if (m_context == nullptr) {
        return fail(UIErrorCode::WrongContext, "UI tree updater is not bound to a context");
    }
    return m_context->destroyNodeFromUpdater(m_root, node);
}

Core::Result<std::unique_ptr<UIContext>> UIContext::Create(
    Platform::WindowId ownerWindow,
    UIContextCapacityConfig capacityConfig,
    std::pmr::memory_resource& resource)
{
    if (!ownerWindow.hasValue()) {
        return fail(UIErrorCode::InvalidOwnerWindow, "UI context owner window id is empty");
    }

    auto normalizedResult = normalizeCapacity(capacityConfig);
    if (!normalizedResult) {
        return Core::failure(normalizedResult.error());
    }

    try {
        const std::thread::id ownerThreadId = std::this_thread::get_id();
        auto lifetime = std::make_shared<Detail::UIContextLifetimeControl>(
            ownerThreadId,
            normalizedResult->rootCapacity);
        auto implResult =
            Impl::Create(ownerWindow, *normalizedResult, lifetime, resource);
        if (!implResult) {
            return Core::failure(implResult.error());
        }

        auto context = std::unique_ptr<UIContext>(new UIContext(std::move(*implResult)));
        lifetime->context = context.get();
        return context;
    } catch (const std::bad_alloc&) {
        return fail(Core::CoreErrorCode::OutOfMemory, "UI context allocation failed");
    } catch (const std::exception& exception) {
        return fail(Core::CoreErrorCode::Internal, std::string_view(exception.what()));
    } catch (...) {
        return fail(Core::CoreErrorCode::Internal, "UI context allocation failed");
    }
}

UIContext::UIContext(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

UIContext::~UIContext() noexcept
{
    if (m_impl) {
        m_impl->detachLifetime(this);
    }
}

Platform::WindowId UIContext::ownerWindow() const noexcept
{
    return m_impl->ownerWindow;
}

bool UIContext::contains(UINodeId node) const noexcept
{
    return m_impl->isOwnerThread() && m_impl->contains(node);
}

UIRootBuilder UIContext::rootBuilder() noexcept
{
    return UIRootBuilder(*this);
}

Core::Result<UITreeUpdater> UIContext::treeUpdater(UIRootOwner& rootOwner)
{
    if (Core::Status ownerThread = m_impl->ensureOwnerThread(); !ownerThread) {
        return Core::failure(ownerThread.error());
    }
    m_impl->drainDeferredRootDestroys();
    if (!rootOwner.hasValue()) {
        return fail(UIErrorCode::RootRequired, "UI tree updater requires a root owner");
    }
    if (rootOwner.rootNodeId().ownerWindow() != m_impl->ownerWindow) {
        return fail(
            UIErrorCode::WrongOwnerWindow,
            "UI root owner belongs to another owner window");
    }
    const std::shared_ptr<Detail::UIContextLifetimeControl> lifetime =
        rootOwner.m_lifetime.lock();
    if (!lifetime || lifetime->context == nullptr) {
        return fail(UIErrorCode::RootRequired, "UI root owner is detached");
    }
    if (lifetime->context != this) {
        return fail(UIErrorCode::WrongContext, "UI root owner belongs to another context");
    }
    if (!m_impl->contains(rootOwner.rootNodeId())) {
        return fail(UIErrorCode::RootRequired, "UI root owner is no longer alive");
    }
    return UITreeUpdater(*this, rootOwner.rootNodeId());
}

Core::Status UIContext::commitStructure()
{
    return m_impl->commitStructure();
}

UICommittedStructureView UIContext::committedStructure() const noexcept
{
    return m_impl->committedStructure();
}

UIContextStatistics UIContext::statistics() const noexcept
{
    return m_impl->statistics();
}

usize UIContext::liveNodeCount() const noexcept
{
    return m_impl->nodes.activeCount();
}

usize UIContext::liveRootCount() const noexcept
{
    return m_impl->liveRootCount;
}

Core::Result<UIRootOwner> UIContext::createRoot()
{
    return m_impl->createRoot(*this);
}

Core::Result<UINodeId> UIContext::createChild(UINodeId parent, UIWidgetKind kind)
{
    return m_impl->createChild(parent, kind);
}

Core::Status UIContext::destroyNodeFromUpdater(UINodeId updaterRoot, UINodeId node)
{
    return m_impl->destroyFromUpdater(updaterRoot, node);
}

void UIContext::destroyRootFromOwner(UINodeId root) noexcept
{
    if (!m_impl->isOwnerThread()) {
        return;
    }
    m_impl->drainDeferredRootDestroys();
    m_impl->destroyRootImmediately(root);
}

bool UIContext::isAliveInRoot(UINodeId updaterRoot, UINodeId node) const noexcept
{
    if (!m_impl->isOwnerThread() || !updaterRoot.hasValue()) {
        return false;
    }
    return m_impl->isNodeWithinRoot(updaterRoot, node);
}

} // namespace Tina::UI
