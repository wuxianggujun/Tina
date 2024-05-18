//
// Created by wuxianggujun on 2024/5/5.
// https://github.com/zsmj2017/MiniJson
//

#ifndef TINA_FRAMEWORK_JSON_HPP
#define TINA_FRAMEWORK_JSON_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Tina {

    enum class JsonType {
        kNULL,
        kBOOL,
        kNUMBER,
        kSTRING,
        kARRAY,
        kOBJECT
    };

    class JsonVlaue;

    class Json final {

    public:
        using _array = std::vector<Json>;
        using _object = std::unordered_map<std::string, Json>;
    public:


    public:


    };

} // Tina

#endif //TINA_FRAMEWORK_JSON_HPP
