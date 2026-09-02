#pragma once

// Fixed-extent voxel world: a dense block grid split into 16^3 chunks.
//
// 16^3 is not a taste choice. StaticMeshUploadDesc indexes with u16, so one chunk
// mesh cannot exceed 65535 vertices. The worst case for a 16^3 chunk is a
// checkerboard: 2048 solid blocks x 6 exposed faces x 4 verts = 49152, which fits.
// A 16x64x16 chunk would reach 196608 and overflow, so height is split into chunks
// too rather than kept as one tall column.

#include <tina/core/base/Types.hpp>

#include <cmath>
#include <vector>

namespace VoxelSample {

using Tina::i32;
using Tina::u32;
using Tina::u8;
using Tina::usize;

// 0 is air and is never meshed. The rest index the block atlas tile table.
enum class BlockType : u8 {
    Air = 0,
    Grass = 1,
    Dirt = 2,
    Stone = 3,
    Planks = 4,
};

constexpr i32 ChunkSize = 16;
constexpr i32 ChunkVolume = ChunkSize * ChunkSize * ChunkSize;

constexpr i32 WorldChunksX = 8;
constexpr i32 WorldChunksY = 4;
constexpr i32 WorldChunksZ = 8;
constexpr i32 WorldChunkCount = WorldChunksX * WorldChunksY * WorldChunksZ;

constexpr i32 WorldBlocksX = WorldChunksX * ChunkSize; // 128
constexpr i32 WorldBlocksY = WorldChunksY * ChunkSize; // 64
constexpr i32 WorldBlocksZ = WorldChunksZ * ChunkSize; // 128

// Deterministic value noise. Hash-based rather than table-based so the world is
// reproducible from a seed without shipping permutation data.
[[nodiscard]] constexpr u32 hashCoord(i32 x, i32 z, u32 seed) noexcept
{
    u32 value = static_cast<u32>(x) * 0x1F1F1F1FU;
    value ^= static_cast<u32>(z) * 0x85EBCA6BU;
    value ^= seed * 0xC2B2AE35U;
    value ^= value >> 15U;
    value *= 0x2545F491U;
    value ^= value >> 13U;
    return value;
}

// Unit-range noise at integer lattice points.
[[nodiscard]] constexpr float latticeNoise(i32 x, i32 z, u32 seed) noexcept
{
    return static_cast<float>(hashCoord(x, z, seed) >> 8U) / static_cast<float>(1U << 24U);
}

// Smooth (bilinear, smoothstep-weighted) noise sampled at block resolution.
[[nodiscard]] inline float smoothNoise(float x, float z, u32 seed) noexcept
{
    const float floorX = std::floor(x);
    const float floorZ = std::floor(z);
    const i32 latticeX = static_cast<i32>(floorX);
    const i32 latticeZ = static_cast<i32>(floorZ);
    const float fractionX = x - floorX;
    const float fractionZ = z - floorZ;
    const float weightX = fractionX * fractionX * (3.0F - 2.0F * fractionX);
    const float weightZ = fractionZ * fractionZ * (3.0F - 2.0F * fractionZ);

    const float corner00 = latticeNoise(latticeX, latticeZ, seed);
    const float corner10 = latticeNoise(latticeX + 1, latticeZ, seed);
    const float corner01 = latticeNoise(latticeX, latticeZ + 1, seed);
    const float corner11 = latticeNoise(latticeX + 1, latticeZ + 1, seed);

    const float edge0 = corner00 + (corner10 - corner00) * weightX;
    const float edge1 = corner01 + (corner11 - corner01) * weightX;
    return edge0 + (edge1 - edge0) * weightZ;
}

// Surface height for a world column, in blocks. Two octaves are enough to give
// walkable hills and dig-worthy overhangs without a full noise library.
[[nodiscard]] inline i32 terrainHeight(i32 worldX, i32 worldZ, u32 seed) noexcept
{
    constexpr float BaseHeight = 26.0F;
    const float coarse = smoothNoise(static_cast<float>(worldX) / 34.0F,
                                     static_cast<float>(worldZ) / 34.0F, seed);
    const float fine = smoothNoise(static_cast<float>(worldX) / 11.0F,
                                   static_cast<float>(worldZ) / 11.0F, seed ^ 0x9E3779B9U);
    const float height = BaseHeight + coarse * 14.0F + fine * 4.0F;
    const i32 clamped = static_cast<i32>(height);
    if (clamped < 1)
    {
        return 1;
    }
    if (clamped > WorldBlocksY - 2)
    {
        return WorldBlocksY - 2;
    }
    return clamped;
}

class ChunkData final {
  public:
    [[nodiscard]] BlockType get(i32 x, i32 y, i32 z) const noexcept
    {
        return blocks_[index(x, y, z)];
    }

    void set(i32 x, i32 y, i32 z, BlockType type) noexcept
    {
        BlockType& slot = blocks_[index(x, y, z)];
        if (slot == type)
        {
            return;
        }
        if (slot == BlockType::Air)
        {
            ++solidCount_;
        }
        else if (type == BlockType::Air)
        {
            --solidCount_;
        }
        slot = type;
        dirty_ = true;
    }

