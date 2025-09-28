//
// UI System Implementation
//

#include "UISystem.hpp"
#include "../core/Log.hpp"
#include "../renderer/ShaderManager.hpp"
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <chrono>
#include <cstring>

namespace Tina::UI {

//=============================================================================
// SystemInterface Implementation
//=============================================================================

SystemInterface::SystemInterface() {
    auto now = std::chrono::high_resolution_clock::now();
    m_startTime = std::chrono::duration<double>(now.time_since_epoch()).count();
}

SystemInterface::~SystemInterface() = default;

double SystemInterface::GetElapsedTime() {
    auto now = std::chrono::high_resolution_clock::now();
    double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
    return currentTime - m_startTime;
}

bool SystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    // 对高频、重复的警告做节流，避免一帧大量日志导致卡顿
    static int no_font_warn_count = 0;
    const bool is_no_font = message.find("No font face defined") != Rml::String::npos;

    if (is_no_font) {
        if (no_font_warn_count < 3) {
            TINA_WARN("RmlUI: {}", message.c_str());
        } else if (no_font_warn_count == 3) {
            TINA_WARN("RmlUI: No font face defined ... (后续重复警告已抑制)");
        }
        ++no_font_warn_count;
        return true;
    }

    switch (type) {
        case Rml::Log::LT_ERROR:   TINA_ERROR("RmlUI: {}", message.c_str()); break;
        case Rml::Log::LT_WARNING: TINA_WARN ("RmlUI: {}", message.c_str()); break;
        case Rml::Log::LT_INFO:    TINA_INFO ("RmlUI: {}", message.c_str()); break;
        default:                   TINA_DEBUG("RmlUI: {}", message.c_str()); break;
    }
    return true;
}

//=============================================================================
// RenderInterface Implementation
//=============================================================================

RenderInterface::RenderInterface() : m_nextTextureId(1), m_nextGeometryId(1) {
    // 初始化 UI 顶点布局：与内置 color 着色器保持一致
    m_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
    .end();
    
    // Initialize shader manager and load UI shader
    m_shaderManager = new Tina::renderer::ShaderManager();
    m_shaderManager->initialize();
    
    // 加载 UI 着色器程序：复用已有 color 程序（使用 u_modelViewProj 内置常量缓冲）
    m_program = m_shaderManager->loadProgram("color", "color");
    if (!bgfx::isValid(m_program)) {
        TINA_ERROR("Failed to load UI shader program");
    }

    // 仅创建采样器（当前 UI 着色器未用纹理，可不创建也行）。
    if (!bgfx::isValid(m_uniformTexture))
        m_uniformTexture = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
}

RenderInterface::~RenderInterface() {
    // Cleanup textures
    for (auto& pair : m_textures) {
        if (bgfx::isValid(pair.second.handle)) {
            bgfx::destroy(pair.second.handle);
        }
    }
    
    // Cleanup geometries
    for (auto& pair : m_geometries) {
        if (bgfx::isValid(pair.second.vb)) {
            bgfx::destroy(pair.second.vb);
        }
        if (bgfx::isValid(pair.second.ib)) {
            bgfx::destroy(pair.second.ib);
        }
    }
    
    if (bgfx::isValid(m_uniformTexture)) {
        bgfx::destroy(m_uniformTexture);
    }
    // 无需销毁 u_modelViewProj（为 bgfx 内置常量缓冲，由 setTransform/setViewTransform 驱动）
    
    // Cleanup shader manager (this will destroy the program)
    delete m_shaderManager;
}

void RenderInterface::SetViewport(int width, int height) {
    m_viewportWidth = width;
    m_viewportHeight = height;
}

void RenderInterface::SetScissorRegion(Rml::Rectanglei region) {
    m_scissorRegion = region;
}

void RenderInterface::EnableScissorRegion(bool enable) {
    m_scissorEnabled = enable;
}

