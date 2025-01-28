#include "Config.hpp"

#include <fstream>
#include <iostream>

namespace Tina
{
    void Config::loadFromFile(const std::string& filePath)
    {
        try
        {
            std::cout << "Attempting to load config from: " << filePath << std::endl;
            
            // 检查文件是否存在
            std::ifstream checkFile(filePath);
            if (!checkFile.good()) {
                std::cerr << "Config file does not exist or is not accessible: " << filePath << std::endl;
                return; // 如果文件不存在，直接返回，使用默认配置
            }
            checkFile.close();

            m_data.clear();
            std::cout << "Cleared existing config data" << std::endl;

            try {
                YamlValue parseData = YamlParser::parseFile(filePath);
                std::cout << "Successfully parsed YAML file" << std::endl;

                if (std::holds_alternative<std::unordered_map<std::string, YamlValuePtr>>(parseData.data))
                {
                    m_data = std::get<std::unordered_map<std::string, YamlValuePtr>>(parseData.data);
                    std::cout << "Successfully loaded config data" << std::endl;
                }
                else
                {
                    std::cerr << "YAML root is not a map" << std::endl;
                    return; // 使用默认配置
                }
            }
            catch (const YAML::Exception& e)
            {
                std::cerr << "YAML parsing error: " << e.what() << std::endl;
                return; // 使用默认配置
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error loading config: " << e.what() << std::endl;
            return; // 使用默认配置
        }
        catch (...)
        {
            std::cerr << "Unknown error while loading config" << std::endl;
            return; // 使用默认配置
        }
    }

    void Config::saveToFile(const std::string& filePath) const
    {
        try
        {
            YamlValue dataToSerialize(m_data);
            std::string yamlString = YamlParser::stringify(dataToSerialize);
            std::ofstream file(filePath);
            if (file.is_open())
            {
                file << yamlString;
                file.close();
            }
            else
            {
                throw std::runtime_error("Could not open file for writing: " + filePath);
            }
        }
        catch (const std::runtime_error& e)
        {
            throw std::runtime_error("Error saving config to " + filePath + ": " + e.what());
        }
    }

    bool Config::contains(const std::string& key) const
    {
        try {
            std::vector<std::string> keys;
            size_t start = 0;
            size_t end = key.find('.');
            while (end != std::string::npos) {
                keys.push_back(key.substr(start, end - start));
                start = end + 1;
                end = key.find('.', start);
            }
            keys.push_back(key.substr(start));

            const std::unordered_map<std::string, YamlValuePtr>* currentMap = &m_data;
            
            // 处理嵌套键
            for (size_t i = 0; i < keys.size() - 1; ++i) {
                auto it = currentMap->find(keys[i]);
                if (it == currentMap->end()) {
                    return false;
                }
                
                // 检查值是否为 map 类型
                if (!it->second || !std::holds_alternative<std::unordered_map<std::string, YamlValuePtr>>(it->second->data)) {
                    return false;
                }
                
                currentMap = &std::get<std::unordered_map<std::string, YamlValuePtr>>(it->second->data);
            }

            // 检查最后一个键
            return currentMap->find(keys.back()) != currentMap->end();
        }
        catch (...) {
            // 如果发生任何异常，返回 false
            return false;
        }
    }


    void Config::setNested(const std::vector<std::string>& keys, const YamlValue& value)
    {
        std::unordered_map<std::string, YamlValuePtr>* currentMap = &m_data;
        for (size_t i = 0; i < keys.size() - 1; ++i)
        {
            auto& key = keys[i];
            if (!currentMap->contains(key) || !std::holds_alternative<std::unordered_map<std::string, YamlValuePtr>>(
                (*currentMap)[key]->data))
            {
                (*currentMap)[key] = std::make_shared<YamlValue>(YamlValue{
                    std::unordered_map<std::string, YamlValuePtr>()
                });
            }
            currentMap = &std::get<std::unordered_map<std::string, YamlValuePtr>>((*currentMap)[key]->data);
        }
        (*currentMap)[keys.back()] = std::make_shared<YamlValue>(value);
    }

}
