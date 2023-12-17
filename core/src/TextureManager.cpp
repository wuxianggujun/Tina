//
// Created by WuXiangGuJun on 2023/12/17.
//
#include "TextureManager.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_image.h>
#include "config.hpp"

SDL_Texture* TextureManager::getTexture(const std::string& id, const std::string& path, SDL_Renderer* renderer)
{
    if (m_textures.find(id) != m_textures.end())
    {
        return m_textures[id];
    }

    if (renderer == nullptr || path == "none")
    {
        BREAK();
    }

    const std::string finalPath = m_BasePath + path;
    SDL_Surface* surface = IMG_Load(finalPath.c_str());

    if (!surface)
    {
        BREAK();
    }
    m_textures[id] = SDL_CreateTextureFromSurface(renderer, surface);

    if (!m_textures[id])
        BREAK();

    SDL_Log(std::string("loaded " +id).c_str());

    return m_textures[id];
}

void TextureManager::clear()
{
    for (auto& texture:m_textures)
    {
        SDL_DestroyTexture(texture.second);
        SDL_Log(std::string("destroyed "+ texture.first).c_str());
    }
    m_textures.clear();
}

TextureManager::TextureManager()
{
    m_BasePath=SDL_GetBasePath();
}

TextureManager TextureManager::s_instance;


