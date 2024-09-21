#ifndef TINA_GRAPHICS_INDEXBUFFER_HPP
#define TINA_GRAPHICS_INDEXBUFFER_HPP

#include <bgfx/bgfx.h>
#include "base/NonCopyable.hpp"


namespace Tina {
    class IndexBuffer : public NonCopyable {
    public:
        IndexBuffer();

        IndexBuffer(IndexBuffer &&other) noexcept;

        IndexBuffer &operator=(IndexBuffer &&other) noexcept;

        ~IndexBuffer();

        void init(const void *data, uint32_t size, bool isDynamic = false);

        void update(const void *data, uint32_t size, uint32_t offset = 0) const;

        void free();

        void enable() const;

        void enable(uint32_t startIndex, uint32_t numIndices) const;

        [[nodiscard]] bool isValid() const;

        [[nodiscard]] decltype(auto) handle() const {
            return (!m_isDynamic) ? m_staticHandle.idx : m_dynamicHandle.idx;
        }

    private:
        bgfx::IndexBufferHandle m_staticHandle = BGFX_INVALID_HANDLE;
        bgfx::DynamicIndexBufferHandle m_dynamicHandle = BGFX_INVALID_HANDLE;

        bool m_isDynamic = false;
    };
}


#endif //TINA_GRAPHICS_INDEXBUFFER_HPP
