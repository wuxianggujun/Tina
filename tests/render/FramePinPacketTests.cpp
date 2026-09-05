#include <gtest/gtest.h>

#include <tina/render/FramePin.hpp>
#include <tina/render/FrameResource.hpp>
#include <tina/render/RenderFramePacket.hpp>
#include <tina/render/RenderErrors.hpp>

#include <atomic>
#include <array>
#include <bit>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Tina::Tests {
namespace {

static_assert(!std::is_copy_constructible_v<Render::SubmissionTicket>);
static_assert(!std::is_copy_assignable_v<Render::SubmissionTicket>);
static_assert(std::is_move_constructible_v<Render::SubmissionTicket>);
static_assert(!std::is_move_assignable_v<Render::SubmissionTicket>);
static_assert(std::is_copy_constructible_v<Render::FrameResourceRef>);
static_assert(std::is_copy_assignable_v<Render::FrameResourceRef>);
static_assert(std::is_default_constructible_v<Render::FrameResourceRef>);
static_assert(!std::is_constructible_v<Render::FrameResourceRef, Core::u64, Core::u64, Core::u32>);

std::atomic<int> g_releaseCount{0};

void countingRelease(void* userData) noexcept
{
    ++g_releaseCount;
    delete static_cast<int*>(userData);
}

[[nodiscard]] Render::FramePin makeCountingPin(Core::u64 tag)
{
    return Render::FramePin{Render::FramePinKind::AssetLease, tag, new int{1}, &countingRelease};
}

[[nodiscard]] constexpr Render::FrameResourceDescriptor spriteTexture(Core::u64 bindingKey) noexcept
{
    return {Render::FrameResourceKind::Texture2D, bindingKey};
}

[[nodiscard]] Render::FramePin makeLocalCountingPin(Core::u32& releases) noexcept
{
    return Render::FramePin{Render::FramePinKind::Custom, 1, &releases, [](void* context) noexcept {
                               ++*static_cast<Core::u32*>(context);
                           }};
}

TEST(FramePinTest, ReleaseRunsExactlyOnce)
{
    g_releaseCount.store(0);
    {
        Render::FramePin pin{
            Render::FramePinKind::Custom,
            7,
            new int{42},
            &countingRelease,
        };
        EXPECT_TRUE(pin.hasValue());
        pin.release();
        EXPECT_FALSE(pin.hasValue());
        EXPECT_EQ(g_releaseCount.load(), 1);
        pin.release();
        EXPECT_EQ(g_releaseCount.load(), 1);
    }
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(FramePinTest, MoveTransfersOwnershipWithoutDoubleRelease)
{
    g_releaseCount.store(0);
    Render::FramePin a{
        Render::FramePinKind::AssetLease,
        1,
        new int{1},
        &countingRelease,
    };
    Render::FramePin b{std::move(a)};
    EXPECT_FALSE(a.hasValue());
    EXPECT_TRUE(b.hasValue());
    b.release();
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(FramePinTest, AddRejectsMismatchedKindWithoutConsumingCallerPin)
{
    g_releaseCount.store(0);
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    Render::FramePin pin = makeCountingPin(1);

    auto status = packet.add(Render::FramePinKind::Surface, std::move(pin));

    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, Render::RenderErrorCode::InvalidFramePin);
    EXPECT_TRUE(pin.hasValue());
    EXPECT_EQ(packet.pinCount(), 0U);
    pin.release();
    EXPECT_EQ(g_releaseCount.load(), 1);
    ASSERT_TRUE(packet.completeSkipped().has_value());
}

TEST(FrameResourceTest, DefaultRefAndViewAreInvalid)
{
    constexpr Render::FrameResourceRef ref{};
    Render::FrameResourceTableView view{};

    static_assert(!ref.hasValue());
    EXPECT_TRUE(view.empty());
    EXPECT_EQ(view.resolve(ref, Render::FrameResourceKind::Texture2D), nullptr);
}

TEST(FrameResourceTest, InternDeduplicatesAndResolvesDescriptor)
{
    g_releaseCount.store(0);
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(7).has_value());
    Render::FramePin firstPin = makeCountingPin(1);
    Render::FramePin duplicatePin = makeCountingPin(2);

    auto first = packet.resourceSink().intern(spriteTexture(91), std::move(firstPin));
    auto duplicate = packet.resourceSink().intern(spriteTexture(91), std::move(duplicatePin));

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(duplicate.has_value());
    EXPECT_EQ(*first, *duplicate);
    EXPECT_FALSE(firstPin.hasValue());
    EXPECT_FALSE(duplicatePin.hasValue());
    EXPECT_EQ(packet.resourceCount(), 1U);
    EXPECT_EQ(g_releaseCount.load(), 1);

    const Render::FrameResourceTableView view = packet.resourceTableView();
    ASSERT_EQ(view.size(), 1U);
    const auto* descriptor =
        view.resolve(*first, Render::FrameResourceKind::Texture2D);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->deviceBindingKey, 91U);
    EXPECT_EQ(view.resolve(*first, Render::FrameResourceKind::Invalid), nullptr);

