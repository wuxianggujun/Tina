set(TINA_NETWORK_HEADER_ISOLATION_SOURCES
    header_isolation/NetworkEndpointHeader.cpp
    header_isolation/NetworkErrorsHeader.cpp
    header_isolation/UdpSocketHeader.cpp
)

# Links only ProjectOptions and points at include/, so a public header that
# secretly needs a third-party include directory fails to compile here.
add_library(tina_network_header_isolation OBJECT
    ${TINA_NETWORK_HEADER_ISOLATION_SOURCES}
)
target_compile_features(tina_network_header_isolation PRIVATE cxx_std_23)
target_include_directories(tina_network_header_isolation PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(tina_network_header_isolation PRIVATE
    Tina::ProjectOptions
)
set_target_properties(tina_network_header_isolation PROPERTIES CXX_EXTENSIONS OFF)

add_executable(tina_network_tests
    IpAddressTests.cpp
    UdpSocketTests.cpp
    $<TARGET_OBJECTS:tina_network_header_isolation>
)
target_compile_features(tina_network_tests PRIVATE cxx_std_23)
target_link_libraries(tina_network_tests PRIVATE
    Tina::ProjectOptions
    Tina::Network
    GTest::gtest_main
)
set_target_properties(tina_network_tests PROPERTIES CXX_EXTENSIONS OFF)
