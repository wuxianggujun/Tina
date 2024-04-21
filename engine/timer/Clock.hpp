#ifndef TINA_TIMER_CLOCK_HPP
#define TINA_TIMER_CLOCK_HPP

#include <filesystem>
#include <boost/chrono.hpp>
#include <boost/noncopyable.hpp>
#include <boost/timer/timer.hpp>

namespace Tina {

    class Clock : public boost::noncopyable{

    public:
        static long long FileTimePointToTimeStamp(std::filesystem::file_time_type fileTime) {
           
        }

        static std::filesystem::file_time_type TimeStampToFileTimePoint(long long timeStamp) {

        }

    public:
        Clock() :m_deltaTime(0.0), m_elapsed{},m_startTime(boost::chrono::steady_clock::now()),
            m_lastTime(m_startTime),m_currentTime(m_startTime){

        }

        Clock(Clock&&) = default;
        Clock& operator=(Clock&&) = default;

        double elapsed(boost::timer::cpu_timer timer) {
            boost::timer::nanosecond_type elapsed = timer.elapsed().wall;
            double nanos = static_cast<double>(boost::chrono::nanoseconds(elapsed).count());
            return nanos * boost::chrono::nanoseconds::period::num / boost::chrono::nanoseconds::period::den;
        }

        void update() {
            m_lastTime = m_currentTime;
            m_currentTime = boost::chrono::steady_clock::now();
            m_elapsed = m_currentTime - m_lastTime;

            m_deltaTime = static_cast<float>(boost::chrono::duration_cast<boost::chrono::duration<double>>(m_elapsed).count());
        }

        float getFramerate() const {
            return 1.0f / m_deltaTime;
        }

        float getDeltaTime() const{
            return m_deltaTime;
        }

    private:
        float m_deltaTime;

        boost::chrono::duration<float> m_elapsed;
        boost::chrono::steady_clock::time_point m_startTime;
        boost::chrono::steady_clock::time_point m_lastTime;
        boost::chrono::steady_clock::time_point m_currentTime;
    };

}

#endif