Rml::CompiledGeometryHandle RenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
    if (vertices.empty() || indices.empty()) {
        return 0;
    }
    
    // Convert vertices to bgfx format
    struct UIVertex {
        float x, y, z;  // 3D position to match varying.def.sc
        uint32_t color;
    };
    
    Tina::Container::Vector<UIVertex> uiVertices;
    uiVertices.reserve(vertices.size());
    
    for (const auto& vertex : vertices) {
        UIVertex uiVertex;
        uiVertex.x = vertex.position.x;
        uiVertex.y = vertex.position.y;
        uiVertex.z = 0.0f;  // UI is 2D, set z=0
        uiVertex.color = vertex.colour.red | (vertex.colour.green << 8) | 
                        (vertex.colour.blue << 16) | (vertex.colour.alpha << 24);
        uiVertices.push_back(uiVertex);
    }
    
    // Create bgfx buffers
    const bgfx::Memory* memv = bgfx::copy(uiVertices.data(), (uint32_t)(uiVertices.size() * sizeof(UIVertex)));
    bgfx::VertexBufferHandle vb = bgfx::createVertexBuffer(memv, m_layout);
    
    const bgfx::Memory* memi = bgfx::copy(indices.data(), (uint32_t)(indices.size() * sizeof(int)));
    bgfx::IndexBufferHandle ib = bgfx::createIndexBuffer(memi, BGFX_BUFFER_INDEX32);
    
    if (!bgfx::isValid(vb) || !bgfx::isValid(ib)) {
        if (bgfx::isValid(vb)) bgfx::destroy(vb);
        if (bgfx::isValid(ib)) bgfx::destroy(ib);
        return 0;
    }
    
    // Store geometry data
    Rml::CompiledGeometryHandle handle = m_nextGeometryId++;
    GeometryData& data = m_geometries[handle];
    data.vb = vb;
    data.ib = ib;
    data.indexCount = (uint32_t)indices.size();
    
    return handle;
}

void RenderInterface::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) {
    auto it = m_geometries.find(geometry);
    if (it == m_geometries.end()) {
        return;
    }
    
    const GeometryData& data = it->second;
    if (!bgfx::isValid(data.vb) || !bgfx::isValid(data.ib)) {
        return;
    }
    
    // 设置模型变换：将 RmlUI 传入的 translation 应用于世界坐标
    {
        float mtx[16];
        bx::mtxIdentity(mtx);
        mtx[12] = translation.x; // X 平移
        mtx[13] = translation.y; // Y 平移
        bgfx::setTransform(mtx);
    }

    // Set render state
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;
    
    if (m_scissorEnabled) {
        bgfx::setScissor(m_scissorRegion.Left(), 
                        m_viewportHeight - m_scissorRegion.Top() - m_scissorRegion.Height(),
                        m_scissorRegion.Width(), m_scissorRegion.Height());
    }
    
    // Set texture if available（当前 color 着色器未采样，可忽略）
    if (texture != 0 && bgfx::isValid(m_uniformTexture)) {
        auto texIt = m_textures.find(texture);
        if (texIt != m_textures.end() && bgfx::isValid(texIt->second.handle)) {
            bgfx::setTexture(0, m_uniformTexture, texIt->second.handle);
        }
    }
    
    bgfx::setVertexBuffer(0, data.vb);
    bgfx::setIndexBuffer(data.ib);
    bgfx::setState(state);
    
    if (bgfx::isValid(m_program)) {
        bgfx::submit(0, m_program);
    }
}

void RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    auto it = m_geometries.find(geometry);
    if (it != m_geometries.end()) {
        if (bgfx::isValid(it->second.vb)) {
            bgfx::destroy(it->second.vb);
        }
        if (bgfx::isValid(it->second.ib)) {
            bgfx::destroy(it->second.ib);
        }
        m_geometries.erase(it);
    }
}

Rml::TextureHandle RenderInterface::LoadTexture(Rml::Vector2i& texture_dimensions,
                                              const Rml::String& source) {
    // TODO: Implement texture loading from file
    TINA_WARN("LoadTexture not implemented: {}", source.c_str());
    texture_dimensions.x = 1;
    texture_dimensions.y = 1;
    return 0;
}

Rml::TextureHandle RenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source,
                                                   Rml::Vector2i source_dimensions) {
    if (source.empty() || source_dimensions.x <= 0 || source_dimensions.y <= 0) {
        return 0;
    }
    
    // Create bgfx texture
    const bgfx::Memory* mem = bgfx::copy(source.data(), source.size());
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        source_dimensions.x, source_dimensions.y, false, 1,
        bgfx::TextureFormat::RGBA8, 0, mem
    );
    
    if (!bgfx::isValid(handle)) {
        return 0;
    }
    
    // Store texture data
    Rml::TextureHandle textureId = m_nextTextureId++;
    TextureData& data = m_textures[textureId];
    data.handle = handle;
    data.width = source_dimensions.x;
    data.height = source_dimensions.y;
    
    return textureId;
}

