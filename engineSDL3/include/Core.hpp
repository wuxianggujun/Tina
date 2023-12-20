//
// Created by WuXiangGuJun on 2023/12/18.
//

/*
#pragma region Platform 和 #pragma endregion：这是用于Visual Studio的预处理指令，
用于将代码划分为指定的区域。在这里，它将接下来的代码划分为一个名为 "Platform" 的区域。
虽然这个指令对其他编译器没有实际作用，但它有助于提高代码的可读性。

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) || defined(__WIN32__) || defined(__NT__)：
这是一个条件编译指令，用于检查是否定义了 WIN32、_WIN32、__WIN32、WIN32 或 NT 这些宏。如果在 Windows 平台上编译，这些宏会被定义。

#define TINA_WINDOWS 1：如果满足上面的条件（即在 Windows 平台上编译），则定义一个名为 TINA_WINDOWS 的宏，并赋值为 1。
#elif defined(__linux__)：如果上面的条件不满足（即在非 Windows 平台上编译），并且定义了 linux 宏（表示在 Linux 平台上编译），则执行这里的代码。
#define TINA_LINUX 1：定义一个名为 TINA_LINUX 的宏，并赋值为 1。
#else：如果前面的条件都不满足（即不在 Windows 和 Linux 平台上编译），则执行这里的代码。
#error "TINA supports only Windows and Linux"：输出一个编译错误，提示 TINA 仅支持 Windows 和 Linux 平台。
#endif：结束条件编译指令。
#if defined(TINA_WINDOWS) && defined(TINA_LINUX)：检查是否同时定义了 TINA_WINDOWS 和 TINA_LINUX 这两个宏。如果在编译时同时定义了这两个宏，说明代码中存在错误。
#error "Error, both window and linux platforms are defined"：输出一个编译错误，提示同时定义了 Windows 和 Linux 平台。
#endif：结束条件编译指令。
*/



#ifndef CORE_HPP
#define CORE_HPP

#pragma region Platform

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) || defined(__WIN32__) || defined(__NT__)

#define TINA_WINDOWS 1
#elif defined(__linux__)
#define TINA_LINUX 1
#else
#error "TINA supports only Windows and Linux"
#endif

#if defined(TINA_WINDOWS) && defined(TINA_LINUX)
#error "Error,both window and linux platforms are defined"
#endif

#pragma endregion
#endif //CORE_HPP
