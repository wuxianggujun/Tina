include_guard(GLOBAL)

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "linux-clang22-libstdcxx15.cmake may only be used from a Linux host")
endif()

set(TINA_CLANG22_C_COMPILER "" CACHE FILEPATH
    "Optional absolute path to the Clang 22 C compiler")
set(TINA_CLANG22_CXX_COMPILER "" CACHE FILEPATH
    "Optional absolute path to the Clang 22 C++ compiler")
set(TINA_GXX15_COMPILER "" CACHE FILEPATH
    "Optional absolute path to the GCC 15 C++ driver that owns libstdc++")

if(NOT TINA_CLANG22_C_COMPILER)
    find_program(TINA_CLANG22_C_COMPILER_DISCOVERED NAMES clang-22 REQUIRED)
    set(TINA_CLANG22_C_COMPILER "${TINA_CLANG22_C_COMPILER_DISCOVERED}"
        CACHE FILEPATH "Optional absolute path to the Clang 22 C compiler" FORCE)
endif()
if(NOT TINA_CLANG22_CXX_COMPILER)
    find_program(TINA_CLANG22_CXX_COMPILER_DISCOVERED NAMES clang++-22 REQUIRED)
    set(TINA_CLANG22_CXX_COMPILER "${TINA_CLANG22_CXX_COMPILER_DISCOVERED}"
        CACHE FILEPATH "Optional absolute path to the Clang 22 C++ compiler" FORCE)
endif()
if(NOT TINA_GXX15_COMPILER)
    find_program(TINA_GXX15_COMPILER_DISCOVERED NAMES g++-15 REQUIRED)
    set(TINA_GXX15_COMPILER "${TINA_GXX15_COMPILER_DISCOVERED}"
        CACHE FILEPATH "Optional absolute path to the GCC 15 C++ driver that owns libstdc++" FORCE)
endif()

execute_process(
    COMMAND "${TINA_CLANG22_CXX_COMPILER}" -dumpversion
    OUTPUT_VARIABLE TINA_CLANG22_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE TINA_CLANG22_VERSION_RESULT
)
if(NOT TINA_CLANG22_VERSION_RESULT EQUAL 0
   OR TINA_CLANG22_VERSION VERSION_LESS 22
   OR NOT TINA_CLANG22_VERSION VERSION_LESS 23)
    message(FATAL_ERROR
        "Tina requires Clang 22.x for this preset; found '${TINA_CLANG22_VERSION}'")
endif()

execute_process(
    COMMAND "${TINA_GXX15_COMPILER}" -dumpfullversion
    OUTPUT_VARIABLE TINA_GXX15_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE TINA_GXX15_VERSION_RESULT
)
if(NOT TINA_GXX15_VERSION_RESULT EQUAL 0
   OR TINA_GXX15_VERSION VERSION_LESS 15
   OR NOT TINA_GXX15_VERSION VERSION_LESS 16)
    message(FATAL_ERROR
        "Tina requires the GCC 15.x libstdc++ toolchain; found '${TINA_GXX15_VERSION}'")
endif()

execute_process(
    COMMAND "${TINA_GXX15_COMPILER}" -print-libgcc-file-name
    OUTPUT_VARIABLE TINA_GCC15_LIBGCC
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE TINA_GCC15_LIBGCC_RESULT
)
if(NOT TINA_GCC15_LIBGCC_RESULT EQUAL 0 OR NOT EXISTS "${TINA_GCC15_LIBGCC}")
    message(FATAL_ERROR
        "Unable to locate the GCC 15 installation through ${TINA_GXX15_COMPILER}")
endif()
get_filename_component(TINA_GCC15_INSTALL_DIR "${TINA_GCC15_LIBGCC}" DIRECTORY)

set(CMAKE_C_COMPILER "${TINA_CLANG22_C_COMPILER}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${TINA_CLANG22_CXX_COMPILER}" CACHE FILEPATH "" FORCE)

# Clang's compiler version does not select a C++ standard library version. Pin
# the GCC 15 installation explicitly so std::expected and
# std::move_only_function cannot silently regress to an older system libstdc++.
string(APPEND CMAKE_CXX_FLAGS_INIT
    " -stdlib=libstdc++ --gcc-install-dir=${TINA_GCC15_INSTALL_DIR}")

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    TINA_CLANG22_C_COMPILER
    TINA_CLANG22_CXX_COMPILER
    TINA_GXX15_COMPILER
)