void RenderInterface::ReleaseTexture(Rml::TextureHandle texture) {
    auto it = m_textures.find(texture);
    if (it != m_textures.end()) {
        if (bgfx::isValid(it->second.handle)) {
            bgfx::destroy(it->second.handle);
        }
        m_textures.erase(it);
    }
}

void RenderInterface::SetTransform(const Rml::Matrix4f* transform) {
    // TODO: Set transform uniform
    (void)transform;
}

//=============================================================================
// UISystem Implementation
//=============================================================================

UISystem::UISystem() = default;

UISystem::~UISystem() {
    shutdown();
}

bool UISystem::initialize(int viewportWidth, int viewportHeight) {
    if (m_initialized) {
        return true;
    }
    
    m_viewportWidth = viewportWidth;
    m_viewportHeight = viewportHeight;
    
    if (!initializeRmlUI()) {
        TINA_ERROR("Failed to initialize RmlUI");
        return false;
    }
    
    m_initialized = true;
    TINA_INFO("UI System initialized successfully");
    return true;
}

void UISystem::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    shutdownRmlUI();
    m_initialized = false;
    TINA_INFO("UI System shutdown");
}

static bool LoadDefaultFonts() {
    // 依次尝试加载常见字体，第一款作为全局回退（fallback），避免“Arial, sans-serif”找不到时报错。
    // 注意：请将字体文件放在 resources/fonts/ 目录。
    struct FontCand { const char* path; Rml::Style::FontWeight weight; bool fallback; };
    Tina::Container::Vector<FontCand> cands;
    // 优先尝试用户提供的思源黑体（简体中文）
    cands.push_back({"resources/fonts/SourceHanSansSC-Regular.otf", Rml::Style::FontWeight::Normal, true});
    cands.push_back({"resources/fonts/SourceHanSansSC-Regular.otf", Rml::Style::FontWeight::Bold,   false});
    
    cands.push_back({"resources/fonts/NotoSans-Regular.ttf",  Rml::Style::FontWeight::Normal, true});
    cands.push_back({"resources/fonts/NotoSans-Bold.ttf",     Rml::Style::FontWeight::Bold,   false});
    cands.push_back({"resources/fonts/NotoSansSC-Regular.otf",Rml::Style::FontWeight::Normal, true});
    cands.push_back({"resources/fonts/NotoSansSC-Bold.otf",   Rml::Style::FontWeight::Bold,   false});
    cands.push_back({"resources/fonts/NotoSansCJKsc-Regular.otf", Rml::Style::FontWeight::Normal, true});
    cands.push_back({"resources/fonts/NotoSansCJKsc-Bold.otf",    Rml::Style::FontWeight::Bold,   false});
    cands.push_back({"resources/fonts/Inter-Regular.ttf",    Rml::Style::FontWeight::Normal, true});
    cands.push_back({"resources/fonts/Inter-Bold.ttf",       Rml::Style::FontWeight::Bold,   false});
    cands.push_back({"resources/fonts/Arial.ttf",            Rml::Style::FontWeight::Normal, true});
    cands.push_back({"resources/fonts/Arial-Bold.ttf",       Rml::Style::FontWeight::Bold,   false});

    bool any = false;
    for (const auto& f : cands) {
        // 简单检查文件是否存在
        FILE* fp = fopen(f.path, "rb");
        if (!fp) continue; fclose(fp);
        if (Rml::LoadFontFace(f.path, f.fallback, f.weight)) {
            any = true;
            TINA_INFO("RmlUI: 加载字体成功: {}{}", f.path, f.fallback ? " (fallback)" : "");
        } else {
            TINA_WARN("RmlUI: 加载字体失败: {}", f.path);
        }
    }
    if (!any) {
        TINA_WARN("RmlUI: 未找到可用字体 (resources/fonts)。UI 将退化且可能无文本显示。");
    }
    return any;
}

