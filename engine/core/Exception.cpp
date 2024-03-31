#include "Exception.hpp"

namespace Tina {

	char const* Exception::what() const noexcept
	{
		if (std::string const* cmt = comment())
		{
			return cmt->data();
		}

		return std::exception::what();
	}

	std::string Exception::lineInfo() const
	{
		char const* const* file = boost::get_error_info<boost::throw_file>(*this);
		int const* line = boost::get_error_info<boost::throw_line>(*this);
		std::string result;
		if (file)
		{
			result += *file;
		}
		result += ':';
		if (line)
		{
			result += std::to_string(*line);
		}
		return result;
	}

	std::string const* Exception::comment() const noexcept
	{
		return boost::get_error_info<errinfo_comment>(*this);
	}

}
