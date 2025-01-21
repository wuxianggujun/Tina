#include "Config.hpp"

#include <fstream>

namespace Tina
{
    void Config::loadFromFile(const std::string& filePath)
    {
        try
        {
            m_data.clear();

            YamlValue parseData = YamlParser::parseFile(filePath);

            if (std::holds_alternative<std::unordered_map<std::string, YamlValuePtr>>(parseData.data))
            {
                m_data = std::get<std::unordered_map<std::string, YamlValuePtr>>(parseData.data);
            }
            else
            {
                throw std::runtime_error("Load YAML data is not a map");
            }
        }
        catch (const std::runtime_error& e)
        {
            throw std::runtime_error("Error loading config from " + filePath + ": " + e.what());
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
        for (size_t i = 0; i < keys.size() - 1; ++i) {
            auto it = currentMap->find(keys[i]);
            if (it == currentMap->end() || !std::holds_alternative<std::unordered_map<std::string, YamlValuePtr>>(it->second->data)) {
                return false;
            }
            currentMap = &std::get<std::unordered_map<std::string, YamlValuePtr>>(it->second->data);
        }

        return currentMap->contains(keys.back());
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
