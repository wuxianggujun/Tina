// //
// // Created by wuxianggujun on 2025/1/30.
// //
//
// #pragma once
//
// #include "base/String.hpp"
// #include "math/Rect.hpp"
// #include "graphics/Texture.hpp"
//
// namespace Tina
// {
//     class TextureAtlas
//     {
//     public:
//         void addFrame(const String& name, const Rect& rect);
//         const Rect& getFrame(const String& name) const;
//         void setTexture(bgfx::TextureHandle texture);
//         bgfx::TextureHandle getTexture() const;
//
//     private:
//         bgfx::TextureHandle m_texture;
//         std::unordered_map<String, Rect> m_frames;
//     };
// } // Tina
