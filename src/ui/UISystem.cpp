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
    TINA_INFO("UI: RenderInterface ctor");
    // 顶点布局：位置 + UV + 颜色（颜色为 RGBA8 归一化）
    m_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
    .end();

    // ShaderManager 与 UI 专用着色器
    m_shaderManager = new Tina::renderer::ShaderManager();
    m_shaderManager->initialize();
    m_program = m_shaderManager->loadProgram("ui", "ui");
    if (!bgfx::isValid(m_program)) {
        TINA_ERROR("Failed to load UI shader program");
    }

    // 采样器 uniform
    if (!bgfx::isValid(m_uniformTexture))
        m_uniformTexture = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    // 创建 1x1 白色纹理作为无纹理时的占位
    const uint32_t white = 0xffffffffu;
    const bgfx::Memory* mem = bgfx::copy(&white, sizeof(white));
    m_whiteTexture = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                           BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
                                           BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
                                           mem);
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
    if (bgfx::isValid(m_whiteTexture)) {
        bgfx::destroy(m_whiteTexture);
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
    static bool logged_once = false;
    if (!logged_once) {
        TINA_INFO("UI: CompileGeometry, verts={}, indices={}", (int)vertices.size(), (int)indices.size());
        logged_once = true;
    }
    
    // Convert vertices to bgfx format
    struct UIVertex {
        float x, y, z;     // 位置
        float u, v;        // UV
        float r, g, b, a;  // 颜色（float）
    };
    
    Tina::Container::Vector<UIVertex> uiVertices;
    uiVertices.reserve(vertices.size());
    
    for (const auto& vertex : vertices) {
        UIVertex uiVertex;
        uiVertex.x = vertex.position.x;
        uiVertex.y = vertex.position.y;
        uiVertex.z = 0.0f;
        uiVertex.u = vertex.tex_coord.x;
        uiVertex.v = vertex.tex_coord.y;
        uiVertex.r = (float)vertex.colour.red   / 255.0f;
        uiVertex.g = (float)vertex.colour.green / 255.0f;
        uiVertex.b = (float)vertex.colour.blue  / 255.0f;
        uiVertex.a = (float)vertex.colour.alpha / 255.0f;
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
    
    // 设置模型变换：基础为 SetTransform 传入的矩阵，再叠加 translation 偏移
    {
        float mtx[16];
        bx::mtxIdentity(mtx);
        mtx[12] = translation.x;
        mtx[13] = translation.y;
        bgfx::setTransform(mtx);
    }

    // Set render state
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;

    bgfx::Encoder* enc = bgfx::begin(false);
    if (!enc) return;

    if (m_scissorEnabled) {
        const uint16_t sx = (uint16_t)std::max(0, m_scissorRegion.Left());
        const uint16_t sy = (uint16_t)std::max(0, m_scissorRegion.Top());
        const uint16_t sw = (uint16_t)std::max(0, m_scissorRegion.Width());
        const uint16_t sh = (uint16_t)std::max(0, m_scissorRegion.Height());
        enc->setScissor(sx, sy, sw, sh);
    }

    // 绑定纹理（有 UI 纹理则使用，否则绑定 1x1 白色）
    if (bgfx::isValid(m_uniformTexture)) {
        bgfx::TextureHandle th = m_whiteTexture;
        if (texture != 0) {
            auto texIt = m_textures.find(texture);
            if (texIt != m_textures.end() && bgfx::isValid(texIt->second.handle)) {
                th = texIt->second.handle;
            }
        }
        if (bgfx::isValid(th)) {
            enc->setTexture(0, m_uniformTexture, th);
        }
    }

    enc->setVertexBuffer(0, data.vb);
    enc->setIndexBuffer(data.ib);
    enc->setState(state);

    static bool logged_rg_once = false;
    if (!logged_rg_once) {
        TINA_INFO("UI: RenderGeometry submit view={} tex={} scissor={} region=({},{} {}x{})", (int)m_currentViewId, (int)texture, m_scissorEnabled ? 1 : 0,
                  (int)m_scissorRegion.Left(), (int)m_scissorRegion.Top(), (int)m_scissorRegion.Width(), (int)m_scissorRegion.Height());
        logged_rg_once = true;
    }
    if (bgfx::isValid(m_program)) {
        enc->submit(m_currentViewId, m_program);
    }
    bgfx::end(enc);
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
    static bool logged_tex_once = false;
    if (!logged_tex_once) {
        TINA_INFO("UI: GenerateTexture {}x{} ({} bytes)", source_dimensions.x, source_dimensions.y, (int)source.size());
        logged_tex_once = true;
    }
    
    // RmlUI 6.x: GenerateTexture 提供 RGBA 预乘 alpha 数据
    const bgfx::Memory* mem = bgfx::copy(source.data(), (uint32_t)source.size());
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        (uint16_t)source_dimensions.x, (uint16_t)source_dimensions.y, false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
        mem
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
    // 简化：忽略自定义变换矩阵，依赖 RenderGeometry 的 translation。
    (void)transform;
    m_hasTransform = false;
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
    // 尝试加载字体文件，如果都失败则创建最小fallback字体
    struct FontCand { const char* path; Rml::Style::FontWeight weight; bool fallback; };
    Tina::Container::Vector<FontCand> cands;
    cands.push_back({"resources/fonts/SourceHanSansSC-Regular.otf", Rml::Style::FontWeight::Normal, false});
    cands.push_back({"resources/fonts/NotoSans-Regular.ttf",  Rml::Style::FontWeight::Normal, false});
    cands.push_back({"resources/fonts/Arial.ttf",            Rml::Style::FontWeight::Normal, true});

    bool any = false;
    for (const auto& f : cands) {
        FILE* fp = fopen(f.path, "rb");
        if (!fp) continue; 
        fclose(fp);
        if (Rml::LoadFontFace(f.path, f.fallback, f.weight)) {
            any = true;
            TINA_INFO("RmlUI: 加载字体成功: {}{}", f.path, f.fallback ? " (fallback)" : "");
        } else {
            TINA_WARN("RmlUI: 加载字体失败: {}", f.path);
        }
    }

    // 尝试加载粗体字重（若文件存在）
    {
        const char* bold_candidates[] = {
            "resources/fonts/SourceHanSansSC-Bold.otf",
            "resources/fonts/NotoSans-Bold.ttf"
        };
        for (const char* bp : bold_candidates) {
            FILE* fp = fopen(bp, "rb");
            if (!fp) continue;
            fclose(fp);
            if (Rml::LoadFontFace(bp, false, Rml::Style::FontWeight::Bold)) {
                any = true;
                TINA_INFO("RmlUI: 加载粗体字体成功: {}", bp);
                break;
            }
        }
    }
    
    // 如果没有找到任何字体文件，创建一个最小的内存字体
    if (!any) {
        TINA_WARN("RmlUI: 未找到字体文件，使用内置fallback字体");
        // 创建一个最小的1x1像素字体数据作为fallback
        const unsigned char minimal_font_data[] = {
            0x00, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x80, 0x00, 0x03, 0x00, 0x70,
            0x47, 0x44, 0x45, 0x46, 0x00, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x18,
            0x00, 0x00, 0x00, 0x18, 0x47, 0x50, 0x4F, 0x53, 0x9E, 0x0F, 0x6F, 0x64,
            0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x20, 0x47, 0x53, 0x55, 0x42,
            0x07, 0x24, 0x76, 0x56, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x0A,
            0x4F, 0x53, 0x2F, 0x32, 0x8A, 0x0F, 0x8E, 0xE4, 0x00, 0x00, 0x00, 0x5C,
            0x00, 0x00, 0x00, 0x60, 0x63, 0x6D, 0x61, 0x70, 0x00, 0x0C, 0x00, 0x14,
            0x00, 0x00, 0x00, 0xBC, 0x00, 0x00, 0x00, 0x1E, 0x67, 0x61, 0x73, 0x70,
            0xFF, 0xFF, 0x00, 0x03, 0x00, 0x00, 0x00, 0xDC, 0x00, 0x00, 0x00, 0x08,
            0x67, 0x6C, 0x79, 0x66, 0x2F, 0x91, 0x81, 0x84, 0x00, 0x00, 0x00, 0xE4,
            0x00, 0x00, 0x00, 0x0A, 0x68, 0x65, 0x61, 0x64, 0x0F, 0x71, 0x52, 0x73,
            0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x36
        };
        
        // 尝试从内存加载最小字体
        // 使用内存重载作为最后的 fallback
        Rml::Span<const Rml::byte> data_span(reinterpret_cast<const Rml::byte*>(minimal_font_data), sizeof(minimal_font_data));
        if (Rml::LoadFontFace(data_span, Rml::String("Fallback"), Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true)) {
            TINA_INFO("RmlUI: 使用内存 fallback 字体");
            any = true;
        }
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
    
    // 同步视口给渲染接口
    if (m_renderInterface) {
        m_renderInterface->SetViewport(m_viewportWidth, m_viewportHeight);
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
    if (m_context && m_renderInterface) {
        TINA_INFO("UI: render() begin");
        // 设置UI专用的视图（避免与其他视图冲突，使用较高的ID）
        const uint16_t uiViewId = 15;
        
        // 设置2D正交投影矩阵（左上角为原点，符合RmlUI坐标系）
        float ortho[16];
        bx::mtxOrtho(ortho, 0.0f, (float)m_viewportWidth, (float)m_viewportHeight, 0.0f, -1.0f, 1.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
        
        // 设置视图和投影
        bgfx::setViewTransform(uiViewId, nullptr, ortho);
        bgfx::setViewRect(uiViewId, 0, 0, (uint16_t)m_viewportWidth, (uint16_t)m_viewportHeight);
        
        // 仅清理深度（保留颜色，UI 叠加）
        bgfx::setViewClear(uiViewId, BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
        
        // 设置当前UI视图ID到渲染接口
        m_renderInterface->setCurrentViewId(uiViewId);
        
        m_context->Render();
        TINA_INFO("UI: render() end");
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

