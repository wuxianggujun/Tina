#include "IndexBuffer.hpp"

#include <bx/bx.h>
#include <cassert>

namespace Tina {
    IndexBuffer::IndexBuffer() = default;

    IndexBuffer::IndexBuffer(IndexBuffer &&other) noexcept {
        if (isValid()) free();

        m_staticHandle.idx = other.m_staticHandle.idx;
        other.m_staticHandle.idx = bgfx::kInvalidHandle;

        m_dynamicHandle.idx = other.m_dynamicHandle.idx;
        other.m_dynamicHandle.idx = bgfx::kInvalidHandle;

        m_isDynamic = other.m_isDynamic;
    }

    IndexBuffer &IndexBuffer::operator=(IndexBuffer &&other) noexcept {
        if (isValid()) free();

        m_staticHandle.idx = other.m_staticHandle.idx;
        other.m_staticHandle.idx = bgfx::kInvalidHandle;

        m_dynamicHandle.idx = other.m_dynamicHandle.idx;
        other.m_dynamicHandle.idx = bgfx::kInvalidHandle;

        m_isDynamic = other.m_isDynamic;
        return *this;
    }

    IndexBuffer::~IndexBuffer() {
        free();
    }

    void IndexBuffer::init(const void *data, uint32_t size, bool isDynamic) {
        assert(size != 0);

        if (isDynamic || data == nullptr) {
            if (isValid()) {
                update(data, size);
            } else {
                m_isDynamic = true;
                const bgfx::Memory *mem = (data ? bgfx::copy(data, size) : bgfx::alloc(size));
                m_dynamicHandle = bgfx::createDynamicIndexBuffer(mem);
                assert(bgfx::isValid(m_dynamicHandle));
            }
        } else {
            assert(!isValid());
            m_isDynamic = false;
            m_staticHandle = bgfx::createIndexBuffer(bgfx::copy(data, size));
            assert(bgfx::isValid(m_staticHandle));
        }
    }

    void IndexBuffer::update(const void *data, uint32_t size, uint32_t offset) const {
        assert(size != 0);
        assert(m_isDynamic);
        assert(bgfx::isValid(m_dynamicHandle));

        bgfx::update(m_dynamicHandle, offset, bgfx::copy(data, size));
    }

    void IndexBuffer::free() {
        if (!m_isDynamic && bgfx::isValid(m_staticHandle)) {
            bgfx::destroy(m_staticHandle);
            m_staticHandle.idx = bgfx::kInvalidHandle;
        } else if (m_isDynamic && bgfx::isValid(m_dynamicHandle)) {
            bgfx::destroy(m_dynamicHandle);
            m_dynamicHandle.idx = bgfx::kInvalidHandle;
        }
    }

    void IndexBuffer::enable() const {
        if (!m_isDynamic) {
            assert(bgfx::isValid(m_staticHandle));
            bgfx::setIndexBuffer(m_staticHandle);
        } else {
            assert(bgfx::isValid(m_dynamicHandle));
            bgfx::setIndexBuffer(m_dynamicHandle);
        }
    }

    void IndexBuffer::enable(uint32_t startIndex, uint32_t numIndices) const {
        if (!m_isDynamic) {
            assert(bgfx::isValid(m_staticHandle));
            bgfx::setIndexBuffer(m_staticHandle, startIndex, numIndices);
        } else {
            assert(bgfx::isValid(m_dynamicHandle));
            bgfx::setIndexBuffer(m_dynamicHandle, startIndex, numIndices);
        }
    }

    bool IndexBuffer::isValid() const {
        return bgfx::isValid(m_staticHandle) || bgfx::isValid(m_dynamicHandle);
    }
}
