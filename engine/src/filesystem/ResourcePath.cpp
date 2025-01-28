#include "ResourcePath.hpp"
#include <filesystem>

namespace Tina {
    // 静态成员初始化
    Path ResourcePath::s_rootPath("");
    const String ResourcePath::DEFAULT_ROOT_PATH("../../resources");

    // 资源子目录
    const String ResourcePath::SHADER_PATH("shaders");
    const String ResourcePath::CONFIG_PATH("config");
    const String ResourcePath::TEXTURE_PATH("textures");
    const String ResourcePath::MODEL_PATH("models");
    const String ResourcePath::FONT_PATH("fonts");
    const String ResourcePath::AUDIO_PATH("audio");

    void ResourcePath::setRootPath(const String& path) {
        s_rootPath = Path(path);
    }

    Path ResourcePath::getRootPath() {
        if (s_rootPath.isEmpty()) {
            s_rootPath = Path(DEFAULT_ROOT_PATH);
        }
        return s_rootPath;
    }

    Path ResourcePath::getShaderPath() {
        return getRootPath().getChildFile(SHADER_PATH);
    }

    Path ResourcePath::getConfigPath() {
        return getRootPath().getChildFile(CONFIG_PATH);
    }

    Path ResourcePath::getTexturePath() {
        return getRootPath().getChildFile(TEXTURE_PATH);
    }

    Path ResourcePath::getModelPath() {
        return getRootPath().getChildFile(MODEL_PATH);
    }

    Path ResourcePath::getFontPath() {
        return getRootPath().getChildFile(FONT_PATH);
    }

    Path ResourcePath::getAudioPath() {
        return getRootPath().getChildFile(AUDIO_PATH);
    }
} 