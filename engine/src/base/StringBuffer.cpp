#include "StringBuffer.hpp"

namespace Tina {
    StringBuffer::StringBuffer() {
        strBuffer = new Buffer<char>(2048);
    }

    StringBuffer::~StringBuffer() {
        if (strBuffer) {
            strBuffer->clear();
        }
        delete strBuffer;
        strBuffer = nullptr;
    }

    StringBuffer::StringBuffer(int capacity) {
        if (capacity < 0) {
            throw std::invalid_argument("capacity must be non-negative");
        }
        strBuffer = new Buffer<char>(capacity);
    }

    StringBuffer::StringBuffer(const char *str) {
        strBuffer = new Buffer<char>(strlen(str));
        strBuffer->append(str, strlen(str));
    }

    StringBuffer::StringBuffer(const std::string &str) {
        strBuffer = new Buffer<char>(str.size());
        strBuffer->append(str.data(), str.size());
    }

    void StringBuffer::append(const char *str) const {
        strBuffer->append(str, strlen(str));
    }

    void StringBuffer::append(const char *str, const int offset, const int len) const {
        auto strLen = strlen(str);

        if (static_cast<size_t>(offset) > strLen || (offset + len) > strLen) {
            throw std::out_of_range("offset or length out of range");
        }
        if (offset >= 0 && static_cast<size_t>(offset) <= strLen) {
            strBuffer->append(str + offset, len);
        }
    }

    void StringBuffer::append(const std::string &str) const {
        strBuffer->append(str.c_str(), str.size());
    }

    void StringBuffer::append(const char &c) const {
        strBuffer->append(c);
    }

    void StringBuffer::clear() const {
        if (strBuffer) {
            strBuffer->clear();
            strBuffer->resize(0);
        }
    }

    std::string StringBuffer::convertEncoding(const std::string &input, const char *toEncoding) const {
        UErrorCode status{U_ZERO_ERROR};
        auto *conv = ucnv_open("shift_jis", &status);

        // 设置错误回调
        ucnv_setToUCallBack(conv, UCNV_TO_U_CALLBACK_STOP, nullptr, nullptr, nullptr, &status);
        if (U_FAILURE(status))
            return "";

        auto inputPtr = input.c_str();
        auto inputLen = input.size();
        auto outputCapacity = ucnv_getMaxCharSize(conv) * inputLen + 1;
        std::string output;
        output.reserve(outputCapacity);
        char *outputPtr = &output[0];

        ucnv_convertEx(conv, nullptr, &outputPtr, reinterpret_cast<const char *>(outputCapacity), &inputPtr,
                          reinterpret_cast<const char *>(inputLen), nullptr, nullptr, nullptr,
                          nullptr, true, true, &status);
        if (U_FAILURE(status))
            ucnv_close(conv);
        return output;
    }

    size_t StringBuffer::countWhiteSpace(const icu::StringPiece &input) const {
        const char *s = input.data();
        size_t length = input.length();
        size_t count = 0;
        for (int32_t i = 0; i < length;) {
            UChar32 c;
            U8_NEXT(s, i, length, c);
            if (u_isUWhiteSpace(c)) {
                ++count;
            }
        }
        return count;
    }
}
