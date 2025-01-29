//
// Created by wuxianggujun on 2025/1/21.
//

#ifndef TINA_CORE_YAMLPARSER_HPP
#define TINA_CORE_YAMLPARSER_HPP

#include <string>
#include <any>
#include <unordered_map>
#include <variant>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "YamlParser.hpp"
#include "YamlParser.hpp"

namespace Tina
{
    struct YamlValue;

    // 使用 std::shared_ptr 包装 YamlValue
    using YamlValuePtr = std::shared_ptr<YamlValue>;

    struct YamlValue
    {
        std::variant<int, double, bool, std::string, std::vector<YamlValuePtr>, std::unordered_map<std::string, YamlValuePtr>> data;
        // 添加构造函数
        YamlValue(int val) : data(val) {}
        YamlValue(double val) : data(val) {}
        YamlValue(bool val) : data(val) {}
        YamlValue(const std::string& val) : data(val) {}
        YamlValue(const char* val) : data(std::string(val)) {}  // 为了方便从字符串字面量构造
        YamlValue(const std::vector<YamlValuePtr>& val) : data(val) {}
        YamlValue(const std::unordered_map<std::string, YamlValuePtr>& val) : data(val) {}
    };

    class YamlParser
    {
    public:
        static YamlValue parseFile(const std::string& filePath);
        static std::string stringify(const YamlValue& data);
    private:
        static YamlValuePtr parseNode(const YAML::Node& node);
        static void serializeValue(YAML::Emitter& emitter, const YamlValuePtr& value);
    };
} // Tina

#endif //TINA_CORE_YAMLPARSER_HPP
