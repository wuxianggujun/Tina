//
// Created by 33442 on 2024/3/14.
//

#include "Log.hpp"
#include <spdlog/pattern_formatter.h>

namespace Tina
{

    std::atomic<bool> Logger::writeToConsole_{true};
    std::atomic<bool> Logger::writeToFile_{true};

    class co_formatter_flag : public spdlog::custom_flag_formatter
    {
    public:
        void format(const spdlog::details::log_msg& msg, const std::tm& tm_time, spdlog::memory_buf_t& dest) override
        {
            std::string coID = std::to_string();

        }
    };


} // TINA
