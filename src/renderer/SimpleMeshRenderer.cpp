#include "SimpleMeshRenderer.hpp"

#include "ShaderCatalog.hpp"
#include "ShaderManager.hpp"

#include <bx/math.h>

#include <array>

namespace Tina::Renderer {
namespace {

struct MeshVertex {
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
    float a;
};

constexpr std::array<MeshVertex, 8> kCubeVertices{{
    {-1.0f, -1.0f, -1.0f, 0.95f, 0.25f, 0.25f, 1.0f},
    { 1.0f, -1.0f, -1.0f, 0.25f, 0.95f, 0.35f, 1.0f},
    { 1.0f,  1.0f, -1.0f, 0.25f, 0.45f, 0.95f, 1.0f},
    {-1.0f,  1.0f, -1.0f, 0.95f, 0.85f, 0.25f, 1.0f},
    {-1.0f, -1.0f,  1.0f, 0.75f, 0.25f, 0.95f, 1.0f},
    { 1.0f, -1.0f,  1.0f, 0.25f, 0.90f, 0.90f, 1.0f},
    { 1.0f,  1.0f,  1.0f, 0.95f, 0.55f, 0.20f, 1.0f},
    {-1.0f,  1.0f,  1.0f, 0.90f, 0.90f, 0.90f, 1.0f},
}};

constexpr std::array<uint16_t, 36> kCubeIndices{{
    0, 2, 1, 0, 3, 2,
    4, 5, 6, 4, 6, 7,
    0, 1, 5, 0, 5, 4,
    3, 7, 6, 3, 6, 2,
    0, 4, 7, 0, 7, 3,
    1, 2, 6, 1, 6, 5,
}};

} // namespace

SimpleMeshRenderer::~SimpleMeshRenderer()
{
    shutdown();
}

bool SimpleMeshRenderer::initialize(ShaderManager& shaders)
{
    shutdown();

    m_program = ShaderCatalog::Load(shaders, ShaderCatalog::Tag::WorldSolid);
    if (!bgfx::isValid(m_program)) {
        return false;
    }

    m_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
        .end();

    m_vertexBuffer = bgfx::createVertexBuffer(
        bgfx::copy(kCubeVertices.data(), static_cast<uint32_t>(sizeof(kCubeVertices))),
        m_layout);
    m_indexBuffer = bgfx::createIndexBuffer(
        bgfx::copy(kCubeIndices.data(), static_cast<uint32_t>(sizeof(kCubeIndices))));

    if (!isInitialized()) {
        shutdown();
        return false;
    }
    return true;
}

void SimpleMeshRenderer::shutdown()
{
    if (bgfx::isValid(m_indexBuffer)) {
        bgfx::destroy(m_indexBuffer);
        m_indexBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_vertexBuffer)) {
        bgfx::destroy(m_vertexBuffer);
        m_vertexBuffer = BGFX_INVALID_HANDLE;
    }
    // ShaderManager owns and destroys the shared program.
    m_program = BGFX_INVALID_HANDLE;
}

void SimpleMeshRenderer::drawCube(uint16_t viewId, float rotationRadians) const
{
    if (!isInitialized()) {
        return;
    }

    float transform[16];
    bx::mtxRotateXY(transform, rotationRadians * 0.65f, rotationRadians);
    bgfx::setTransform(transform);
    bgfx::setVertexBuffer(0, m_vertexBuffer);
    bgfx::setIndexBuffer(m_indexBuffer);
    bgfx::setState(BGFX_STATE_WRITE_RGB |
                   BGFX_STATE_WRITE_A |
                   BGFX_STATE_WRITE_Z |
                   BGFX_STATE_DEPTH_TEST_LESS |
                   BGFX_STATE_MSAA);
    bgfx::submit(viewId, m_program);
}

bool SimpleMeshRenderer::isInitialized() const noexcept
{
    return bgfx::isValid(m_program) &&
           bgfx::isValid(m_vertexBuffer) &&
           bgfx::isValid(m_indexBuffer);
}

} // namespace Tina::Renderer
