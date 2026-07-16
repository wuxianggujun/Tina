#pragma once

#include <tina/core/base/Types.hpp>

namespace Tina::Core {

enum class OperatingSystem : u8 {
    Windows,
    Linux,
    MacOS,
    Unknown,
};

enum class Compiler : u8 {
    Msvc,
    Clang,
    Gcc,
    Unknown,
};

enum class Architecture : u8 {
    X86,
    X64,
    Arm32,
    Arm64,
    Unknown,
};

#if defined(_WIN32)
inline constexpr OperatingSystem CurrentOperatingSystem = OperatingSystem::Windows;
#elif defined(__APPLE__)
inline constexpr OperatingSystem CurrentOperatingSystem = OperatingSystem::MacOS;
#elif defined(__linux__)
inline constexpr OperatingSystem CurrentOperatingSystem = OperatingSystem::Linux;
#else
inline constexpr OperatingSystem CurrentOperatingSystem = OperatingSystem::Unknown;
#endif

#if defined(_MSC_VER) && !defined(__clang__)
inline constexpr Compiler CurrentCompiler = Compiler::Msvc;
#elif defined(__clang__)
inline constexpr Compiler CurrentCompiler = Compiler::Clang;
#elif defined(__GNUC__)
inline constexpr Compiler CurrentCompiler = Compiler::Gcc;
#else
inline constexpr Compiler CurrentCompiler = Compiler::Unknown;
#endif

#if defined(_M_X64) || defined(__x86_64__)
inline constexpr Architecture CurrentArchitecture = Architecture::X64;
#elif defined(_M_IX86) || defined(__i386__)
inline constexpr Architecture CurrentArchitecture = Architecture::X86;
#elif defined(_M_ARM64) || defined(__aarch64__)
inline constexpr Architecture CurrentArchitecture = Architecture::Arm64;
#elif defined(_M_ARM) || defined(__arm__)
inline constexpr Architecture CurrentArchitecture = Architecture::Arm32;
#else
inline constexpr Architecture CurrentArchitecture = Architecture::Unknown;
#endif

inline constexpr u32 ProcessBitCount = static_cast<u32>(sizeof(void*) * 8U);
inline constexpr bool IsWindows = CurrentOperatingSystem == OperatingSystem::Windows;
inline constexpr bool IsLinux = CurrentOperatingSystem == OperatingSystem::Linux;
inline constexpr bool IsMacOS = CurrentOperatingSystem == OperatingSystem::MacOS;

} // namespace Tina::Core
