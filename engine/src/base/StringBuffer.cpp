#include "StringBuffer.hpp"

namespace Tina {
    StringBuffer::StringBuffer(){
        strBuffer = new Buffer<char>(2048);
    }

    StringBuffer::StringBuffer(int capacity) {
        strBuffer = new Buffer<char>(capacity);
    }

    StringBuffer::StringBuffer(const char *str) {
        strBuffer = new Buffer<char>(strlen(str));
        strBuffer->append(str, strlen(str));
    }

    StringBuffer::StringBuffer(const std::string &str) {
        strBuffer = new Buffer<char>(str.size());
    }

    StringBuffer StringBuffer::append(const char *str) const {
        strBuffer->append(str, strlen(str));
        return *this;
    }

    StringBuffer StringBuffer::append(const char *str, const int offset, const int len) const {
        strBuffer->append(str + offset, len);
        return *this;
    }

    StringBuffer StringBuffer::append(const std::string &str) const {
        strBuffer->append(str.c_str(), str.size());
        return *this;
    }

    StringBuffer StringBuffer::append(const char &c) const {
        strBuffer->append(c);
        return *this;
    }
}
