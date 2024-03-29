#ifndef TINA_EXCEPTION_HPP
#define TINA_EXCEPTION_HPP

#include <exception>
#include <string>
#include <boost/exception/all.hpp>

namespace Tina {

	struct errinfo_file_name_tag;
	using errinfo_file_name = boost::error_info<errinfo_file_name_tag, std::string>;

	struct errinfo_line_number_tag;
	using errinfo_line_number = boost::error_info<errinfo_line_number_tag, int>;

	struct errinfo_comment_tag;
	using errinfo_comment = boost::error_info<errinfo_comment_tag, std::string>;

	// 基础异常类
	class Exception : public std::exception, public boost::exception {
	public:
		char const* what() const noexcept override;

		std::string lineInfo() const;

		std::string const* comment() const noexcept;
	};

	// 通用异常模板类
	template<typename T>
	class SimpleException : public Exception {
	public:
		const char* what() const noexcept override {
			return T::TAG();
		}
	};

	// 错误信息提供者
	struct RuntimeError {
		
		static constexpr const char* message() {
			return "";
		}
	};

	struct GlfwError {
		static constexpr const char* TAG() {
			return "Cannot initialize GLFW.";
		}
	};

	// 宏定义以便附加信息并抛出异常
#define THROW_SIMPLE_EXCEPTION(ErrorType, Description) \
    throw Tina::SimpleException<ErrorType>() \
        << Tina::errinfo_file_name(__FILE__) \
        << Tina::errinfo_line_number(__LINE__) \
        << Tina::errinfo_comment(Description)

}

#endif // TINA_EXCEPTION_HPP