bool UISystem::initializeRmlUI() {
    // Create interfaces
    m_systemInterface = new SystemInterface();
    m_renderInterface = new RenderInterface();
    
    // Initialize RmlUI
    Rml::SetSystemInterface(m_systemInterface);
    Rml::SetRenderInterface(m_renderInterface);
    
    if (!Rml::Initialise()) {
        TINA_ERROR("Failed to initialize RmlUI core");
        return false;
    }
    
    // Create context
    m_context = Rml::CreateContext("main", Rml::Vector2i(m_viewportWidth, m_viewportHeight));
    if (!m_context) {
        TINA_ERROR("Failed to create RmlUI context");
        return false;
    }
    
    // 加载默认字体，必须在文档显示前完成
    LoadDefaultFonts();
    
    return true;
}

void UISystem::shutdownRmlUI() {
    if (m_context) {
        m_context = nullptr;
    }
    
    Rml::Shutdown();
    
    delete m_renderInterface;
    delete m_systemInterface;
    m_renderInterface = nullptr;
    m_systemInterface = nullptr;
}

void UISystem::handleEvent(const Tina::os::Event& event) {
    if (!m_context) return;
    
    switch (event.type) {
        case Tina::os::Event::Type::KEY:
            m_context->ProcessKeyDown(
                convertKeyCode(event.key.key_code),
                0 // key modifier flags
            );
            break;
            
        case Tina::os::Event::Type::CHAR:
            if (event.text_input.utf8 > 0) {
                m_context->ProcessTextInput(static_cast<Rml::Character>(event.text_input.utf8));
            }
            break;
            
        case Tina::os::Event::Type::MOUSE_BUTTON:
            m_context->ProcessMouseButtonDown(
                convertMouseButton(event.mouse_button.button),
                0 // key modifier flags
            );
            break;
            
        case Tina::os::Event::Type::MOUSE_MOVE:
            // Note: RmlUI expects absolute coordinates, we may need to track mouse position
            break;
            
        case Tina::os::Event::Type::MOUSE_WHEEL:
            m_context->ProcessMouseWheel(
                static_cast<float>(event.mouse_wheel.amount), 
                0 // key modifier flags
            );
            break;
            
        default:
            break;
    }
}

void UISystem::update(float deltaTime) {
    if (m_context) {
        m_context->Update();
    }
}

void UISystem::render() {
    if (m_context) {
        m_context->Render();
    }
}

void UISystem::setViewportSize(int width, int height) {
    m_viewportWidth = width;
    m_viewportHeight = height;
    
    if (m_context) {
        m_context->SetDimensions(Rml::Vector2i(width, height));
    }
    
    if (m_renderInterface) {
        m_renderInterface->SetViewport(width, height);
    }
}

Rml::ElementDocument* UISystem::loadDocument(const Rml::String& path) {
    if (!m_context) {
        return nullptr;
    }
    
    return m_context->LoadDocument(path);
}

// Key conversion helper
Rml::Input::KeyIdentifier UISystem::convertKeyCode(Tina::os::KeyCode keyCode) {
    switch (keyCode) {
        case Tina::os::KeyCode::A: return Rml::Input::KI_A;
        case Tina::os::KeyCode::C: return Rml::Input::KI_C;
        case Tina::os::KeyCode::D: return Rml::Input::KI_D;
        case Tina::os::KeyCode::E: return Rml::Input::KI_E;
        case Tina::os::KeyCode::F: return Rml::Input::KI_F;
        case Tina::os::KeyCode::S: return Rml::Input::KI_S;
        case Tina::os::KeyCode::W: return Rml::Input::KI_W;
        case Tina::os::KeyCode::ESCAPE: return Rml::Input::KI_ESCAPE;
        case Tina::os::KeyCode::RETURN: return Rml::Input::KI_RETURN;
        case Tina::os::KeyCode::SPACE: return Rml::Input::KI_SPACE;
        case Tina::os::KeyCode::LEFT: return Rml::Input::KI_LEFT;
        case Tina::os::KeyCode::RIGHT: return Rml::Input::KI_RIGHT;
        case Tina::os::KeyCode::UP: return Rml::Input::KI_UP;
        case Tina::os::KeyCode::DOWN: return Rml::Input::KI_DOWN;
        default: return Rml::Input::KI_UNKNOWN;
    }
}

int UISystem::convertMouseButton(Tina::os::MouseButton button) {
    switch (button) {
        case Tina::os::MouseButton::LEFT: return 0;
        case Tina::os::MouseButton::RIGHT: return 1;
        case Tina::os::MouseButton::MIDDLE: return 2;
        default: return 0;
    }
}

} // namespace Tina::UI
