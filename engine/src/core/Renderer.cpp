#include "Renderer.hpp"

#include "bgfx/platform.h"
#include "bx/math.h"
#include "bx/timer.h"
#include "stb/stb_image.h"

namespace Tina {
    inline uint32_t encodeNormalRgba8(float _x, float _y = 0.0f, float _z = 0.0f, float _w = 0.0f) {
        const float src[] =
        {
            _x * 0.5f + 0.5f,
            _y * 0.5f + 0.5f,
            _z * 0.5f + 0.5f,
            _w * 0.5f + 0.5f,
        };
        uint32_t dst;
        bx::packRgba8(&dst, src);
        return dst;
    }

    void calcTangents(void *_vertices, uint16_t _numVertices, bgfx::VertexLayout _layout, const uint16_t *_indices,
                      uint32_t _numIndices) {
        struct PosTexcoord {
            float m_x;
            float m_y;
            float m_z;
            float m_pad0;
            float m_u;
            float m_v;
            float m_pad1;
            float m_pad2;
        };

        float *tangents = new float[6 * _numVertices];
        bx::memSet(tangents, 0, 6 * _numVertices * sizeof(float));

        PosTexcoord v0;
        PosTexcoord v1;
        PosTexcoord v2;

        for (uint32_t ii = 0, num = _numIndices / 3; ii < num; ++ii) {
            const uint16_t *indices = &_indices[ii * 3];
            uint32_t i0 = indices[0];
            uint32_t i1 = indices[1];
            uint32_t i2 = indices[2];

            bgfx::vertexUnpack(&v0.m_x, bgfx::Attrib::Position, _layout, _vertices, i0);
            bgfx::vertexUnpack(&v0.m_u, bgfx::Attrib::TexCoord0, _layout, _vertices, i0);

            bgfx::vertexUnpack(&v1.m_x, bgfx::Attrib::Position, _layout, _vertices, i1);
            bgfx::vertexUnpack(&v1.m_u, bgfx::Attrib::TexCoord0, _layout, _vertices, i1);

            bgfx::vertexUnpack(&v2.m_x, bgfx::Attrib::Position, _layout, _vertices, i2);
            bgfx::vertexUnpack(&v2.m_u, bgfx::Attrib::TexCoord0, _layout, _vertices, i2);

            const float bax = v1.m_x - v0.m_x;
            const float bay = v1.m_y - v0.m_y;
            const float baz = v1.m_z - v0.m_z;
            const float bau = v1.m_u - v0.m_u;
            const float bav = v1.m_v - v0.m_v;

            const float cax = v2.m_x - v0.m_x;
            const float cay = v2.m_y - v0.m_y;
            const float caz = v2.m_z - v0.m_z;
            const float cau = v2.m_u - v0.m_u;
            const float cav = v2.m_v - v0.m_v;

            const float det = (bau * cav - bav * cau);
            const float invDet = 1.0f / det;

            const float tx = (bax * cav - cax * bav) * invDet;
            const float ty = (bay * cav - cay * bav) * invDet;
            const float tz = (baz * cav - caz * bav) * invDet;

            const float bx = (cax * bau - bax * cau) * invDet;
            const float by = (cay * bau - bay * cau) * invDet;
            const float bz = (caz * bau - baz * cau) * invDet;

            for (uint32_t jj = 0; jj < 3; ++jj) {
                float *tanu = &tangents[indices[jj] * 6];
                float *tanv = &tanu[3];
                tanu[0] += tx;
                tanu[1] += ty;
                tanu[2] += tz;

                tanv[0] += bx;
                tanv[1] += by;
                tanv[2] += bz;
            }
        }

        for (uint32_t ii = 0; ii < _numVertices; ++ii) {
            const bx::Vec3 tanu = bx::load<bx::Vec3>(&tangents[ii * 6]);
            const bx::Vec3 tanv = bx::load<bx::Vec3>(&tangents[ii * 6 + 3]);

            float nxyzw[4];
            bgfx::vertexUnpack(nxyzw, bgfx::Attrib::Normal, _layout, _vertices, ii);

            const bx::Vec3 normal = bx::load<bx::Vec3>(nxyzw);
            const float ndt = bx::dot(normal, tanu);
            const bx::Vec3 nxt = bx::cross(normal, tanu);
            const bx::Vec3 tmp = bx::sub(tanu, bx::mul(normal, ndt));

            float tangent[4];
            bx::store(tangent, bx::normalize(tmp));
            tangent[3] = bx::dot(nxt, tanv) < 0.0f ? -1.0f : 1.0f;

            bgfx::vertexPack(tangent, true, bgfx::Attrib::Tangent, _layout, _vertices, ii);
        }

        delete [] tangents;
    }

