//
// FontResource 实现
//

#include "Font.hpp"
#include "../core/Log.hpp"

namespace Tina::Engine {

bool FontResource::load(const FileSystem::Content& blob)
{
    if (blob.empty()) {
        TINA_ERROR("Font: 空数据: {}", getPath().c_str());
        return false;
    }
    m_data = blob; // 保持字节，以便使用 FT_New_Memory_Face
    TINA_INFO("Font: 加载字节成功 {} ({} bytes)", getPath().c_str(), (int)m_data.size());
    return true;
}

void FontResource::unload()
{
    for (auto& kv : m_faces) {
        if (kv.second.face) { FT_Done_Face(kv.second.face); kv.second.face = nullptr; }
        if (kv.second.lib) { FT_Done_FreeType(kv.second.lib); kv.second.lib = nullptr; }
    }
    m_faces.clear();
    m_data.clear();
}

bool FontResource::ensureFace(int pixelSize)
{
    auto it = m_faces.find(pixelSize);
    if (it != m_faces.end() && it->second.face) return true;
    if (m_data.empty()) return false;

    FaceEntry ent{};
    if (FT_Init_FreeType(&ent.lib)) return false;
    if (FT_New_Memory_Face(ent.lib, m_data.data(), (FT_Long)m_data.size(), 0, &ent.face)) {
        FT_Done_FreeType(ent.lib);
        ent.lib = nullptr; ent.face = nullptr;
        return false;
    }
    FT_Select_Charmap(ent.face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(ent.face, 0, (FT_UInt)pixelSize);
    ent.sizePx = pixelSize;
    m_faces[pixelSize] = ent;
    return true;
}

FT_Face FontResource::getFace(int pixelSize) const
{
    auto it = m_faces.find(pixelSize);
    if (it == m_faces.end()) return nullptr;
    return it->second.face;
}

} // namespace Tina::Engine