    Render::RenderFrame frame{};
    frame.resources = view;
    EXPECT_EQ(frame.resources.resolve(*first, Render::FrameResourceKind::Texture2D),
              descriptor);

    ASSERT_TRUE(packet.completeSkipped().has_value());
    EXPECT_EQ(g_releaseCount.load(), 2);
    EXPECT_TRUE(view.empty());
    EXPECT_EQ(view.resolve(*first, Render::FrameResourceKind::Texture2D), nullptr);
}

TEST(FrameResourceTest, LookupCollisionsKeepDescriptorIdentityAndReuseSlotsNextFrame)
{
    constexpr Core::u32 resourceCapacity = 2U;
    constexpr Core::u32 slotCount = std::bit_ceil(resourceCapacity * 2U);
    std::array<Core::u64, slotCount> firstKeyBySlot{};
    Core::u64 firstKey = 0;
    Core::u64 secondKey = 0;
    for (Core::u64 key = 1; key <= slotCount + 1U; ++key)
    {
        const auto slot = Render::Detail::hashFrameResourceDescriptor(spriteTexture(key)) & (slotCount - 1U);
        if (firstKeyBySlot[slot] != 0)
        {
            firstKey = firstKeyBySlot[slot];
            secondKey = key;
            break;
        }
        firstKeyBySlot[slot] = key;
    }
    ASSERT_NE(firstKey, 0U);
    ASSERT_NE(secondKey, 0U);

    Core::u32 releases = 0;
    Render::RenderFramePacket packet({.resourceCapacity = resourceCapacity});
    for (Core::u64 frame = 1; frame <= 3; ++frame)
    {
        ASSERT_TRUE(packet.beginFrame(frame));
        EXPECT_EQ(packet.resourceLookupProbeCount(), 0U);
        auto first = packet.intern(spriteTexture(firstKey), makeLocalCountingPin(releases));
        auto second = packet.intern(spriteTexture(secondKey), makeLocalCountingPin(releases));
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        EXPECT_NE(*first, *second);
        EXPECT_EQ(first->index(), 0U);
        EXPECT_EQ(second->index(), 1U);
        EXPECT_EQ(packet.resourceLookupProbeCount(), 3U);
        auto duplicate = packet.intern(spriteTexture(secondKey), makeLocalCountingPin(releases));
        ASSERT_TRUE(duplicate);
        EXPECT_EQ(*duplicate, *second);
        EXPECT_EQ(packet.resourceLookupProbeCount(), 5U);
        const auto view = packet.resourceTableView();
        ASSERT_NE(view.resolve(*first, Render::FrameResourceKind::Texture2D), nullptr);
        EXPECT_EQ(view.resolve(*first, Render::FrameResourceKind::Texture2D)->deviceBindingKey, firstKey);
        ASSERT_NE(view.resolve(*second, Render::FrameResourceKind::Texture2D), nullptr);
        EXPECT_EQ(view.resolve(*second, Render::FrameResourceKind::Texture2D)->deviceBindingKey, secondKey);
        ASSERT_TRUE(packet.completeSkipped());
        EXPECT_EQ(releases, frame * 3U);
    }
}

