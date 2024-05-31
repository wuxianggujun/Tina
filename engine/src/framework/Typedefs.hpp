//
// Created by wuxianggujun on 2024/5/31.
//

#ifndef TINA_TYPEDEFS_HPP
#define TINA_TYPEDEFS_HPP

// No discard allows the compiler to flag warnings if we don't use the return value of functions / classes
#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

// In some cases NO_DISCARD will get false positives,
// we can prevent the warning in specific cases by preceding the call with a cast.
#ifdef ALLOW_DISCARD
#define ALLOW_DISCARD (void)
#endif


// Generic ABS function,for math uses please use Math::abs.
template<typename T>
constexpr T ABS(T value) {
    return value < 0 ? -value : value;
}

template<typename T>
constexpr const T SIGN(const T value) {
    return value > 0 ? +1.0f : (value < 0 ? -1.0f : 0.0f);
}

template<typename T, typename T2>
constexpr auto MIN(const T a, const T2 b) {
    return a < b ? a : b;
}

template<typename T, typename T2>
constexpr auto MAX(const T a, const T2 b) {
    return a > b ? a : b;
}

template<typename T, typename T2, typename T3>
constexpr auto CLAMP(const T a, const T2 b, const T3 c) {
    return a < b ? b : (a > c ? c : a);
}


#endif //TINA_TYPEDEFS_HPP
