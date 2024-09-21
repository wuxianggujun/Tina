#include "VertexBuffer.hpp"

#include<bx/bx.h>
#include <cassert>

namespace Tina {
    VertexBuffer::VertexBuffer() = default;

    VertexBuffer::VertexBuffer(VertexBuffer &&other) noexcept {
        if (isValid()) free();

        m_staticHandle.idx = other.m_staticHandle.idx;
        other.m_staticHandle.idx = bgfx::kInvalidHandle;

        m_dynamicHandle.idx = other.m_dynamicHandle.idx;
        other.m_dynamicHandle.idx = bgfx::kInvalidHandle;

        m_layout = bx::move(other.m_layout);
        m_isDynamic = other.m_isDynamic;

        m_size = other.m_size;
        other.m_size = 0;
    }

    VertexBuffer &VertexBuffer::operator=(VertexBuffer &&other) noexcept {
        if (isValid()) free();

        m_staticHandle.idx = other.m_staticHandle.idx;
        other.m_staticHandle.idx = bgfx::kInvalidHandle;

        m_dynamicHandle.idx = other.m_dynamicHandle.idx;
        other.m_dynamicHandle.idx = bgfx::kInvalidHandle;

        m_layout = bx::move(other.m_layout);
        m_isDynamic = other.m_isDynamic;

        m_size = other.m_size;
        other.m_size = 0;
        return *this;
    }

    VertexBuffer::~VertexBuffer() {
        free();
    }

    void VertexBuffer::init(const void *data, uint32_t size, bool isDynamic) {
        assert(size != 0);
        assert(m_layout.m_stride != 0);

        auto createBuffer = [this](const void *data, uint32_t size, bool isDynamic) {
            m_size = size;
            m_isDynamic = (data == nullptr || isDynamic);

            if (m_isDynamic) {
                auto *mem = (data ? bgfx::copy(data, size) : bgfx::alloc(size));
                m_dynamicHandle = bgfx::createDynamicVertexBuffer(mem, m_layout);
                assert(bgfx::isValid(m_dynamicHandle));
            } else {
                auto *mem = bgfx::copy(data, size);
                m_staticHandle = bgfx::createVertexBuffer(mem, m_layout);
                assert(bgfx::isValid(m_staticHandle));
            }
        };

        if (isValid()) {
            // Reallocate a new buffer if size increased
            if (size > m_size) {
                free();
                createBuffer(data, size, isDynamic);
            } else if (data != nullptr) {
                update(data, size);
            }
        } else {
            createBuffer(data, size, isDynamic);
        }
    }

    void VertexBuffer::update(const void *data, uint32_t size, uint32_t offset) const {
        assert(data);
        assert(size != 0);
        assert(offset + size <= m_size);
        assert(m_isDynamic);
        assert(bgfx::isValid(m_dynamicHandle));

        bgfx::update(m_dynamicHandle, offset, bgfx::copy(data, size));
    }

    void VertexBuffer::free() {
        if (!m_isDynamic && bgfx::isValid(m_staticHandle)) {
            bgfx::destroy(m_staticHandle);
            m_staticHandle.idx = bgfx::kInvalidHandle;
        } else if (m_isDynamic && bgfx::isValid(m_dynamicHandle)) {
            bgfx::destroy(m_dynamicHandle);
            m_dynamicHandle.idx = bgfx::kInvalidHandle;
        }
    }

    void VertexBuffer::enable() {
        if (!m_isDynamic) {
            assert(bgfx::isValid(m_staticHandle));
            bgfx::setVertexBuffer(0, m_staticHandle);
        } else {
            assert(bgfx::isValid(m_dynamicHandle));
            bgfx::setVertexBuffer(0, m_dynamicHandle);
        }
    }

    void VertexBuffer::enable(uint32_t startVertex, uint32_t numVertices) const {
        if (!m_isDynamic) {
            assert(bgfx::isValid(m_staticHandle));
            bgfx::setVertexBuffer(0, m_staticHandle,startVertex,numVertices);
        } else {
            assert(bgfx::isValid(m_dynamicHandle));
            bgfx::setVertexBuffer(0, m_dynamicHandle,startVertex,numVertices);
        }
    }

    bool VertexBuffer::isValid() const {
        return bgfx::isValid(m_staticHandle) || bgfx::isValid(m_dynamicHandle);
    }

    void VertexBuffer::setUpDefaultLayout() {
        m_layout.begin()
                .add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float, false)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float, false)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float, false)
                .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float, false)
                .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float, false) // lightValue
                .add(bgfx::Attrib::TexCoord2, 1, bgfx::AttribType::Float, false)
                .end();
    }
}
