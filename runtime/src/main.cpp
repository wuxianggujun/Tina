#include "TinaEngine.hpp"

using namespace Tina;

 int main(int argc, char* argv[]) {
    ParserYamlConfig config("../resources/config/textures.yaml");
    // 创建具体组件的实例
    ScopePtr<IWindow> window = Tina::createScopePtr<GLFWWindow>();
    ScopePtr<IRenderer> renderer = Tina::createScopePtr<BgfxRenderer>(window.get()); // BgfxRenderer 需要 IWindow 指针
    ScopePtr<IGuiSystem> guiSystem = Tina::createScopePtr<BgfxGuiSystem>();
    ScopePtr<EventHandler> eventHandler = Tina::createScopePtr<EventHandler>(); // 假设您有一个 EventHandler 的实现

    // 创建 GameApplication 实例，并传入组件
    GameApplication application(std::move(window), std::move(renderer), std::move(guiSystem), std::move(eventHandler));
    
    // 运行应用程序
    application.run();
    return 0;
}
