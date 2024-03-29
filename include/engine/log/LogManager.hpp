#ifndef TINA_LOGMANAGER_HPP
#define TINA_LOGMANAGER_HPP

namespace Tina::Manager {
	
	class LogManager {
	public:
		LogManager() = default;
		~LogManager() = default;

		void initialize();
		void shutdown();
	};
}


#endif // TINA_LOGMANAGER_HPP
