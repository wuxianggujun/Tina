#ifndef TINA_LOG_HPP
#define TINA_LOG_HPP

#include <spdlog/spdlog.h>

#define TINA_DEFAULT_LOGGER_NAME "TinaLogger"

#ifdef TINA_CONFIG_RELEASE
#define TINA_TRACE(...) if (spdlog::get(TINA_DEFAULT_LOGGER_NAME) != nullptr) {spdlog::get(TINA_DEFAULT_LOGGER_NAME)->
trace(__VA_ARGS__); }
#else
#define TINA_TRACE(...) (void)0

#endif // TINA_CONFIG_RELEASE

#endif //TINA_LOG_HPP

