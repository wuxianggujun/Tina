#include <gtest/gtest.h>
#include "time/StopWatch.hpp"
#include <thread>

using namespace Tina;

TEST(StopWatchTest, TestGetElapsedMilliseconds)
{
    //Create and start a stopwatch
    Stopwatch my_watch;

    my_watch.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    my_watch.stop();

   auto str =  Stopwatch::format(my_watch.duration());

}
