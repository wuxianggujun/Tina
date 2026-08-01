cmake_minimum_required(VERSION 3.25)
project(TinaSdkMissingComponentConsumer LANGUAGES NONE)

if(NOT DEFINED TINA_EXPECT_MISSING_COMPONENTS OR TINA_EXPECT_MISSING_COMPONENTS STREQUAL "")
    message(FATAL_ERROR "TINA_EXPECT_MISSING_COMPONENTS must name at least one unavailable component")
endif()

find_package(Tina 0.0.1 CONFIG COMPONENTS ${TINA_EXPECT_MISSING_COMPONENTS})
if(Tina_FOUND)
    message(FATAL_ERROR
        "The Tina package unexpectedly accepted missing components: ${TINA_EXPECT_MISSING_COMPONENTS}")
endif()

foreach(tina_component IN LISTS TINA_EXPECT_MISSING_COMPONENTS)
    if(Tina_${tina_component}_FOUND)
        message(FATAL_ERROR "Missing Tina component ${tina_component} is marked as found")
    endif()
    if(TARGET Tina::${tina_component})
        message(FATAL_ERROR "Missing Tina component unexpectedly defines target Tina::${tina_component}")
    endif()
endforeach()

if(Tina_PlatformGlfw_FOUND OR TARGET Tina::PlatformGlfw OR Tina_ADAPTER_TARGETS)
    message(FATAL_ERROR "An unrequested PlatformGlfw adapter leaked into the missing-component probe")
endif()