TEST(FrameResourceTest, LargeMixedWorkingSetDeduplicatesWithoutQuadraticScanning)
{
    constexpr Core::u32 resourceCapacity = 8'193U;
    constexpr std::array kinds{
        Render::FrameResourceKind::Texture2D,
        Render::FrameResourceKind::Mesh3DGeometry,
        Render::FrameResourceKind::Mesh3DMaterial,
        Render::FrameResourceKind::SkinnedMesh3DGeometry,
        Render::FrameResourceKind::Shader,
        Render::FrameResourceKind::ShaderUniforms,
    };
    const auto descriptorAt = [&](Core::u32 index) {
        // Keys share low bits: hashing only a narrowed key would create one long chain.
        return Render::FrameResourceDescriptor{
            kinds[index % kinds.size()], (static_cast<Core::u64>(index / kinds.size()) + 1U) << 32U | 17U};
    };
    Core::u32 releases = 0;
    Render::RenderFramePacket packet({.resourceCapacity = resourceCapacity});
    std::vector<Render::FrameResourceRef> refs(resourceCapacity);
    for (Core::u64 frame = 1; frame <= 2; ++frame)
    {
        ASSERT_TRUE(packet.beginFrame(frame));
        for (Core::u32 index = 0; index < resourceCapacity; ++index)
        {
            auto ref = packet.intern(descriptorAt(index), makeLocalCountingPin(releases));
            ASSERT_TRUE(ref) << index;
            EXPECT_EQ(ref->index(), index);
            refs[index] = *ref;
        }
        const auto view = packet.resourceTableView();
        for (Core::u32 remaining = resourceCapacity; remaining != 0; --remaining)
        {
            const Core::u32 index = remaining - 1U;
            const auto descriptor = descriptorAt(index);
            auto duplicate = packet.intern(descriptor, makeLocalCountingPin(releases));
            ASSERT_TRUE(duplicate);
            EXPECT_EQ(*duplicate, refs[index]);
            const auto* resolved = view.resolve(*duplicate, descriptor.kind);
            ASSERT_NE(resolved, nullptr);
            EXPECT_EQ(*resolved, descriptor);
        }
        EXPECT_EQ(packet.resourceCount(), resourceCapacity);
        EXPECT_LT(packet.resourceLookupProbeCount(), static_cast<Core::u64>(resourceCapacity) * 16U);
        auto rejectedPin = makeLocalCountingPin(releases);
        auto rejected = packet.intern(spriteTexture(1U), std::move(rejectedPin));
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error().code, Render::RenderErrorCode::FrameResourceCapacityExceeded);
        EXPECT_TRUE(rejectedPin.hasValue());
        rejectedPin.release();
        ASSERT_TRUE(packet.abandon());
        EXPECT_EQ(releases, frame * (resourceCapacity * 2U + 1U));
        EXPECT_TRUE(view.empty());
    }
}

TEST(FrameResourceTest, CapacityCoversDefaultSpriteMeshAndMaterialWorkingSets)
{
    constexpr Core::u32 SpriteTextureCount = 64;
    constexpr Core::u32 MeshGeometryCount = 128;
    constexpr Core::u32 MeshMaterialCount = 128;
    constexpr Core::u32 ExpectedResourceCount =
        SpriteTextureCount + MeshGeometryCount + MeshMaterialCount;
    static_assert(Render::RenderFramePacket::MaxResources >= ExpectedResourceCount);

    g_releaseCount.store(0);
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(8).has_value());

    struct ResourceDomain final {
        Render::FrameResourceKind kind;
        Core::u32 count;
    };
    constexpr ResourceDomain domains[]{
        {Render::FrameResourceKind::Texture2D, SpriteTextureCount},
        {Render::FrameResourceKind::Mesh3DGeometry, MeshGeometryCount},
        {Render::FrameResourceKind::Mesh3DMaterial, MeshMaterialCount},
    };
    Core::u32 descriptorCount = 0;
    for (const ResourceDomain domain : domains)
    {
        for (Core::u32 index = 0; index < domain.count; ++index)
        {
            const auto resource = packet.intern(
                Render::FrameResourceDescriptor{domain.kind, index + 1U},
                makeCountingPin(descriptorCount + 1U));
            ASSERT_TRUE(resource.has_value()) << descriptorCount;
            ++descriptorCount;
        }
    }

    EXPECT_EQ(descriptorCount, ExpectedResourceCount);
    EXPECT_EQ(packet.resourceCount(), descriptorCount);
    ASSERT_TRUE(packet.completeSkipped().has_value());
    EXPECT_EQ(g_releaseCount.load(), static_cast<int>(descriptorCount));
}

