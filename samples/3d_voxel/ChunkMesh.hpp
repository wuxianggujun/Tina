#pragma once

// Chunk -> StaticMesh conversion with visible-face culling.
//
// A face is emitted only where the adjacent block is air, so a fully enclosed
// chunk costs nothing. Winding, tangents and UV origin all follow the engine's
// canonical cube in src/render/bgfx/BgfxOpaque3DGeometry.cpp: corners are wound
// counter-clockwise as seen from outside (BGFX_STATE_CULL_CW culls the other
// direction), tangent.w is -1, and v=0 is the top of the texture.

#include "VoxelWorld.hpp"

#include <tina/core/base/Types.hpp>

#include <array>
#include <cmath>
#include <vector>

namespace VoxelSample {

using Tina::u16;

// Interleaved P3_N3_T4_UV2, matching StaticMeshUploadDesc.
constexpr u32 FloatsPerVertex = 12;

// The atlas is one row of 16x16 tiles; only the u range differs per tile.
constexpr u32 AtlasTileCount = 4;
constexpr u16 AtlasTileSize = 16;
constexpr u16 AtlasWidth = AtlasTileSize * static_cast<u16>(AtlasTileCount);
constexpr u16 AtlasHeight = AtlasTileSize;
constexpr float AtlasTileWidth = 1.0F / static_cast<float>(AtlasTileCount);

// A tile's u range is inset by half a texel. Without it the right edge of tile N
// lands exactly on the first texel of tile N+1, and Point sampling there returns
// the neighbouring block's colour along every face seam.
constexpr float AtlasHalfTexelU = 0.5F / static_cast<float>(AtlasWidth);

struct ChunkMeshData final {
    std::vector<float> vertices{};
    std::vector<u16> indices{};

