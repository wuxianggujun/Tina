#ifndef TINA_TIMER_CLOCK_HPP
#define TINA_TIMER_CLOCK_HPP

#include <boost/chrono.hpp>
#include <boost/noncopyable.hpp>
#include <boost/timer/timer.hpp>

namespace Tina {

    class Clock : public boost::noncopyable{


    public:
        Clock();

        Clock(Clock&&) = default;
        Clock& operator=(Clock&&) = default;

        double elapsed(boost::timer::cpu_timer timer) {
            boost::timer::nanosecond_type elapsed = timer.elapsed().wall;
            double nanos = static_cast<double>(boost::chrono::nanoseconds(elapsed).count());
            return nanos * boost::chrono::nanoseconds::period::num / boost::chrono::nanoseconds::period::den;
        }

        float getDeltaTime() {
            return 1.0f;
        }

    private:
        boost::timer::auto_cpu_timer autoCpuTimer;
        boost::timer::cpu_timer timer;
    };

}

#endif
