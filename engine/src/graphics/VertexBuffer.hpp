#ifndef TINA_GRAPHICS_VERTEXBUFFER_hPP
#define TINA_GRAPHICS_VERTEXBUFFER_hPP

#include <bgfx/bgfx.h>
#include "base/NonCopyable.hpp"


namespace Tina {
    class VertexBuffer : public NonCopyable {
    public:
        VertexBuffer();

        VertexBuffer(VertexBuffer &&other) noexcept;

        VertexBuffer &operator=(VertexBuffer &&other) noexcept;

        ~VertexBuffer();

        void init(const void *data, uint32_t size, bool isDynamic = false);

        void update(const void *data, uint32_t size, uint32_t offset = 0) const;

        void free();

        void enable();

        void enable(uint32_t startVertex, uint32_t numVertices) const;

        [[nodiscard]] bool isValid() const;

        [[nodiscard]] decltype(auto) handle() const {
            return (!m_isDynamic) ? m_staticHandle.idx : m_dynamicHandle.idx;
        }

        bgfx::VertexLayout &getLayout() {
            return m_layout;
        }

        void setUpDefaultLayout();

    private:
        bgfx::VertexBufferHandle m_staticHandle = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle m_dynamicHandle = BGFX_INVALID_HANDLE;

        bgfx::VertexLayout m_layout;

        bool m_isDynamic = false;

        uint32_t m_size = 0;
    };
}

#endif // TINA_GRAPHICS_VERTEXBUFFER_hPP
