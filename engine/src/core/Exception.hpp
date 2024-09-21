//
// Created by wuxianggujun on 2024/8/1.
//

#ifndef TINA_CORE_EXCEPTION_HPP
#define TINA_CORE_EXCEPTION_HPP

#include <stdexcept>

namespace Tina
{
    class Exception : public std::exception
    {
    public:
        explicit Exception(const std::string& msg, int code = 0);

        Exception(const std::string& msg, const std::string& arg, int code = 0);

        Exception(const std::string& msg, const Exception& nested, int code = 0);

        Exception(const Exception& other);

        ~Exception() noexcept;

        Exception& operator=(const Exception& other);

        virtual const char* name() const noexcept;

        virtual const char* className() const noexcept;

        virtual const char* what() const noexcept;

        const Exception* nested() const;

        const std::string& message() const;

        int code() const;

        std::string displayText() const;

        virtual Exception* clone() const;

        virtual void rethrow() const;

    protected:
        Exception(int code = 0);

        void message(const std::string& msg);

        void exendedMessage(const std::string& msg);

    private:
        std::string m_message;
        Exception* m_previous;
        int m_code;
    };

    inline const Exception* Exception::nested() const
    {
        return m_previous;
    }

    inline const std::string& Exception::message() const
    {
        return m_message;
    }

    inline void Exception::message(const std::string& msg)
    {
        m_message = msg;
    }

    inline int Exception::code() const
    {
        return m_code;
    }


#define TINA_DECLARE_EXCEPTION_CODE(CLS, BASE, CODE) \
	class CLS: public BASE														\
	{																				\
	public:																			\
		CLS(int code = CODE);														\
		CLS(const std::string& msg, int code = CODE);								\
		CLS(const std::string& msg, const std::string& arg, int code = CODE);		\
		CLS(const std::string& msg, const Tina::Exception& exc, int code = CODE);	\
		CLS(const CLS& exc);														\
		~CLS() noexcept;															\
		CLS& operator = (const CLS& exc);											\
		const char* name() const noexcept;											\
		const char* className() const noexcept;										\
		Tina::Exception* clone() const;												\
		void rethrow() const;														\
	};

#define TINA_DECLARE_EXCEPTION(CLS, BASE) \
	TINA_DECLARE_EXCEPTION_CODE(CLS, BASE, 0)

#define TINA_IMPLEMENT_EXCEPTION(CLS, BASE, NAME)													\
	CLS::CLS(int code): BASE(code)																	\
	{																								\
	}																								\
	CLS::CLS(const std::string& msg, int code): BASE(msg, code)										\
	{																								\
	}																								\
	CLS::CLS(const std::string& msg, const std::string& arg, int code): BASE(msg, arg, code)		\
	{																								\
	}																								\
	CLS::CLS(const std::string& msg, const Tina::Exception& exc, int code): BASE(msg, exc, code)	\
	{																								\
	}																								\
	CLS::CLS(const CLS& exc): BASE(exc)																\
	{																								\
	}																								\
	CLS::~CLS() noexcept																			\
	{																								\
	}																								\
	CLS& CLS::operator = (const CLS& exc)															\
	{																								\
		BASE::operator = (exc);																		\
		return *this;																				\
	}																								\
	const char* CLS::name() const noexcept															\
	{																								\
		return NAME;																				\
	}																								\
	const char* CLS::className() const noexcept														\
	{																								\
		return typeid(*this).name();																\
	}																								\
	Tina::Exception* CLS::clone() const																\
	{																								\
		return new CLS(*this);																		\
	}																								\
	void CLS::rethrow() const																		\
	{																								\
		throw *this;																				\
	}

    TINA_DECLARE_EXCEPTION(LogicException, Exception)

    TINA_DECLARE_EXCEPTION(NullPointerException, LogicException)


    TINA_DECLARE_EXCEPTION(RuntimeException, Exception)

    TINA_DECLARE_EXCEPTION(NotFoundException, RuntimeException)


    TINA_DECLARE_EXCEPTION(IOException, RuntimeException)

    TINA_DECLARE_EXCEPTION(FileException, IOException)

    TINA_DECLARE_EXCEPTION(FileNotFoundException, FileException)
}


#endif //TINA_CORE_EXCEPTION_HPP