    Renderer::PosNormalTangentTexcoordVertex s_cubeVertices[24] = {
        {-1.0f, 1.0f, 1.0f, encodeNormalRgba8(0.0f, 0.0f, 1.0f), 0, 0, 0},
        {1.0f, 1.0f, 1.0f, encodeNormalRgba8(0.0f, 0.0f, 1.0f), 0, 0x7fff, 0},
        {-1.0f, -1.0f, 1.0f, encodeNormalRgba8(0.0f, 0.0f, 1.0f), 0, 0, 0x7fff},
        {1.0f, -1.0f, 1.0f, encodeNormalRgba8(0.0f, 0.0f, 1.0f), 0, 0x7fff, 0x7fff},
        {-1.0f, 1.0f, -1.0f, encodeNormalRgba8(0.0f, 0.0f, -1.0f), 0, 0, 0},
        {1.0f, 1.0f, -1.0f, encodeNormalRgba8(0.0f, 0.0f, -1.0f), 0, 0x7fff, 0},
        {-1.0f, -1.0f, -1.0f, encodeNormalRgba8(0.0f, 0.0f, -1.0f), 0, 0, 0x7fff},
        {1.0f, -1.0f, -1.0f, encodeNormalRgba8(0.0f, 0.0f, -1.0f), 0, 0x7fff, 0x7fff},
        {-1.0f, 1.0f, 1.0f, encodeNormalRgba8(0.0f, 1.0f, 0.0f), 0, 0, 0},
        {1.0f, 1.0f, 1.0f, encodeNormalRgba8(0.0f, 1.0f, 0.0f), 0, 0x7fff, 0},
        {-1.0f, 1.0f, -1.0f, encodeNormalRgba8(0.0f, 1.0f, 0.0f), 0, 0, 0x7fff},
        {1.0f, 1.0f, -1.0f, encodeNormalRgba8(0.0f, 1.0f, 0.0f), 0, 0x7fff, 0x7fff},
        {-1.0f, -1.0f, 1.0f, encodeNormalRgba8(0.0f, -1.0f, 0.0f), 0, 0, 0},
        {1.0f, -1.0f, 1.0f, encodeNormalRgba8(0.0f, -1.0f, 0.0f), 0, 0x7fff, 0},
        {-1.0f, -1.0f, -1.0f, encodeNormalRgba8(0.0f, -1.0f, 0.0f), 0, 0, 0x7fff},
        {1.0f, -1.0f, -1.0f, encodeNormalRgba8(0.0f, -1.0f, 0.0f), 0, 0x7fff, 0x7fff},
        {1.0f, -1.0f, 1.0f, encodeNormalRgba8(1.0f, 0.0f, 0.0f), 0, 0, 0},
        {1.0f, 1.0f, 1.0f, encodeNormalRgba8(1.0f, 0.0f, 0.0f), 0, 0x7fff, 0},
        {1.0f, -1.0f, -1.0f, encodeNormalRgba8(1.0f, 0.0f, 0.0f), 0, 0, 0x7fff},
        {1.0f, 1.0f, -1.0f, encodeNormalRgba8(1.0f, 0.0f, 0.0f), 0, 0x7fff, 0x7fff},
        {-1.0f, -1.0f, 1.0f, encodeNormalRgba8(-1.0f, 0.0f, 0.0f), 0, 0, 0},
        {-1.0f, 1.0f, 1.0f, encodeNormalRgba8(-1.0f, 0.0f, 0.0f), 0, 0x7fff, 0},
        {-1.0f, -1.0f, -1.0f, encodeNormalRgba8(-1.0f, 0.0f, 0.0f), 0, 0, 0x7fff},
        {-1.0f, 1.0f, -1.0f, encodeNormalRgba8(-1.0f, 0.0f, 0.0f), 0, 0x7fff, 0x7fff},
    };

