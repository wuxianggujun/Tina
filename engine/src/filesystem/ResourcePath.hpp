#ifndef TINA_FILESYSTEM_RESOURCE_PATH_HPP
#define TINA_FILESYSTEM_RESOURCE_PATH_HPP

#include "base/String.hpp"
#include "filesystem/Path.hpp"

namespace Tina {
    class ResourcePath {
    public:
        static void setRootPath(const String& path);
        static Path getRootPath();
        
        // 获取各种资源的路径
        static Path getShaderPath();
        static Path getConfigPath();
        static Path getTexturePath();
        static Path getModelPath();
        static Path getFontPath();
        static Path getAudioPath();
        
        // 组合路径
        static Path combine(const Path& base, const Path& relative);
        
    private:
        static Path s_rootPath;  // 资源根目录
        static const String DEFAULT_ROOT_PATH;  // 默认根目录
        
        // 各种资源的相对路径
        static const String SHADER_PATH;
        static const String CONFIG_PATH;
        static const String TEXTURE_PATH;
        static const String MODEL_PATH;
        static const String FONT_PATH;
        static const String AUDIO_PATH;
    };
}

#endif // TINA_FILESYSTEM_RESOURCE_PATH_HPP