TEST(FrameResourceTest, InvalidInputAndCapacityFailureDoNotConsumeCallerPin)
{
    g_releaseCount.store(0);
    Render::RenderFramePacket packet;
    Render::FramePin beforeBegin = makeCountingPin(1);
    auto wrongState = packet.intern(spriteTexture(1), std::move(beforeBegin));
    ASSERT_FALSE(wrongState.has_value());
    EXPECT_TRUE(beforeBegin.hasValue());

    ASSERT_TRUE(packet.beginFrame(1).has_value());
    Render::FramePin invalidDescriptorPin = makeCountingPin(2);
    auto invalidDescriptor = packet.intern(spriteTexture(0), std::move(invalidDescriptorPin));
    ASSERT_FALSE(invalidDescriptor.has_value());
    EXPECT_EQ(invalidDescriptor.error().code, Render::RenderErrorCode::InvalidFrameResource);
    EXPECT_TRUE(invalidDescriptorPin.hasValue());

    for (Core::u32 index = 0; index < Render::RenderFramePacket::MaxResources; ++index)
    {
        ASSERT_TRUE(packet.intern(spriteTexture(index + 1), makeCountingPin(index + 10)).has_value())
            << index;
    }
    Render::FramePin overflowPin = makeCountingPin(1000);
    auto overflow = packet.intern(spriteTexture(1000), std::move(overflowPin));
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, Render::RenderErrorCode::FrameResourceCapacityExceeded);
    EXPECT_TRUE(overflowPin.hasValue());

    Render::FramePin duplicatePin = makeCountingPin(1001);
    auto duplicate = packet.intern(spriteTexture(1), std::move(duplicatePin));
    ASSERT_TRUE(duplicate.has_value());
    EXPECT_FALSE(duplicatePin.hasValue());
    EXPECT_EQ(packet.resourceCount(), Render::RenderFramePacket::MaxResources);

    beforeBegin.release();
    invalidDescriptorPin.release();
    overflowPin.release();
    ASSERT_TRUE(packet.completeSkipped().has_value());
    EXPECT_EQ(g_releaseCount.load(), static_cast<int>(Render::RenderFramePacket::MaxResources + 4));
}

TEST(FrameResourceTest, OwnerGenerationAndKindValidationFailClosed)
{
    Render::RenderFramePacket firstPacket;
    Render::RenderFramePacket secondPacket;
    ASSERT_TRUE(firstPacket.beginFrame(5).has_value());
    ASSERT_TRUE(secondPacket.beginFrame(5).has_value());
    auto firstRef = firstPacket.intern(spriteTexture(11), makeCountingPin(1));
    auto secondRef = secondPacket.intern(spriteTexture(22), makeCountingPin(2));
    ASSERT_TRUE(firstRef.has_value());
    ASSERT_TRUE(secondRef.has_value());

    const auto firstView = firstPacket.resourceTableView();
    EXPECT_NE(firstView.resolve(*firstRef, Render::FrameResourceKind::Texture2D), nullptr);
    EXPECT_EQ(firstView.resolve(*secondRef, Render::FrameResourceKind::Texture2D), nullptr);
    EXPECT_EQ(firstView.resolve(*firstRef, Render::FrameResourceKind::Invalid), nullptr);

    ASSERT_TRUE(firstPacket.beginFrame(5).has_value());
    auto replacementRef = firstPacket.intern(spriteTexture(33), makeCountingPin(3));
    ASSERT_TRUE(replacementRef.has_value());
    const auto replacementView = firstPacket.resourceTableView();
    EXPECT_TRUE(firstView.empty());
    EXPECT_EQ(firstView.resolve(*replacementRef, Render::FrameResourceKind::Texture2D), nullptr);
    EXPECT_EQ(replacementView.resolve(*firstRef, Render::FrameResourceKind::Texture2D), nullptr);
    const auto* replacement =
        replacementView.resolve(*replacementRef, Render::FrameResourceKind::Texture2D);
    ASSERT_NE(replacement, nullptr);
    EXPECT_EQ(replacement->deviceBindingKey, 33U);

    ASSERT_TRUE(firstPacket.completeSkipped().has_value());
    ASSERT_TRUE(secondPacket.completeSkipped().has_value());
}

