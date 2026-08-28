if(NOT DEFINED TINA_SDK_INCLUDE_DIR OR TINA_SDK_INCLUDE_DIR STREQUAL "")
    message(FATAL_ERROR "TINA_SDK_INCLUDE_DIR must name the installed SDK include directory")
endif()

cmake_path(ABSOLUTE_PATH TINA_SDK_INCLUDE_DIR NORMALIZE OUTPUT_VARIABLE tina_sdk_include_directory)
if(NOT IS_DIRECTORY "${tina_sdk_include_directory}/tina")
    message(FATAL_ERROR "Installed Tina headers were not found under ${tina_sdk_include_directory}")
endif()

file(GLOB_RECURSE tina_sdk_headers LIST_DIRECTORIES FALSE "${tina_sdk_include_directory}/tina/*.hpp")
if(NOT tina_sdk_headers)
    message(FATAL_ERROR "Installed Tina SDK contains no public headers")
endif()

if(DEFINED TINA_EXPECT_AUDIO_MINIAUDIO)
    set(tina_audio_miniaudio_headers
        "${tina_sdk_include_directory}/tina/audio/AudioDecode.hpp"
        "${tina_sdk_include_directory}/tina/audio/miniaudio/MiniaudioDevice.hpp"
    )
    foreach(tina_audio_miniaudio_header IN LISTS tina_audio_miniaudio_headers)
        if(TINA_EXPECT_AUDIO_MINIAUDIO AND NOT EXISTS "${tina_audio_miniaudio_header}")
            message(FATAL_ERROR "Installed AudioMiniaudio header is missing: ${tina_audio_miniaudio_header}")
        elseif(NOT TINA_EXPECT_AUDIO_MINIAUDIO AND EXISTS "${tina_audio_miniaudio_header}")
            message(FATAL_ERROR "AudioMiniaudio header leaked into an SDK package without that adapter")
        endif()
    endforeach()
endif()

set(tina_forbidden_patterns
    "#[ \t]*include[ \t]*[<\"](bgfx|GLFW|entt|box2d|miniaudio|freetype|ft2build|xxhash|cgltf|stb|tracy|X11|wayland|xcb)[/.>\"]"
    "bgfx::"
    "GLFWwindow"
    "entt::"
    "box2d::"
    "b2World"
    "ma_(engine|device|context)"
    "FT_(Face|Library)"
    "XXH[0-9]+_"
    "cgltf_"
    "stbi_"
    "tracy::"
    "Tracy[A-Za-z0-9_]*"
    "TRACY_[A-Za-z0-9_]+"
    "HWND[ \t]*[*&]"
    "wl_[A-Za-z0-9_]+[ \t]*[*&]"
    "xcb_[A-Za-z0-9_]+[ \t]*[*&]"
    # Platform socket types stay inside src/network.
    "#[ \t]*include[ \t]*[<\"](winsock2|ws2tcpip|sys/socket|netinet/in|arpa/inet|mbedtls|psa)[/.>\"]"
    "mbedtls_[A-Za-z0-9_]+"
    "MBEDTLS_[A-Za-z0-9_]+"
    "SOCKET[ \t]*[*&]"
    "sockaddr(_in|_in6|_storage)?[ \t]*[*&]"
)

foreach(tina_header IN LISTS tina_sdk_headers)
    file(READ "${tina_header}" tina_header_content)
    string(REGEX REPLACE "//[^\r\n]*" "" tina_header_content "${tina_header_content}")
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" tina_header_content "${tina_header_content}")
    foreach(tina_pattern IN LISTS tina_forbidden_patterns)
        if(tina_header_content MATCHES "${tina_pattern}")
            file(RELATIVE_PATH tina_relative_header "${tina_sdk_include_directory}" "${tina_header}")
            message(FATAL_ERROR
                "Installed SDK header ${tina_relative_header} exposes forbidden third-party token '${CMAKE_MATCH_0}'")
        endif()
    endforeach()
endforeach()

list(LENGTH tina_sdk_headers tina_sdk_header_count)
message(STATUS "Verified ${tina_sdk_header_count} installed Tina SDK headers without third-party includes/types")
