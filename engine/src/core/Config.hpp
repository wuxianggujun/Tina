#ifndef TINA_CORE_CONFIG_HPP
#define TINA_CORE_CONFIG_HPP

#include "YamlParser.hpp"

namespace Tina
{
    class Config
    {
    public:
        Config() = default;

        virtual ~Config() = default;

        void loadFromFile(const std::string& filePath);

        void saveToFile(const std::string& filePath) const;

        template <typename T>
        T get(const std::string& key) const;

        template <typename T>
        void set(const std::string& key, const T& value);

        bool contains(const std::string& key) const;

    protected:
        std::unordered_map<std::string, YamlValuePtr> m_data;

        // Helper functions to get nested values and convert YamlValue to T
        template <typename T>
        T getNested(const std::vector<std::string>& keys) const;

        template <typename T>
        T convertTo(const YamlValuePtr& valuePtr) const;

        // Helper function to set nested values
        void setNested(const std::vector<std::string>& keys, const YamlValue& value);
    };

    template <typename T>
    T Config::get(const std::string& key) const
    {
        std::vector<std::string> keys;
        size_t start = 0;
        size_t end = key.find('.');

        // 如果键中包含 '.'，则进行分割
        if (end != std::string::npos)
        {
            while (end != std::string::npos)
            {
                keys.push_back(key.substr(start, end - start));
                start = end + 1;
                end = key.find('.', start);
            }
            keys.push_back(key.substr(start));

            return getNested<T>(keys);
        }
        // 如果键中不包含 '.'，则直接在 m_data 中查找
        const auto it = m_data.find(key);
        if (it != m_data.end())
        {
            return convertTo<T>(it->second);
        }
        throw std::runtime_error("Key not found: " + key);
    }

    template <typename T>
    void Config::set(const std::string& key, const T& value)
    {
        std::vector<std::string> keys;
        size_t start = 0;
        size_t end = key.find('.');
        while (end != std::string::npos)
        {
            keys.push_back(key.substr(start, end - start));
            start = end + 1;
            end = key.find('.', start);
        }

        keys.push_back(key.substr(start));
        setNested(keys, YamlValue(value));
    }

    template <typename T>
    T Config::getNested(const std::vector<std::string>& keys) const
    {
        const std::unordered_map<std::string, YamlValuePtr>* currentMap = &m_data;
        for (size_t i = 0; i < keys.size() - 1; ++i)
        {
            auto it = currentMap->find(keys[i]);

            if (it == currentMap->end() || !std::holds_alternative<std::unordered_map<std::string, YamlValuePtr>>(
                it->second->data))
            {
                std::string fullKey;
                for(size_t j = 0; j <= i; ++j) {
                    fullKey += keys[j];
                    if(j < i) fullKey += ".";
                }
                throw std::runtime_error("Key not found or invalid type: " + fullKey);
            }
            currentMap = &std::get<std::unordered_map<std::string, YamlValuePtr>>(it->second->data);
        }

        const auto finalIt = currentMap->find(keys.back());
        if (finalIt != currentMap->end())
        {
            return convertTo<T>(finalIt->second);
        }

        throw std::runtime_error("Key not found: " + keys.back());
    }

    template <typename T>
    T Config::convertTo(const YamlValuePtr& valuePtr) const
    {
        if (!valuePtr) {
            throw std::runtime_error("Null value pointer");
        }

        const YamlValue& value = *valuePtr;

        if constexpr (std::is_same_v<T, int>)
        {
            if (std::holds_alternative<int>(value.data))
            {
                return std::get<int>(value.data);
            }
            else if (std::holds_alternative<double>(value.data))
            {
                return static_cast<int>(std::get<double>(value.data));
            }
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            if (std::holds_alternative<double>(value.data))
            {
                return std::get<double>(value.data);
            }
            else if (std::holds_alternative<int>(value.data))
            {
                return static_cast<double>(std::get<int>(value.data));
            }
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            if (std::holds_alternative<bool>(value.data))
            {
                return std::get<bool>(value.data);
            }
            else if (std::holds_alternative<int>(value.data))
            {
                return std::get<int>(value.data) != 0;
            }
            else if (std::holds_alternative<std::string>(value.data))
            {
                const auto& str = std::get<std::string>(value.data);
                return str == "true" || str == "1" || str == "yes" || str == "on";
            }
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (std::holds_alternative<std::string>(value.data))
            {
                return std::get<std::string>(value.data);
            }
            else if (std::holds_alternative<int>(value.data))
            {
                return std::to_string(std::get<int>(value.data));
            }
            else if (std::holds_alternative<double>(value.data))
            {
                return std::to_string(std::get<double>(value.data));
            }
            else if (std::holds_alternative<bool>(value.data))
            {
                return std::get<bool>(value.data) ? "true" : "false";
            }
        }
        throw std::runtime_error("Type conversion error");
    }
}
#endif //TINA_CORE_CONFIG_HPP
