#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <compare>

namespace Tina::Render {

class FramePin;
class RenderFramePacket;

enum class FrameResourceKind : Core::u8 {
    Invalid = 0,
    Texture2D = 1,
    Mesh3DGeometry = 2,
    Mesh3DMaterial = 3,
    // Skinned geometry shares the device Mesh3D binding-key namespace but is a
    // distinct kind: a skinned item resolving a static binding (or vice versa)
    // fails closed at resolve time instead of drawing with the wrong pipeline.
    SkinnedMesh3DGeometry = 4,
};

struct FrameResourceDescriptor final {
    FrameResourceKind kind = FrameResourceKind::Invalid;
    Core::u64 deviceBindingKey = 0;

    auto operator<=>(const FrameResourceDescriptor&) const = default;
};

// Copyable packet-local identity. Only a RenderFramePacket can issue a valid ref.
class FrameResourceRef final {
public:
    inline static constexpr Core::u32 InvalidIndex = static_cast<Core::u32>(-1);

    constexpr FrameResourceRef() noexcept = default;

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return m_packetOwner != 0 && m_frameGeneration != 0 && m_index != InvalidIndex;
    }
    [[nodiscard]] constexpr Core::u64 packetOwner() const noexcept { return m_packetOwner; }
    [[nodiscard]] constexpr Core::u64 frameGeneration() const noexcept { return m_frameGeneration; }
    [[nodiscard]] constexpr Core::u32 index() const noexcept { return m_index; }
    explicit constexpr operator bool() const noexcept { return hasValue(); }

    auto operator<=>(const FrameResourceRef&) const = default;

private:
    friend class RenderFramePacket;

    [[nodiscard]] static constexpr FrameResourceRef createForPacket(
        Core::u64 packetOwner,
        Core::u64 frameGeneration,
        Core::u32 index) noexcept
    {
        return FrameResourceRef(packetOwner, frameGeneration, index);
    }

    constexpr FrameResourceRef(
        Core::u64 packetOwner,
        Core::u64 frameGeneration,
        Core::u32 index) noexcept
        : m_packetOwner(packetOwner), m_frameGeneration(frameGeneration), m_index(index)
    {
    }

    Core::u64 m_packetOwner = 0;
    Core::u64 m_frameGeneration = 0;
    Core::u32 m_index = InvalidIndex;
};

// Borrowed packet view. The packet must outlive the view; resolve fails closed as
// soon as that packet generation completes, is abandoned, or is replaced.
class FrameResourceTableView final {
public:
    constexpr FrameResourceTableView() noexcept = default;

    [[nodiscard]] Core::u32 size() const noexcept
    {
        return isLive() ? m_count : 0;
    }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] const FrameResourceDescriptor* resolve(
        FrameResourceRef ref,
        FrameResourceKind expectedKind) const noexcept
    {
        if (!isLive() || expectedKind == FrameResourceKind::Invalid || !ref.hasValue()
            || ref.packetOwner() != m_packetOwner
            || ref.frameGeneration() != m_frameGeneration || ref.index() >= m_count)
        {
            return nullptr;
        }

        const FrameResourceDescriptor& descriptor = m_descriptors[ref.index()];
        if (descriptor.kind != expectedKind || descriptor.deviceBindingKey == 0)
        {
            return nullptr;
        }
        return &descriptor;
    }

private:
    friend class RenderFramePacket;

    constexpr FrameResourceTableView(
        const FrameResourceDescriptor* descriptors,
        Core::u32 count,
        const Core::u64* liveGeneration,
        Core::u64 packetOwner,
        Core::u64 frameGeneration) noexcept
        : m_descriptors(descriptors),
          m_count(count),
          m_liveGeneration(liveGeneration),
          m_packetOwner(packetOwner),
          m_frameGeneration(frameGeneration)
    {
    }

    [[nodiscard]] bool isLive() const noexcept
    {
        return m_descriptors != nullptr && m_liveGeneration != nullptr && m_packetOwner != 0
            && m_frameGeneration != 0 && *m_liveGeneration == m_frameGeneration;
    }

    const FrameResourceDescriptor* m_descriptors = nullptr;
    Core::u32 m_count = 0;
    const Core::u64* m_liveGeneration = nullptr;
    Core::u64 m_packetOwner = 0;
    Core::u64 m_frameGeneration = 0;
};

class FrameResourceSink {
public:
    virtual ~FrameResourceSink() noexcept = default;
    [[nodiscard]] virtual Core::Result<FrameResourceRef> intern(
        FrameResourceDescriptor descriptor,
        FramePin&& pin) noexcept = 0;
    [[nodiscard]] virtual Core::u32 resourceCount() const noexcept = 0;
};

} // namespace Tina::Render
