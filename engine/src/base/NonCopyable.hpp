#ifndef TINA_BASE_NONCOPYABLE_HPP
#define TINA_BASE_NONCOPYABLE_HPP

namespace TIna {
    class NonCopyable {
    public:
        NonCopyable(const NonCopyable &) = delete;

        NonCopyable &operator=(const NonCopyable &) = delete;

    protected:
        NonCopyable() = default;

        NonCopyable(NonCopyable &&) = default;

        NonCopyable &operator=(NonCopyable &&) = default;
    };
}


#endif //TINA_BASE_NONCOPYABLE_HPP
