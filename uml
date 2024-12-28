classDiagram

    class GameApplication {
        -ScopePtr<IWindow> m_window
        -ScopePtr<IGuiSystem> m_guiSystem
        -ScopePtr<IRenderer> m_renderer
        -ScopePtr<EventHandler> m_eventHandler
        -ScopePtr<ResourceManager> m_resourceManager
        -entt::registry m_registry
        -float lastFrameTime
        +GameApplication(ScopePtr<IWindow>&&, ScopePtr<IRenderer>&&, ScopePtr<IGuiSystem>&&, ScopePtr<EventHandler>&&)
        +~GameApplication()
        +initialize()
        +run()
        +mainLoop()
        +shutdown()
        -update(float)
        -render()
        -createTestGui()
    }

    class CoreApplication {
        +CoreApplication(int, char**)
        +~CoreApplication()
        +run(bool)
        +hasBeenInterrupted : bool
        -init()
        -exit()
        -createWindow(uint16_t, uint16_t, const std::string&)
    }

    class IWindow {
        <<interface>>
        +WindowConfig
        +~IWindow()
        +create(const WindowConfig&)
        +destroy()
        +pollEvents()
        +shouldClose()
        +setEventHandler(ScopePtr<EventHandler>&&)
        +getNativeWindow()
    }
    
    class IWindow::WindowConfig {
        +std::string title
        +Vector2i resolution
        +bool resizable
        +bool maximized
        +bool fullScreen
    }

    class GLFWWindow {
        -GLFWwindow* m_window
        -ScopePtr<EventHandler> m_eventHandler
        +GLFWWindow()
        +~GLFWWindow()
        +create(const WindowConfig&)
        +destroy()
        +pollEvents()
        +shouldClose()
        +setEventHandler(ScopePtr<EventHandler>&&)
        +getNativeWindow()
    }

    class IRenderer {
        <<interface>>
        +~IRenderer()
        +init(Vector2i)
        +shutdown()
        +render()
        +frame()
        +getResolution()
        +setTexture(uint8_t, const ShaderUniform&, const ResourceHandle&)
        +submit(uint8_t, const ResourceHandle&)
    }

    class BgfxRenderer {
        -PosNormalTangentTexcoordVertex s_cubeVertices[24]
        -uint16_t s_cubeIndices[36]
        -IWindow* m_window
        -VertexBuffer m_vbh
        -IndexBuffer m_ibh
        -RefPtr<ResourceManager> m_resourceManager
        -Vector2i m_resolution
        +BgfxRenderer(IWindow*)
        +~BgfxRenderer()
        +init(Vector2i)
        +shutdown()
        +render()
        +frame()
        +getResolution()
        +setTexture(uint8_t, const ShaderUniform&, const ResourceHandle&)
        +submit(uint8_t, const ResourceHandle&)
    }
    
    class BgfxRenderer::PosNormalTangentTexcoordVertex {
        +float m_x
        +float m_y
        +float m_z
        +int16_t m_u
        +int16_t m_v
    }

    class IGuiSystem {
        <<interface>>
        +~IGuiSystem()
        +Initialize(int, int)
        +Shutdown()
        +Update(entt::registry&, float)
        +Render(entt::registry&)
    }

    class BgfxGuiSystem {
        -bgfx::VertexLayout m_layout
        +BgfxGuiSystem()
        +~BgfxGuiSystem()
        +Initialize(int, int)
        +Shutdown()
        +Update(entt::registry&, float)
        +Render(entt::registry&)
    }

    class ResourceManager {
        -std::unordered_map<ResourceHandle, RefPtr<Resource>> m_resources
        -std::unordered_map<ResourceType, RefPtr<Resource> (*)(const ResourceHandle&, const std::string&)> m_resourceFactories
        +ResourceManager()
        +~ResourceManager()
        +loadResource<T, Args...>(const std::string&, Args&&...)
        +getResource<T>(const ResourceHandle&)
        +unloadResource(const ResourceHandle&)
        +unloadAllResources()
        +registerResourceType(ResourceType, RefPtr<Resource> (*)(const ResourceHandle&, const std::string&))
    }

    class Resource {
        #ResourceHandle m_handle
        #std::string m_path
        #ResourceType m_type
        +Resource(const ResourceHandle&, const std::string&, ResourceType)
        +~Resource()
        +getHandle()
        +getPath()
        +getType()
        +isLoaded()
        +load()
        +unload()
    }

    class ResourceHandle {
        -uint64_t m_id
        +ResourceHandle()
        +ResourceHandle(uint64_t)
        +ResourceHandle(const std::string&)
        +getId()
        +isValid()
        +operator==()
        +operator!=()
        +operator<()
    }

    enum ResourceType {
        Unknown,
        Texture,
        Model,
        Shader,
        Sound
    }

    class TextureResource {
        -bgfx::TextureHandle m_textureHandle
        +TextureResource(const ResourceHandle&, const std::string&)
        +~TextureResource()
        +load()
        +unload()
        +isLoaded()
        +getTextureHandle()
    }

    class ShaderResource {
        -Shader m_shader
        +ShaderResource(const ResourceHandle&, const std::string&)
        +~ShaderResource()
        +load()
        +unload()
        +isLoaded()
        +getShader()
    }

    class Shader {
      -const std::string SHADER_PATH = "../resources/shaders/"
      -bgfx::ShaderHandle m_vertexShader = BGFX_INVALID_HANDLE
      -bgfx::ShaderHandle m_fragmentShader = BGFX_INVALID_HANDLE
      -bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE
      +Shader()
      +Shader(const std::string &name)
      +~Shader()
      +loadFromFile(const std::string &name)
      +loadShader(const std::string &path)
      +isValid()
      +getProgram()
    }

    class ShaderUniform {
        -const char *m_uniformName
        -bgfx::UniformHandle m_handle
        +ShaderUniform()
        +ShaderUniform(ShaderUniform &&other)
        +operator=(ShaderUniform &&other)
        +~ShaderUniform()
        +init(const char *uniformName, bgfx::UniformType::Enum type, uint16_t num = 1)
        +free()
        +setValue(float x, float y = 0.f, float z = 0.f, float w = 0.f)
        +setValue(const Color &color, bool needsRounding = false)
        +getHandle()
    }

    class VertexBuffer {
        -bgfx::VertexLayout m_layout;
        -bgfx::VertexBufferHandle m_handle = BGFX_INVALID_HANDLE;
        +VertexBuffer()
        +VertexBuffer(VertexBuffer&& other)
        +operator=(VertexBuffer&& other)
        +~VertexBuffer()
        +init(const void* data, uint32_t size)
        +init(const bgfx::VertexLayout& layout, const void* data, uint32_t size)
        +enable() const
        +free() const
        +getLayout()
        +handle()
    }
    
    class IndexBuffer {
        -bgfx::IndexBufferHandle m_handle = BGFX_INVALID_HANDLE;
        +IndexBuffer()
        +IndexBuffer(IndexBuffer&& other)
        +operator=(IndexBuffer&& other)
        +~IndexBuffer()
        +init(const void* data, uint32_t size)
        +enable() const
        +free() const
        +handle()
    }
    
    class EventHandler{
      +virtual void handleEvent(Event &event) = 0
    }

    GameApplication --|> CoreApplication
    GameApplication --> "1" IWindow
    GameApplication --> "1" IRenderer
    GameApplication --> "1" IGuiSystem
    GameApplication --> "1" EventHandler
    GameApplication --> "1" ResourceManager
    IWindow <|.. GLFWWindow
    IRenderer <|.. BgfxRenderer
    IGuiSystem <|.. BgfxGuiSystem
    ResourceManager --> "*" Resource
    Resource --> "1" ResourceHandle
    Resource --> "1" ResourceType
    TextureResource --|> Resource
    ShaderResource --|> Resource
    BgfxRenderer --> IWindow
    BgfxRenderer --> ResourceManager
    ShaderResource o-- Shader
    BgfxRenderer o-- IndexBuffer
    BgfxRenderer o-- VertexBuffer
    BgfxRenderer o-- ShaderUniform
    BgfxGuiSystem o--  "imgui"
    GLFWWindow o-- EventHandler