    [[nodiscard]] bool empty() const noexcept { return indices.empty(); }
    [[nodiscard]] u32 vertexCount() const noexcept
    {
        return static_cast<u32>(vertices.size() / FloatsPerVertex);
    }
    [[nodiscard]] u32 indexCount() const noexcept { return static_cast<u32>(indices.size()); }
};

struct FaceDesc final {
    i32 neighborX, neighborY, neighborZ;
    float normalX, normalY, normalZ;
    float tangentX, tangentY, tangentZ, tangentW;
    std::array<std::array<float, 3>, 4> corners;
};

// Order: +X, -X, +Y, -Y, +Z, -Z.
constexpr std::array<FaceDesc, 6> Faces{{
    {1, 0, 0, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, -1.0F,
     {{{1.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}}},
    {-1, 0, 0, -1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, -1.0F,
     {{{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 0.0F}}}},
    {0, 1, 0, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F,
     {{{0.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}}},
    {0, -1, 0, 0.0F, -1.0F, 0.0F, 1.0F, 0.0F, 0.0F, -1.0F,
     {{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}}},
    {0, 0, 1, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F, -1.0F,
     {{{0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 1.0F}}}},
    {0, 0, -1, 0.0F, 0.0F, -1.0F, -1.0F, 0.0F, 0.0F, -1.0F,
     {{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}}}},
}};

// Corner UVs are the same for every face. v=0 is the top of the tile.
constexpr std::array<std::array<float, 2>, 4> CornerUv{{
    {0.0F, 1.0F},
    {1.0F, 1.0F},
    {1.0F, 0.0F},
    {0.0F, 0.0F},
}};

// A grass block shows grass on top and dirt everywhere else, which is the whole
// reason face index reaches this function.
[[nodiscard]] constexpr u32 atlasTileFor(BlockType block, usize faceIndex) noexcept
{
    switch (block)
    {
        case BlockType::Grass:
            return faceIndex == 2 ? 0U : 1U;
        case BlockType::Dirt:
            return 1U;
        case BlockType::Stone:
            return 2U;
        case BlockType::Planks:
            return 3U;
        case BlockType::Air:
            break;
    }
    return 1U;
}

// Worst case is a checkerboard: half the blocks solid, each with all six faces
// exposed and no two solid blocks adjacent. Indices are u16, so this must fit.
// Raising ChunkSize past 16 breaks it, which is why the bound is checked here.
static_assert(3 * ChunkVolume * 4 <= 65535,
              "a chunk mesh must stay addressable by u16 indices");

// Vertices are chunk-local ([0, ChunkSize] on each axis); the draw places the
// chunk with a translation, so every chunk mesh is position-independent.
[[nodiscard]] inline ChunkMeshData generateChunkMesh(const VoxelWorld& world, i32 chunkX,
                                                     i32 chunkY, i32 chunkZ)
{
    ChunkMeshData mesh;
    const ChunkData& chunk = world.chunkAt(chunkX, chunkY, chunkZ);
    if (chunk.empty())
    {
        return mesh;
    }

    const i32 baseX = chunkX * ChunkSize;
    const i32 baseY = chunkY * ChunkSize;
    const i32 baseZ = chunkZ * ChunkSize;

    for (i32 localZ = 0; localZ < ChunkSize; ++localZ)
    {
        for (i32 localY = 0; localY < ChunkSize; ++localY)
        {
            for (i32 localX = 0; localX < ChunkSize; ++localX)
            {
                const BlockType block = chunk.get(localX, localY, localZ);
                if (block == BlockType::Air)
                {
                    continue;
                }
                const i32 worldX = baseX + localX;
                const i32 worldY = baseY + localY;
                const i32 worldZ = baseZ + localZ;

                for (usize faceIndex = 0; faceIndex < Faces.size(); ++faceIndex)
                {
                    const FaceDesc& face = Faces[faceIndex];
                    if (world.solidAt(worldX + face.neighborX, worldY + face.neighborY,
                                      worldZ + face.neighborZ))
                    {
                        continue;
                    }

                    const float tileU0 =
                        static_cast<float>(atlasTileFor(block, faceIndex)) * AtlasTileWidth +
                        AtlasHalfTexelU;
                    const float tileSpan = AtlasTileWidth - 2.0F * AtlasHalfTexelU;
                    const auto baseVertex = static_cast<u16>(mesh.vertexCount());
                    for (usize corner = 0; corner < 4; ++corner)
                    {
                        mesh.vertices.push_back(static_cast<float>(localX) +
                                                face.corners[corner][0]);
                        mesh.vertices.push_back(static_cast<float>(localY) +
                                                face.corners[corner][1]);
                        mesh.vertices.push_back(static_cast<float>(localZ) +
                                                face.corners[corner][2]);
                        mesh.vertices.push_back(face.normalX);
                        mesh.vertices.push_back(face.normalY);
                        mesh.vertices.push_back(face.normalZ);
                        mesh.vertices.push_back(face.tangentX);
                        mesh.vertices.push_back(face.tangentY);
                        mesh.vertices.push_back(face.tangentZ);
                        mesh.vertices.push_back(face.tangentW);
                        mesh.vertices.push_back(tileU0 + CornerUv[corner][0] * tileSpan);
                        mesh.vertices.push_back(CornerUv[corner][1]);
                    }
                    mesh.indices.push_back(baseVertex);
                    mesh.indices.push_back(static_cast<u16>(baseVertex + 1U));
                    mesh.indices.push_back(static_cast<u16>(baseVertex + 2U));
                    mesh.indices.push_back(baseVertex);
                    mesh.indices.push_back(static_cast<u16>(baseVertex + 2U));
                    mesh.indices.push_back(static_cast<u16>(baseVertex + 3U));
                }
            }
        }
    }
    return mesh;
}

// Standalone unit cube spanning [0,1]^3, used for the selection box around the
// block under the crosshair. Every face is emitted: it has no neighbours to occlude
// it, and it is drawn slightly inflated around the target block.
[[nodiscard]] inline ChunkMeshData makeUnitCubeMesh()
{
    ChunkMeshData mesh;
    for (const FaceDesc& face : Faces)
    {
        const auto baseVertex = static_cast<u16>(mesh.vertexCount());
        for (usize corner = 0; corner < 4; ++corner)
        {
            mesh.vertices.push_back(face.corners[corner][0]);
            mesh.vertices.push_back(face.corners[corner][1]);
            mesh.vertices.push_back(face.corners[corner][2]);
            mesh.vertices.push_back(face.normalX);
            mesh.vertices.push_back(face.normalY);
            mesh.vertices.push_back(face.normalZ);
            mesh.vertices.push_back(face.tangentX);
            mesh.vertices.push_back(face.tangentY);
            mesh.vertices.push_back(face.tangentZ);
            mesh.vertices.push_back(face.tangentW);
            mesh.vertices.push_back(CornerUv[corner][0]);
            mesh.vertices.push_back(CornerUv[corner][1]);
        }
        mesh.indices.push_back(baseVertex);
        mesh.indices.push_back(static_cast<u16>(baseVertex + 1U));
        mesh.indices.push_back(static_cast<u16>(baseVertex + 2U));
        mesh.indices.push_back(baseVertex);
        mesh.indices.push_back(static_cast<u16>(baseVertex + 2U));
        mesh.indices.push_back(static_cast<u16>(baseVertex + 3U));
    }
    return mesh;
}

// Unit-radius UV sphere centred on the origin, used for the sun.
//
// A sphere rather than a camera-facing quad: the sun is emissive, so it shades to a flat
// bright circle from every direction anyway, and a sphere needs no billboard orientation
// and cannot turn edge-on and vanish as the sun crosses the sky.
//
// Tangents are the d/du direction of the parameterisation, which degenerates at the poles.
// That is harmless here because emissive ignores the TBN entirely; a lit material on this
// mesh would want a proper tangent frame.
[[nodiscard]] inline ChunkMeshData makeUnitSphereMesh(u32 meridians, u32 parallels)
{
    ChunkMeshData mesh;
    if (meridians < 3 || parallels < 2)
    {
        return mesh;
    }
    constexpr float pi = 3.14159265358979323846F;

    // Rings are duplicated along the seam (u = 0 and u = 1 are separate vertices) so the
    // UV wrap does not fold the last quad back onto the first.
    for (u32 ring = 0; ring <= parallels; ++ring)
    {
        const float v = static_cast<float>(ring) / static_cast<float>(parallels);
        const float polar = v * pi;
        const float sinPolar = std::sin(polar);
        const float cosPolar = std::cos(polar);
        for (u32 segment = 0; segment <= meridians; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(meridians);
            const float azimuth = u * 2.0F * pi;
            const float sinAzimuth = std::sin(azimuth);
            const float cosAzimuth = std::cos(azimuth);

            const float nx = sinPolar * cosAzimuth;
            const float ny = cosPolar;
            const float nz = sinPolar * sinAzimuth;

            mesh.vertices.push_back(nx);
            mesh.vertices.push_back(ny);
            mesh.vertices.push_back(nz);
            mesh.vertices.push_back(nx);
            mesh.vertices.push_back(ny);
            mesh.vertices.push_back(nz);
            mesh.vertices.push_back(-sinAzimuth);
            mesh.vertices.push_back(0.0F);
            mesh.vertices.push_back(cosAzimuth);
            mesh.vertices.push_back(-1.0F);
            mesh.vertices.push_back(u);
            mesh.vertices.push_back(v);
        }
    }

    // Wound counter-clockwise seen from outside, matching the cube above: the pipeline
    // culls the other direction, so the reverse order would make the sphere invisible.
    const u32 stride = meridians + 1U;
    for (u32 ring = 0; ring < parallels; ++ring)
    {
        for (u32 segment = 0; segment < meridians; ++segment)
        {
            const auto topLeft = static_cast<u16>(ring * stride + segment);
            const auto topRight = static_cast<u16>(topLeft + 1U);
            const auto bottomLeft = static_cast<u16>(topLeft + stride);
            const auto bottomRight = static_cast<u16>(bottomLeft + 1U);

            mesh.indices.push_back(topLeft);
            mesh.indices.push_back(bottomLeft);
            mesh.indices.push_back(topRight);
            mesh.indices.push_back(topRight);
            mesh.indices.push_back(bottomLeft);
            mesh.indices.push_back(bottomRight);
        }
    }
    return mesh;
}

} // namespace VoxelSample