TEST(FrameResourceTest, ResourcePinsReleaseExactlyOnceForEveryPacketClosure)
{
    g_releaseCount.store(0);
    Render::RenderFramePacket packet;
    Render::CpuSubmissionCompletionLedger ledger;

    ASSERT_TRUE(packet.beginFrame(1).has_value());
    ASSERT_TRUE(packet.intern(spriteTexture(1), makeCountingPin(1)).has_value());
    auto ticket = ledger.beginSubmitted(1);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
    ASSERT_TRUE(packet.complete().has_value());
    ASSERT_TRUE(packet.complete().has_value());
    EXPECT_EQ(g_releaseCount.load(), 1);

    ASSERT_TRUE(packet.beginFrame(2).has_value());
    ASSERT_TRUE(packet.intern(spriteTexture(2), makeCountingPin(2)).has_value());
    ASSERT_TRUE(packet.completeSkipped().has_value());
    ASSERT_TRUE(packet.completeSkipped().has_value());
    EXPECT_EQ(g_releaseCount.load(), 2);

    ASSERT_TRUE(packet.beginFrame(3).has_value());
    ASSERT_TRUE(packet.intern(spriteTexture(3), makeCountingPin(3)).has_value());
    ASSERT_TRUE(packet.abandon().has_value());
    ASSERT_TRUE(packet.abandon().has_value());
    EXPECT_EQ(g_releaseCount.load(), 3);

    ASSERT_TRUE(packet.beginFrame(4).has_value());
    ASSERT_TRUE(packet.intern(spriteTexture(4), makeCountingPin(4)).has_value());
    ASSERT_TRUE(packet.beginFrame(5).has_value());
    EXPECT_EQ(g_releaseCount.load(), 4);
    ASSERT_TRUE(packet.completeSkipped().has_value());
}

TEST(FrameResourceTest, CompleteAbandonAndDestructorInvalidateViewsAndReleaseExactlyOnce)
{
    g_releaseCount.store(0);
    Render::CpuSubmissionCompletionLedger ledger;

    Render::FrameResourceTableView completedView;
    {
        Render::RenderFramePacket packet;
        ASSERT_TRUE(packet.beginFrame(1).has_value());
        auto ref = packet.intern(spriteTexture(1), makeCountingPin(1));
        ASSERT_TRUE(ref.has_value());
        completedView = packet.resourceTableView();
        auto ticket = ledger.beginSubmitted(1);
        ASSERT_TRUE(ticket.has_value());
        ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
        ASSERT_TRUE(packet.complete().has_value());
        EXPECT_TRUE(completedView.empty());
        EXPECT_EQ(completedView.resolve(*ref, Render::FrameResourceKind::Texture2D), nullptr);
    }
    EXPECT_EQ(g_releaseCount.load(), 1);

    Render::FrameResourceTableView abandonedView;
    {
        Render::RenderFramePacket packet;
        ASSERT_TRUE(packet.beginFrame(2).has_value());
        auto ref = packet.intern(spriteTexture(2), makeCountingPin(2));
        ASSERT_TRUE(ref.has_value());
        abandonedView = packet.resourceTableView();
        ASSERT_TRUE(packet.abandon().has_value());
        EXPECT_TRUE(abandonedView.empty());
        EXPECT_EQ(abandonedView.resolve(*ref, Render::FrameResourceKind::Texture2D), nullptr);
    }
    EXPECT_EQ(g_releaseCount.load(), 2);

    {
        Render::RenderFramePacket packet;
        ASSERT_TRUE(packet.beginFrame(3).has_value());
        ASSERT_TRUE(packet.intern(spriteTexture(3), makeCountingPin(3)).has_value());
    }
    EXPECT_EQ(g_releaseCount.load(), 3);
}

TEST(FrameResourceTest, ReconstructedPacketAtSameAddressCannotResolveOldRef)
{
    alignas(Render::RenderFramePacket)
        std::byte storage[sizeof(Render::RenderFramePacket)];

    auto* firstPacket = std::construct_at(reinterpret_cast<Render::RenderFramePacket*>(storage));
    ASSERT_TRUE(firstPacket->beginFrame(1).has_value());
    auto oldRef = firstPacket->intern(spriteTexture(7), makeCountingPin(1));
    ASSERT_TRUE(oldRef.has_value());
    std::destroy_at(firstPacket);

    auto* secondPacket = std::construct_at(reinterpret_cast<Render::RenderFramePacket*>(storage));
    ASSERT_TRUE(secondPacket->beginFrame(1).has_value());
    auto newRef = secondPacket->intern(spriteTexture(7), makeCountingPin(2));
    ASSERT_TRUE(newRef.has_value());
    const auto newView = secondPacket->resourceTableView();
    EXPECT_EQ(newView.resolve(*oldRef, Render::FrameResourceKind::Texture2D), nullptr);
    EXPECT_NE(newView.resolve(*newRef, Render::FrameResourceKind::Texture2D), nullptr);
    std::destroy_at(secondPacket);
}

