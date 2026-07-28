#pragma once

#include <tina/core/id/GenerationId.hpp>

namespace Tina::Render::Bgfx {

struct BgfxTextureResourceSlotTag final {};
struct BgfxMeshResourceSlotTag final {};

template <typename ResourceTag>
class BgfxResourceSlotGeneration final {
  public:
    [[nodiscard]] constexpr u32 value() const noexcept
    {
        return generation_;
    }

    [[nodiscard]] constexpr bool permanentlyRetired() const noexcept
    {
        return permanentlyRetired_;
    }

    [[nodiscard]] constexpr bool canReuse(bool live, bool gpuRetirementPending) const noexcept
    {
        return !live && !gpuRetirementPending && !permanentlyRetired_;
    }

    constexpr void advanceAfterRelease() noexcept
    {
        const auto next = Core::Detail::nextGeneration(generation_);
        if (!next.has_value())
        {
            permanentlyRetired_ = true;
            return;
        }
        generation_ = *next;
    }

    // Private-backend test seam for the otherwise unreachable 32-bit wrap boundary.
    [[nodiscard]] static constexpr BgfxResourceSlotGeneration
    fromGenerationForTesting(u32 generation) noexcept
    {
        BgfxResourceSlotGeneration result;
        result.generation_ = generation;
        return result;
    }

  private:
    u32 generation_ = 1;
    bool permanentlyRetired_ = false;
};

using BgfxTextureResourceSlotGeneration =
    BgfxResourceSlotGeneration<BgfxTextureResourceSlotTag>;
using BgfxMeshResourceSlotGeneration =
    BgfxResourceSlotGeneration<BgfxMeshResourceSlotTag>;

} // namespace Tina::Render::Bgfx
