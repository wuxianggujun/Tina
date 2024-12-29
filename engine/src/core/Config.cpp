#include "Config.hpp"

namespace Tina {
    void Config::loadFromFile(const std::string &filePath) {
          YAML::Node root = YAML::LoadFile(filePath);

        for (YAML::const_iterator it = root.begin(); it != root.end(); ++it) {
            const std::string key = it->first.as<std::string>();
            const YAML::Node& valueNode = it->second;

            if (valueNode.IsScalar()) {
                // 简单标量类型 (int, float, bool, string)
                if (valueNode.Tag() == "!") {
                    // YAML 特殊类型处理，如 !!int, !!float, !!bool, !!str
                    // 这里可以根据需要添加更详细的类型处理
                    m_data[key] = valueNode.as<std::string>();
                } else {
                    // 尝试自动推断类型
                    try {
                        m_data[key] = valueNode.as<bool>();
                    } catch (...) {
                        try {
                            m_data[key] = valueNode.as<int>();
                        } catch (...) {
                            try {
                                m_data[key] = valueNode.as<double>();
                            } catch (...) {
                                m_data[key] = valueNode.as<std::string>();
                            }
                        }
                    }
                }
            } else if (valueNode.IsSequence()) {
                // 序列 (数组) 类型
                // 这里需要根据你的需求来决定如何存储数组
                // 例如，你可以将数组存储为 std::vector<std::any>
                std::vector<std::any> sequence;
                for (YAML::const_iterator seqIt = valueNode.begin(); seqIt != valueNode.end(); ++seqIt) {
                    // 简单处理，都当作字符串
                    sequence.push_back(seqIt->as<std::string>());
                }
                m_data[key] = sequence;
            } else if (valueNode.IsMap()) {
                // 映射 (字典) 类型
                // 这里需要根据你的需求来决定如何存储字典
                // 例如，你可以将字典存储为 std::unordered_map<std::string, std::any>
                std::unordered_map<std::string, std::any> map;
                for (YAML::const_iterator mapIt = valueNode.begin(); mapIt != valueNode.end(); ++mapIt) {
                    map[mapIt->first.as<std::string>()] = mapIt->second.as<std::string>();
                }
                m_data[key] = map;
            }
        }
    }


     void Config::saveToFile(const std::string& filePath) {
        YAML::Emitter out; // 创建 YAML::Emitter 对象
        out << YAML::BeginMap; // 开始写入 YAML 映射

        for (const auto& pair : m_data) {
            const std::string& key = pair.first;
            const std::any& value = pair.second;

            if (value.type() == typeid(int)) {
                out << YAML::Key << key << YAML::Value << std::any_cast<int>(value);
            } else if (value.type() == typeid(double)) {
                out << YAML::Key << key << YAML::Value << std::any_cast<double>(value);
            } else if (value.type() == typeid(bool)) {
                out << YAML::Key << key << YAML::Value << std::any_cast<bool>(value);
            } else if (value.type() == typeid(std::string)) {
                out << YAML::Key << key << YAML::Value << std::any_cast<std::string>(value);
            } else if (value.type() == typeid(std::vector<std::any>)) {
                const auto& vec = std::any_cast<std::vector<std::any>>(value);
                out << YAML::Key << key << YAML::Value << YAML::BeginSeq; // 开始写入 YAML 序列
                for (const auto& item : vec) {
                    if (item.type() == typeid(std::string)) {
                        out << std::any_cast<std::string>(item);
                    }
                }
                out << YAML::EndSeq; // 结束写入 YAML 序列
            } else if (value.type() == typeid(std::unordered_map<std::string, std::any>)) {
                const auto& map = std::any_cast<std::unordered_map<std::string, std::any>>(value);
                out << YAML::Key << key << YAML::Value << YAML::BeginMap; // 开始写入 YAML 映射
                for (const auto& mapPair : map) {
                    if (mapPair.second.type() == typeid(std::string)) {
                        out << YAML::Key << mapPair.first << YAML::Value << std::any_cast<std::string>(mapPair.second);
                    }
                }
                out << YAML::EndMap; // 结束写入 YAML 映射
            }
        }

        out << YAML::EndMap; // 结束写入 YAML 映射

        // 将 Emitter 的内容写入文件
        std::ofstream fout(filePath);
        if (fout.is_open()) {
            fout << out.c_str(); // 使用 out.c_str() 获取生成的 YAML 字符串
            fout.close();
        } else {
            // 文件打开失败处理
            // 可以抛出异常或者记录日志
        }
    }

    
    bool Config::contains(const std::string& key) const {
        return m_data.contains(key);
    }

    
}
