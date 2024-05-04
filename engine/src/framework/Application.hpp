//
// Created by wuxianggujun on 2024/4/27.
//

#ifndef TINA_APPLICATION_HPP
#define TINA_APPLICATION_HPP

namespace Tina {
    class Application {
    public:
        virtual void run() = 0;

    public:
        virtual bool initialize() = 0;
        virtual void close() {};
    };

} // Tina

#endif //TINA_APPLICATION_HPP
