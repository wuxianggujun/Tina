//
// Created by WuXiangGuJun on 2023/12/13.
//

#ifndef TEXTUREMANAGER_HPP
#define TEXTUREMANAGER_HPP

#include <unordered_map>
#include <string>

struct SDL_Texture;
struct SDL_Renderer;

class TextureManager
{
public:
    static TextureManager& getInstance()
    {
        return s_instance;
    }

    SDL_Texture* getTexture(const std::string& id, const std::string& path = "none", SDL_Renderer* renderer = nullptr);

    void clear();

private:
    TextureManager();

    static TextureManager s_instance;

    std::string m_BasePath;

    std::unordered_map<std::string, SDL_Texture*> m_textures;
};


#endif //TEXTUREMANAGER_HPP