TEST(FrameResourceTest, OrdinaryAndResourcePinCapacitiesAreIndependent)
{
    g_releaseCount.store(0);
    Render::RenderFramePacket ordinaryFull;
    ASSERT_TRUE(ordinaryFull.beginFrame(1).has_value());
    for (Core::u32 index = 0; index < Render::RenderFramePacket::MaxPins; ++index)
    {
        ASSERT_TRUE(ordinaryFull.add(Render::FramePinKind::AssetLease, makeCountingPin(index)).has_value());
    }
    ASSERT_TRUE(ordinaryFull.intern(spriteTexture(1), makeCountingPin(100)).has_value());
    EXPECT_EQ(ordinaryFull.pinCount(), Render::RenderFramePacket::MaxPins);
    EXPECT_EQ(ordinaryFull.resourceCount(), 1U);
    ASSERT_TRUE(ordinaryFull.completeSkipped().has_value());

    Render::RenderFramePacket resourcesFull;
    ASSERT_TRUE(resourcesFull.beginFrame(2).has_value());
    for (Core::u32 index = 0; index < Render::RenderFramePacket::MaxResources; ++index)
    {
        ASSERT_TRUE(resourcesFull.intern(spriteTexture(index + 1), makeCountingPin(index)).has_value());
    }
    ASSERT_TRUE(resourcesFull.add(Render::FramePinKind::AssetLease, makeCountingPin(200)).has_value());
    EXPECT_EQ(resourcesFull.resourceCount(), Render::RenderFramePacket::MaxResources);
    EXPECT_EQ(resourcesFull.pinCount(), 1U);
    ASSERT_TRUE(resourcesFull.completeSkipped().has_value());

    EXPECT_EQ(g_releaseCount.load(),
              static_cast<int>(Render::RenderFramePacket::MaxPins
                               + Render::RenderFramePacket::MaxResources + 2));
}

TEST(CpuSubmissionCompletionLedgerTest, BeginCompleteBalancesInflight)
{
    Render::CpuSubmissionCompletionLedger ledger{4};
    auto t1 = ledger.beginSubmitted(1);
    ASSERT_TRUE(t1.has_value());
    auto t2 = ledger.beginSubmitted(2);
    ASSERT_TRUE(t2.has_value());
    EXPECT_EQ(ledger.inflightCount(), 2U);

    ASSERT_TRUE(ledger.complete(*t1).has_value());
    EXPECT_EQ(ledger.inflightCount(), 1U);
    EXPECT_EQ(ledger.lastCompletedSubmission(), 1U);

    ASSERT_TRUE(ledger.complete(*t2).has_value());
    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.completedCount(), 2U);
}

TEST(CpuSubmissionCompletionLedgerTest, CapacityFailureDoesNotLeakInflight)
{
    Render::CpuSubmissionCompletionLedger ledger{1};
    auto t1 = ledger.beginSubmitted(10);
    ASSERT_TRUE(t1.has_value());
    auto t2 = ledger.beginSubmitted(11);
    ASSERT_FALSE(t2.has_value());
    EXPECT_EQ(t2.error().code, Render::RenderErrorCode::SubmissionCompletionLedgerFull);
    EXPECT_EQ(ledger.inflightCount(), 1U);
    ASSERT_TRUE(ledger.complete(*t1).has_value());
    EXPECT_TRUE(ledger.allClear());
}

TEST(CpuSubmissionCompletionLedgerTest, RepeatedCompleteCannotConsumeAnotherOpenTicket)
{
    Render::CpuSubmissionCompletionLedger ledger{2};
    auto first = ledger.beginSubmitted(1);
    auto second = ledger.beginSubmitted(2);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    ASSERT_TRUE(ledger.complete(*first).has_value());
    EXPECT_FALSE(first->hasValue());
    ASSERT_TRUE(ledger.complete(*first).has_value());
    EXPECT_EQ(ledger.inflightCount(), 1U);
    EXPECT_FALSE(ledger.allClear());

    ASSERT_TRUE(ledger.complete(*second).has_value());
    EXPECT_TRUE(ledger.allClear());
}

