#ifndef TINA_CORE_CONFIG_HPP
#define TINA_CORE_CONFIG_HPP

#include <string>
#include <unordered_map>
#include <any>
#include <yaml-cpp/yaml.h>


namespace Tina {
    class Config {
    public:
        Config() = default;

        virtual ~Config() = default;

        virtual void loadFromFile(const std::string &filePath);

        virtual void saveToFile(const std::string &filePath);

        template<typename T>
        T get(const std::string key) const;

        template<typename T>
        void set(const std::string key, const T &value);

        bool contains(const std::string &key) const;

    protected:
        std::unordered_map<std::string, std::any> m_data;
    };

    template<typename T>
    T Config::get(const std::string key) const {
        const auto it = m_data.find(key);
        if (it != m_data.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast &e) {
                return T{};
            }
        }
        return T{};
    }

    template<typename T>
    void Config::set(const std::string key, const T &value) {
        m_data[key] = value;
    }
}
#endif //TINA_CORE_CONFIG_HPP
