#include "ParserYamlConfig.hpp"
#include <iostream>
#include <string>

namespace Tina {
    ParserYamlConfig::ParserYamlConfig(const std::string &path): path_(path), file_(
                                                                     path_,
                                                                     FileMode::Text || FileMode::Read ||
                                                                     FileMode::Write) {
        loadConfig(path);
    }

    ParserYamlConfig::~ParserYamlConfig() {
    }

    void ParserYamlConfig::loadConfig(const std::string &path) {
        YAML::Node config = YAML::LoadFile(path);

        std::string runMode = config["application"]["run-mode"].as<std::string>();
        std::cout << "runMode: " << runMode << std::endl;
        
     
    }
}