TEST(CpuSubmissionCompletionLedgerTest, WrongLedgerCannotConsumeTicket)
{
    Render::CpuSubmissionCompletionLedger issuingLedger{1};
    Render::CpuSubmissionCompletionLedger otherLedger{1};
    auto ticket = issuingLedger.beginSubmitted(7);
    ASSERT_TRUE(ticket.has_value());

    auto wrongLedgerStatus = otherLedger.complete(*ticket);
    ASSERT_FALSE(wrongLedgerStatus.has_value());
    EXPECT_EQ(wrongLedgerStatus.error().code, Render::RenderErrorCode::InvalidSubmissionTicket);
    EXPECT_TRUE(ticket->hasValue());
    EXPECT_EQ(issuingLedger.inflightCount(), 1U);
    EXPECT_TRUE(otherLedger.allClear());

    ASSERT_TRUE(issuingLedger.complete(*ticket).has_value());
    EXPECT_TRUE(issuingLedger.allClear());
}

TEST(CpuSubmissionCompletionLedgerTest, DroppedTicketAbandonsIssuingLedger)
{
    Render::CpuSubmissionCompletionLedger ledger{1};
    {
        auto ticket = ledger.beginSubmitted(9);
        ASSERT_TRUE(ticket.has_value());
        EXPECT_EQ(ledger.inflightCount(), 1U);
    }

    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.abandonedCount(), 1U);
}

// Counting mock proves Host/packet call sites use the SPI, not a concrete Null type only.
class CountingMockSubmissionCompletionLedger final : public Render::ISubmissionCompletionLedger {
public:
    [[nodiscard]] Core::Result<Render::SubmissionTicket> beginSubmitted(Core::u64 submissionIndex) override
    {
        ++beginCalls;
        if (full)
        {
            return Core::failure(Render::RenderErrorCode::SubmissionCompletionLedgerFull, "mock full");
        }
        ++inflight;
        return makeSubmissionTicket(submissionIndex);
    }

protected:
    [[nodiscard]] Core::Status completeOwned(Core::u64 submissionIndex) noexcept override
    {
        ++completeCalls;
        if (inflight == 0)
        {
            return Core::failure(Render::RenderErrorCode::InvalidSubmissionTicket, "mock empty");
        }
        --inflight;
        lastCompleted = submissionIndex;
        return Core::success();
    }

    [[nodiscard]] Core::Status abandonOwned(Core::u64) noexcept override
    {
        ++abandonCalls;
        if (inflight == 0)
        {
            return Core::failure(Render::RenderErrorCode::InvalidSubmissionTicket, "mock empty");
        }
        --inflight;
        return Core::success();
    }

public:
    [[nodiscard]] Core::u32 inflightCount() const noexcept override { return inflight; }
    [[nodiscard]] bool allClear() const noexcept override { return inflight == 0; }

    Core::u32 beginCalls = 0;
    Core::u32 completeCalls = 0;
    Core::u32 abandonCalls = 0;
    Core::u32 inflight = 0;
    Core::u64 lastCompleted = 0;
    bool full = false;
};

TEST(ISubmissionCompletionLedgerPolymorphismTest, PacketCompleteUsesInjectedMock)
{
    g_releaseCount.store(0);
    CountingMockSubmissionCompletionLedger mock;
    Render::ISubmissionCompletionLedger& ledger = mock;
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(1).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{11}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(42);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
    ASSERT_TRUE(packet.complete().has_value());

    EXPECT_EQ(mock.beginCalls, 1U);
    EXPECT_EQ(mock.completeCalls, 1U);
    EXPECT_EQ(mock.abandonCalls, 0U);
    EXPECT_EQ(mock.lastCompleted, 42U);
    EXPECT_TRUE(mock.allClear());
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(ISubmissionCompletionLedgerPolymorphismTest, PacketAbandonUsesInjectedMock)
{
    g_releaseCount.store(0);
    CountingMockSubmissionCompletionLedger mock;
    Render::ISubmissionCompletionLedger& ledger = mock;
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(2).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{12}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(7);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
    ASSERT_TRUE(packet.abandon().has_value());

    EXPECT_EQ(mock.beginCalls, 1U);
    EXPECT_EQ(mock.completeCalls, 0U);
    EXPECT_EQ(mock.abandonCalls, 1U);
    EXPECT_TRUE(mock.allClear());
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(RenderFramePacketTest, PinsReleaseWhenCpuSubmissionCompletes)
{
    g_releaseCount.store(0);
    Render::CpuSubmissionCompletionLedger ledger{};
    Render::ISubmissionCompletionLedger& ledgerSpi = ledger;
    Render::RenderFramePacket packet;

    ASSERT_TRUE(packet.beginFrame(3).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::GlyphAtlas,
                         Render::FramePin{Render::FramePinKind::GlyphAtlas, 0, new int{1}, &countingRelease})
                    .has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Surface,
                         Render::FramePin{Render::FramePinKind::Surface, 9, new int{2}, &countingRelease})
                    .has_value());
    EXPECT_EQ(packet.pinCount(), 2U);

    auto ticket = ledgerSpi.beginSubmitted(5);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledgerSpi, std::move(*ticket)).has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Submitted);

    ASSERT_TRUE(packet.complete().has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Completed);
    EXPECT_EQ(packet.pinCount(), 0U);
    EXPECT_EQ(g_releaseCount.load(), 2);
    EXPECT_TRUE(ledgerSpi.allClear());
}

