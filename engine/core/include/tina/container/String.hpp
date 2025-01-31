#pragma once

#include <EASTL/string.h>

namespace tina::container
{

    class String {
    public:
        using Iterator = typename eastl::string::iterator;
        using ConstIterator = typename eastl::string::const_iterator;

        static const size_t npos = eastl::string::npos;

        String() = default;
        String(const String&) = default;
        String(String&&) noexcept = default;

        String(const char* str) : mString(str) {}
        String(const eastl::string& str) : mString(str) {}

        String& operator=(const String&) = default;
        String& operator=(String&&) noexcept = default;
        String& operator=(const char* str) {
            mString = str;
            return *this;
        }

        // Element access
        char& operator[](size_t index) { return mString[index]; }
        const char& operator[](size_t index) const { return mString[index]; }

        char& at(size_t index) { return mString.at(index); }
        const char& at(size_t index) const { return mString.at(index); }

        // String operations
        const char* c_str() const { return mString.c_str(); }
        const char* data() const { return mString.data(); }

        // Iterators
        Iterator begin() { return mString.begin(); }
        ConstIterator begin() const { return mString.begin(); }
        Iterator end() { return mString.end(); }
        ConstIterator end() const { return mString.end(); }

        // Capacity
        bool empty() const { return mString.empty(); }
        size_t size() const { return mString.size(); }
        size_t length() const { return mString.length(); }
        void reserve(size_t newCapacity) { mString.reserve(newCapacity); }
        size_t capacity() const { return mString.capacity(); }

        // Operations
        void clear() { mString.clear(); }
        String& append(const String& str) {
            mString.append(str.mString);
            return *this;
        }
        String& append(const char* str) {
            mString.append(str);
            return *this;
        }

        String substr(size_t pos = 0, size_t len = eastl::string::npos) const {
            return String(mString.substr(pos, len));
        }

        size_t find(const String& str, size_t pos = 0) const {
            return mString.find(str.mString, pos);
        }

        size_t find(const char* str, size_t pos = 0) const {
            return mString.find(str, pos);
        }

        // Operators
        String& operator+=(const String& str) {
            mString += str.mString;
            return *this;
        }

        String& operator+=(const char* str) {
            mString += str;
            return *this;
        }

        friend String operator+(String lhs, const String& rhs) {
            lhs += rhs;
            return lhs;
        }

        friend bool operator==(const String& lhs, const String& rhs) {
            return lhs.mString == rhs.mString;
        }

        friend bool operator!=(const String& lhs, const String& rhs) {
            return !(lhs == rhs);
        }

    private:
        eastl::string mString;
    };

}
