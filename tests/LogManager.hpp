#ifndef TINA_LOGMANAGER_HPP
#define TINA_LOGMANAGER_HPP

#include <boost/utility.hpp>

namespace Tina::Manager {
	
	class LogManager final : boost::noncopyable{
	public:
		LogManager() = default;
		~LogManager() = default;

		void initialize();
		void shutdown();
	};
}


#endif // TINA_LOGMANAGER_HPP
