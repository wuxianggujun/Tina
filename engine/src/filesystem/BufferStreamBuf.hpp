//
// Created by wuxianggujun on 2024/7/31.
//

#ifndef TINA_FILESYSTEM_BUFFERSTREAMBUF_HPP
#define TINA_FILESYSTEM_BUFFERSTREAMBUF_HPP

#include "BufferAllocator.hpp"
#include <streambuf>
#include <iosfwd>
#include <ios>


namespace Tina
{
    template <typename ch, typename tr, typename ba = BufferAllocator<ch>>
    class BasicBufferStreamBuf : public std::basic_streambuf<ch, tr>
    {
    protected:
        typedef std::basic_streambuf<ch, tr> Base;
        typedef std::basic_ios<ch, tr> IOS;
        typedef ch charType;
        typedef tr traitsType;
        typedef ba allocatorType;
        typedef typename Base::int_type intType;
        typedef typename Base::pos_type posType;
        typedef typename Base::off_type offType;
        typedef typename IOS::openmode openMode;

    public:
        BasicBufferStreamBuf(std::streamsize bufferSize, openMode mode): bufferSize_(bufferSize),
                                                                         pBuffer_(allocatorType::allocate(bufferSize_)),
                                                                         mode_(mode)
        {
            this->setg(pBuffer_ + 4, pBuffer_ + 4, pBuffer_ + 4);
            this->setp(pBuffer_, pBuffer_ + (bufferSize_ - 1));
        }

        ~BasicBufferStreamBuf() override
        {
            allocatorType::deallocate(pBuffer_, bufferSize_);
        }

        intType overflow(intType c) override
        {
            if (!(mode_ & IOS::out)) return traitsType::eof();

            if (c != traitsType::eof())
            {
                *this->pptr() = traitsType::to_char_type(c);
                this->pbump(1);
            }
            if (flushBuffer() == std::streamsize(-1)) return traitsType::eof();

            return c;
        }

        intType underflow() override
        {
            if (!(mode_ & IOS::in)) return traitsType::eof();

            if (this->gptr() && (this->gptr() < this->egptr()))
                return traitsType::to_int_type(*this->gptr());

            int putback = int(this->gptr() - this->eback());
            if (putback > 4) putback = 4;

            traitsType::move(pBuffer_ + (4 - putback), this->gptr() - putback, putback);

            int n = readFromDevice(pBuffer_ + 4, bufferSize_ - 4);
            if (n <= 0) return traitsType::eof();

            this->setg(pBuffer_ + (4 - putback), pBuffer_ + 4, pBuffer_ + 4 + n);

            // return next character
            return traitsType::to_int_type(*this->gptr());
        }

        int sync() override
        {
            if (this->pptr() && this->pptr() > this->pbase())
            {
                if (flushBuffer() == -1) return -1;
            }
            return 0;
        }

    protected:
        void setMode(openMode mode)
        {
            mode_ = mode;
        }

        openMode getMode() const
        {
            return mode_;
        }

    private:
        virtual int readFromDevice(charType* buffer, std::streamsize length)
        {
            return 0;
        }

        virtual int writeToDevice(const charType* buffer, std::streamsize length)
        {
            return 0;
        }

        int flushBuffer()
        {
            int n = int(this->pptr() - this->pbase());
            if (writeToDevice(this->pbase(), n) == n)
            {
                this->pbump(-n);
                return n;
            }
            return -1;
        }


        std::streamsize bufferSize_;
        charType* pBuffer_;
        openMode mode_;

        BasicBufferStreamBuf(const BasicBufferStreamBuf& buf);
        BasicBufferStreamBuf& operator=(const BasicBufferStreamBuf& buf);
    };

    typedef BasicBufferStreamBuf<char, std::char_traits<char>> BufferStreamBuf;
}


#endif //TINA_FILESYSTEM_BUFFERSTREAMBUF_HPP
