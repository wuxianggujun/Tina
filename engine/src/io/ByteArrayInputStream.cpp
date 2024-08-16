//
// Created by wuxianggujun on 2024/8/14.
//

#include "ByteArrayInputStream.hpp"

#include <utility>

namespace Tina {
    ByteArrayInputStream::ByteArrayInputStream(Bytes bytes) : buf_(bytes) {
    }

    ByteArrayInputStream::ByteArrayInputStream(Bytes bytes, size_t offset, size_t length): buf_(bytes) {
        buf_.append(bytes.begin() + offset, length);
    }

    void ByteArrayInputStream::close() {
        if (!buf_.empty()) {
            buf_.clear();
        }
    }

    Byte ByteArrayInputStream::read() {
        return buf_.read();
    }

   Scope<Buffer<Byte>> ByteArrayInputStream::read(size_t size) {
        auto result = std::make_unique<Buffer<Byte> >(size);
        buf_.read(result.get(), size);
        return result;
    }

    size_t ByteArrayInputStream::read(Buffer<Byte> *bytes, std::size_t off, std::size_t len) const {
        if (!bytes) {
            throw std::invalid_argument("Output buffer cannot be null");
        }
        buf_.read(bytes, off, len);
        return buf_.size();
    }

    size_t ByteArrayInputStream::transferTo(OutputStream &out) {
        
        return 0;
    }

    ByteArrayInputStream::~ByteArrayInputStream() {
        ByteArrayInputStream::close();
    }
} // Tina
