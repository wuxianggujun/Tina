#ifndef TINA_CORE_PARSER_YAML_CONFIG_HPP
#define TINA_CORE_PARSER_YAML_CONFIG_HPP

#include "filesystem/Path.hpp"
#include "filesystem/File.hpp"
#include <yaml-cpp/yaml.h>

namespace Tina {
    class ParserYamlConfig {
    public:
        ParserYamlConfig() = delete;

        explicit ParserYamlConfig(const std::string &path);

        ~ParserYamlConfig();

        void loadConfig(const std::string &path);

    private:
        Path path_;
        File file_;
        YAML::Parser parser_;
        YAML::Node root_;
    };
}


#endif //TINA_CORE_PARSER_YAML_CONFIG_HPP
