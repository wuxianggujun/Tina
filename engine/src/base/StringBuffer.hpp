#ifndef TINA_BASE_STRINGBUFFER_HPP
#define TINA_BASE_STRINGBUFFER_HPP

#include "io/Buffer.hpp"

namespace Tina {
    class StringBuffer {
    public:
        StringBuffer();
        explicit StringBuffer(int capacity);
        explicit StringBuffer(const char *str);
        explicit StringBuffer(const std::string &str);

        StringBuffer append(const char *str) const;
        StringBuffer append(const char* str,int offset,int len) const;

        StringBuffer append(const std::string &str) const;
        StringBuffer append(const char& c) const;
    
    private:
        Buffer<char>* strBuffer;
    };
}


#endif //TINA_BASE_STRINGBUFFER_HPP
