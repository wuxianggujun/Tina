#ifndef TINA_BASE_STRINGBUFFER_HPP
#define TINA_BASE_STRINGBUFFER_HPP

#include "io/Buffer.hpp"
#include <string>

namespace Tina {
    class StringBuffer {
    public:
        StringBuffer();

        ~StringBuffer();

        explicit StringBuffer(int capacity);

        explicit StringBuffer(const char *str);

        explicit StringBuffer(const std::string &str);

        void append(const char *str) const;

        void append(const char *str, int offset, int len) const;

        void append(const std::string &str) const;

        void append(const char &c) const;

        void clear() const;
        
        std::string convertEncoding(const std::string &input, const char *toEncoding) const;

        // [[nodiscard]] size_t countWhiteSpace(const icu::StringPiece &input) const;

    private:
        Buffer<char> *strBuffer;
    };
}


#endif //TINA_BASE_STRINGBUFFER_HPP