    static const uint16_t s_cubeIndices[36] =
    {
        0, 2, 1,
        1, 2, 3,
        4, 5, 6,
        5, 7, 6,

        8, 10, 9,
        9, 10, 11,
        12, 13, 14,
        13, 15, 14,

        16, 18, 17,
        17, 18, 19,
        20, 21, 22,
        21, 23, 22,
    };

    Renderer::Renderer(Vector2i size, int viewId): m_numLights(0), m_instancingSupported(false), m_resolution(size),
                                                   m_timeOffset(0.0f) {
        bgfx::setViewClear(0,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);

        const bgfx::Caps *caps = bgfx::getCaps();
        m_instancingSupported = false;

        m_vbh.getLayout().begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Normal, 4, bgfx::AttribType::Uint8, true, true)
                .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Uint8, true, true)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Int16, true, true)
                .end();

        calcTangents(s_cubeVertices
                     , BX_COUNTOF(s_cubeVertices)
                     , m_vbh.getLayout()
                     , s_cubeIndices
                     , BX_COUNTOF(s_cubeIndices)
        );

        m_vbh.init(s_cubeVertices, sizeof(s_cubeVertices));
        m_ibh.init(s_cubeIndices, sizeof(s_cubeIndices));

        s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        s_texNormal = bgfx::createUniform("s_texNormal", bgfx::UniformType::Sampler);

        m_numLights = 4;

        m_shader.loadShader("bump");

        m_textureColor = BgfxUtils::loadTexture("../resources/textures/grassland.png");

        m_textureNormal = BgfxUtils::loadTexture("../resources/textures/grassland.png");

        m_timeOffset = bx::getHPCounter();
    }

    void Renderer::render() {
        bgfx::setViewRect(0, 0, 0, m_resolution.width, m_resolution.height);
        bgfx::touch(0);

        float time = static_cast<float>((bx::getHPCounter() - m_timeOffset) / static_cast<double>(bx::getHPCounter()));

        const bx::Vec3 at = {0.0f, 0.0f, 0.0f};
        const bx::Vec3 eye = {0.0f, 0.0f, -7.0f}; {
            float view[16];
            bx::mtxLookAt(view, eye, at);
            float proj[16];
            bx::mtxProj(proj, 60.0f, float(m_resolution.width) / float(m_resolution.height), 0.1f, 100.0f,
                        bgfx::getCaps()->homogeneousDepth);
            bgfx::setViewTransform(0, view, proj);
            bgfx::setViewRect(0, 0, 0, m_resolution.width, m_resolution.height);
        }
        for (uint32_t yy = 0; yy < 3; ++yy) {
            for (uint32_t xx = 0; xx < 3; ++xx) {
                float mtx[16];
                bx::mtxRotateXY(mtx, time * 0.023f + xx * 0.21f, time * 0.03f + yy * 0.37f);
                mtx[12] = -3.0f + float(xx) * 3.0f;
                mtx[13] = -3.0f + float(yy) * 3.0f;
                mtx[14] = 0.0f;

                // Set transform for draw call.
                bgfx::setTransform(mtx);

                // Set vertex and index buffer.
                m_vbh.enable();
                m_ibh.enable();

                // Bind textures.
                bgfx::setTexture(0, s_texColor, m_textureColor);
                bgfx::setTexture(1, s_texNormal, m_textureNormal);

                // Set render states.
                bgfx::setState(0
                               | BGFX_STATE_WRITE_RGB
                               | BGFX_STATE_WRITE_A
                               | BGFX_STATE_WRITE_Z
                               | BGFX_STATE_DEPTH_TEST_LESS
                               | BGFX_STATE_MSAA
                );

                // Submit primitive for rendering to view 0.
                bgfx::submit(0, m_shader.getProgram());
            }
        }


        // Advance to next frame. Rendering thread will be kicked to
        // process submitted rendering primitives.
        bgfx::frame();
    }

    void Renderer::shutdown() {
        m_vbh.free();
        m_ibh.free();
        bgfx::destroy(m_textureColor);
        bgfx::destroy(m_textureNormal);
        bgfx::destroy(s_texColor);
        bgfx::destroy(s_texNormal);
        bgfx::shutdown();
    }
}