    // A chunk with no solid block can never emit a face, so meshing it is pure
    // waste. Most of this world is air above the surface or fully enclosed below.
    [[nodiscard]] bool empty() const noexcept { return solidCount_ == 0; }
    [[nodiscard]] u32 solidCount() const noexcept { return solidCount_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void markDirty() noexcept { dirty_ = true; }
    void clearDirty() noexcept { dirty_ = false; }

  private:
    // Callers are the world and the mesher, both of which clamp to chunk extents
    // before calling, so this is an internal index rather than a bounds check.
    [[nodiscard]] static constexpr usize index(i32 x, i32 y, i32 z) noexcept
    {
        return static_cast<usize>(x + y * ChunkSize + z * ChunkSize * ChunkSize);
    }

    std::vector<BlockType> blocks_ = std::vector<BlockType>(static_cast<usize>(ChunkVolume),
                                                           BlockType::Air);
    u32 solidCount_ = 0;
    bool dirty_ = true;
};

class VoxelWorld final {
  public:
    explicit VoxelWorld(u32 seed) : chunks_(static_cast<usize>(WorldChunkCount))
    {
        for (i32 worldZ = 0; worldZ < WorldBlocksZ; ++worldZ)
        {
            for (i32 worldX = 0; worldX < WorldBlocksX; ++worldX)
            {
                const i32 height = terrainHeight(worldX, worldZ, seed);
                for (i32 worldY = 0; worldY <= height; ++worldY)
                {
                    BlockType type = BlockType::Stone;
                    if (worldY == height)
                    {
                        type = BlockType::Grass;
                    }
                    else if (worldY > height - 4)
                    {
                        type = BlockType::Dirt;
                    }
                    setBlock(worldX, worldY, worldZ, type);
                }
            }
        }
    }

    [[nodiscard]] static constexpr bool inBounds(i32 worldX, i32 worldY, i32 worldZ) noexcept
    {
        return worldX >= 0 && worldX < WorldBlocksX && worldY >= 0 && worldY < WorldBlocksY &&
               worldZ >= 0 && worldZ < WorldBlocksZ;
    }

    // Out of bounds reads as Air, which makes the world's outer shell emit faces
    // and keeps the mesher free of special cases at the border.
    [[nodiscard]] BlockType getBlock(i32 worldX, i32 worldY, i32 worldZ) const noexcept
    {
        if (!inBounds(worldX, worldY, worldZ))
        {
            return BlockType::Air;
        }
        return chunkAt(worldX >> 4, worldY >> 4, worldZ >> 4)
            .get(worldX & 15, worldY & 15, worldZ & 15);
    }

    [[nodiscard]] bool solidAt(i32 worldX, i32 worldY, i32 worldZ) const noexcept
    {
        return getBlock(worldX, worldY, worldZ) != BlockType::Air;
    }

    void setBlock(i32 worldX, i32 worldY, i32 worldZ, BlockType type) noexcept
    {
        if (!inBounds(worldX, worldY, worldZ))
        {
            return;
        }
        const i32 chunkX = worldX >> 4;
        const i32 chunkY = worldY >> 4;
        const i32 chunkZ = worldZ >> 4;
        const i32 localX = worldX & 15;
        const i32 localY = worldY & 15;
        const i32 localZ = worldZ & 15;
        chunkAt(chunkX, chunkY, chunkZ).set(localX, localY, localZ, type);

        // A block on a chunk face changes the neighbour's occlusion, so the
        // neighbour's mesh is stale even though none of its own blocks moved.
        if (localX == 0)
        {
            markChunkDirty(chunkX - 1, chunkY, chunkZ);
        }
        if (localX == ChunkSize - 1)
        {
            markChunkDirty(chunkX + 1, chunkY, chunkZ);
        }
        if (localY == 0)
        {
            markChunkDirty(chunkX, chunkY - 1, chunkZ);
        }
        if (localY == ChunkSize - 1)
        {
            markChunkDirty(chunkX, chunkY + 1, chunkZ);
        }
        if (localZ == 0)
        {
            markChunkDirty(chunkX, chunkY, chunkZ - 1);
        }
        if (localZ == ChunkSize - 1)
        {
            markChunkDirty(chunkX, chunkY, chunkZ + 1);
        }
    }

    [[nodiscard]] ChunkData& chunkAt(i32 chunkX, i32 chunkY, i32 chunkZ) noexcept
    {
        return chunks_[chunkIndex(chunkX, chunkY, chunkZ)];
    }

    [[nodiscard]] const ChunkData& chunkAt(i32 chunkX, i32 chunkY, i32 chunkZ) const noexcept
    {
        return chunks_[chunkIndex(chunkX, chunkY, chunkZ)];
    }

    [[nodiscard]] static constexpr usize chunkIndex(i32 chunkX, i32 chunkY, i32 chunkZ) noexcept
    {
        return static_cast<usize>(chunkX + chunkY * WorldChunksX +
                                  chunkZ * WorldChunksX * WorldChunksY);
    }

  private:
    void markChunkDirty(i32 chunkX, i32 chunkY, i32 chunkZ) noexcept
    {
        if (chunkX < 0 || chunkX >= WorldChunksX || chunkY < 0 || chunkY >= WorldChunksY ||
            chunkZ < 0 || chunkZ >= WorldChunksZ)
        {
            return;
        }
        chunkAt(chunkX, chunkY, chunkZ).markDirty();
    }

    std::vector<ChunkData> chunks_;
};

} // namespace VoxelSample
