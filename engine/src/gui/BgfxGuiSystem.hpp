#ifndef TINA_CORE_BGFX_GUI_SYSTEM_HPP
#define TINA_CORE_BGFX_GUI_SYSTEM_HPP

#include "IGuiSystem.hpp"
#include "gui/GuiComponent.hpp"

namespace Tina {
    class BgfxGuiSystem : public IGuiSystem {
    public:
        BgfxGuiSystem() = default;
        ~BgfxGuiSystem() override;

        void initialize(int width, int height) override;

        void shutdown() override;

        void update(entt::registry &registry, float deltaTime) override;

        void render(entt::registry &registry) override;

    private:
        bgfx::VertexLayout m_layout;
    };
}

#endif //TINA_CORE_BGFX_GUI_SYSTEM_HPP