TEST(RenderFramePacketTest, AbandonReleasesPinsAndTicket)
{
    g_releaseCount.store(0);
    Render::CpuSubmissionCompletionLedger ledger{};
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{3}, &countingRelease})
                    .has_value());
    auto ticket = ledger.beginSubmitted(8);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
    EXPECT_FALSE(ticket->hasValue());
    ASSERT_TRUE(packet.abandon().has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Abandoned);
    EXPECT_EQ(g_releaseCount.load(), 1);
    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.abandonedCount(), 1U);
}

TEST(RenderFramePacketTest, DestructorAbandonsSubmittedTicket)
{
    g_releaseCount.store(0);
    Render::CpuSubmissionCompletionLedger ledger{};
    {
        Render::RenderFramePacket packet;
        ASSERT_TRUE(packet.beginFrame(1).has_value());
        ASSERT_TRUE(packet
                        .add(Render::FramePinKind::Custom,
                             Render::FramePin{Render::FramePinKind::Custom, 0, new int{5}, &countingRelease})
                        .has_value());
        auto ticket = ledger.beginSubmitted(10);
        ASSERT_TRUE(ticket.has_value());
        ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());
        EXPECT_EQ(packet.submissionIndex(), 10U);
    }

    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.abandonedCount(), 1U);
    EXPECT_EQ(g_releaseCount.load(), 1);
}

TEST(RenderFramePacketTest, BeginFrameAbandonsPreviousSubmittedTicket)
{
    Render::CpuSubmissionCompletionLedger ledger{};
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    auto ticket = ledger.beginSubmitted(11);
    ASSERT_TRUE(ticket.has_value());
    ASSERT_TRUE(packet.attachSubmission(ledger, std::move(*ticket)).has_value());

    ASSERT_TRUE(packet.beginFrame(2).has_value());
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Building);
    EXPECT_EQ(packet.frameIndex(), 2U);
    EXPECT_TRUE(ledger.allClear());
    EXPECT_EQ(ledger.abandonedCount(), 1U);
    ASSERT_TRUE(packet.completeSkipped().has_value());
}

TEST(RenderFramePacketTest, CompleteSkippedReleasesWithoutTicket)
{
    g_releaseCount.store(0);
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(2).has_value());
    ASSERT_TRUE(packet
                    .add(Render::FramePinKind::Custom,
                         Render::FramePin{Render::FramePinKind::Custom, 0, new int{4}, &countingRelease})
                    .has_value());
    ASSERT_TRUE(packet.completeSkipped().has_value());
    EXPECT_EQ(g_releaseCount.load(), 1);
    EXPECT_EQ(packet.state(), Render::RenderFramePacket::State::Completed);
}

TEST(RenderFramePacketTest, PinCapacityIsFixed)
{
    Render::RenderFramePacket packet;
    ASSERT_TRUE(packet.beginFrame(1).has_value());
    for (Core::u32 i = 0; i < Render::RenderFramePacket::MaxPins; ++i)
    {
        ASSERT_TRUE(packet
                        .add(Render::FramePinKind::Custom,
                             Render::FramePin{Render::FramePinKind::Custom, i, new int{static_cast<int>(i)},
                                              &countingRelease})
                        .has_value())
            << i;
    }
    auto overflow = packet.add(
        Render::FramePinKind::Custom,
        Render::FramePin{Render::FramePinKind::Custom, 99, new int{99}, &countingRelease});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, Render::RenderErrorCode::FramePinCapacityExceeded);
    ASSERT_TRUE(packet.completeSkipped().has_value());
}

} // namespace
} // namespace Tina::Tests
