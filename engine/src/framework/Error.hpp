//
// Created by wuxianggujun on 2024/5/30.
//

#ifndef TINA_FRAMEWORK_ERROR_HPP
#define TINA_FRAMEWORK_ERROR_HPP

namespace Tina{
    enum Error{
        SUCCESS = 0,
        FAILED,
        ERROR_UNAVAILABLE
    };
    extern const char* error_names[];
}

#endif //TINA_FRAMEWORK_ERROR_HPP
