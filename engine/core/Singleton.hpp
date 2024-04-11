#include <boost/noncopyable.hpp>

namespace Tina {

template<class T>
    class Singleton : public boost::noncopyable
    {
     public:
        template<typename... Args>
        static T& instance(Args &&...args)
        {
            static T obj(std::forward<Args>(args)...);
            return obj;
        }
     protected:
            Singleton(const Singleton&) = delete;
            Singleton& operator=(const Singleton&) = delete;
            Singleton() = default;
            ~Singleton() = default;
    };

}
