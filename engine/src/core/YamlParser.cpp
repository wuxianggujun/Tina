//
// Created by wuxianggujun on 2025/1/21.
//

#include "YamlParser.hpp"
#include <fstream>
#include <iostream>

namespace Tina
{
    YamlValue YamlParser::parseFile(const std::string& filePath)
    {
        try
        {
            YAML::Node root = YAML::LoadFile(filePath);
            auto parsed = parseNode(root);
            if (!parsed)
            {
                throw std::runtime_error("Failed to parse YAML root node");
            }
            return *parsed;
        }
        catch (const YAML::Exception& e)
        {
            throw std::runtime_error("YAML parsing error: " + std::string(e.what()));
        }catch (const std::exception& e)
        {
            throw std::runtime_error("Error reading YAML file: " + std::string(e.what()));
        }
    }

    YamlValuePtr YamlParser::parseNode(const YAML::Node& node)
    {
        if (node.IsScalar())
        {
            std::string originalValue = node.Scalar();
            
            // 尝试按顺序转换，但不输出中间失败的消息
            try { return std::make_shared<YamlValue>(YamlValue{node.as<int>()}); }
            catch (...) {}
            
            try { return std::make_shared<YamlValue>(YamlValue{node.as<double>()}); }
            catch (...) {}
            
            try { return std::make_shared<YamlValue>(YamlValue{node.as<bool>()}); }
            catch (...) {}

            // 最后尝试作为字符串
            try {
                return std::make_shared<YamlValue>(YamlValue{node.as<std::string>()});
            }
            catch (const YAML::Exception& e) {
                std::cerr << "Failed to parse value: " << originalValue << " - " << e.what() << std::endl;
                throw;
            }
        }
        if (node.IsSequence())
        {
            std::vector<YamlValuePtr> sequence;
            for (const auto& seqIt : node)
            {
                sequence.emplace_back(parseNode(seqIt));
            }
            return std::make_shared<YamlValue>(YamlValue{sequence});
        }
        if (node.IsMap())
        {
            std::unordered_map<std::string, YamlValuePtr> map;
            for (const auto& mapIt : node)
            {
                map[mapIt.first.as<std::string>()] = parseNode(mapIt.second);
            }
            return std::make_shared<YamlValue>(YamlValue{map});
        }
        if (node.IsNull())
        {
            return std::make_shared<YamlValue>(YamlValue{std::string("")});
        }
        throw std::runtime_error("Unsupported YAML node type");
    }


    std::string YamlParser::stringify(const YamlValue& value)
    {
        YAML::Emitter out;
        serializeValue(out, std::make_shared<YamlValue>(value));
        return out.c_str();
    }

    void YamlParser::serializeValue(YAML::Emitter& out, const YamlValuePtr& valuePtr)
    {
        const YamlValue& value = *valuePtr;
        if (std::holds_alternative<std::unordered_map<std::string, YamlValuePtr>>(value.data))
        {
            out << YAML::BeginMap;
            const auto& map = std::get<std::unordered_map<
                std::string, YamlValuePtr>>(value.data);
            for (const auto& pair : map)
            {
                out << YAML::Key << pair.first;
                out << YAML::Value;
                serializeValue(out, pair.second);
            }
            out << YAML::EndMap;
        }
        else if (std::holds_alternative<std::vector<YamlValuePtr>>(value.data))
        {
            out << YAML::BeginSeq;
            const auto& vec = std::get<std::vector<YamlValuePtr>>(value.data);
            for (const auto& item : vec)
            {
                serializeValue(out, item);
            }
            out << YAML::EndSeq;
        }
        else if (std::holds_alternative<int>(value.data))
        {
            out << std::get<int>(value.data);
        }
        else if (std::holds_alternative<double>(value.data))
        {
            out << std::get<double>(value.data);
        }
        else if (std::holds_alternative<bool>(value.data))
        {
            out << std::get<bool>(value.data);
        }
        else if (std::holds_alternative<std::string>(value.data))
        {
            out << std::get<std::string>(value.data);
        }
        else
        {
            throw std::runtime_error("Unsupported data type for YAML serialization.");
        }
    }
} // Tina
