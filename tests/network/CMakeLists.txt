set(TINA_NETWORK_HEADER_ISOLATION_SOURCES
    header_isolation/ByteStreamHeader.cpp
    header_isolation/DnsResolverHeader.cpp
    header_isolation/HttpClientHeader.cpp
    header_isolation/NetworkEndpointHeader.cpp
    header_isolation/NetworkErrorsHeader.cpp
    header_isolation/TcpConnectionHeader.cpp
    header_isolation/TcpListenerHeader.cpp
    header_isolation/UdpSocketHeader.cpp
    header_isolation/WebSocketHeader.cpp
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
    HttpClientTests.cpp
    DnsResolverTests.cpp
    IpAddressTests.cpp
    ReadinessPollerTests.cpp
    TcpConnectionTests.cpp
    TcpListenerTests.cpp
    UdpSocketTests.cpp
    WebSocketTests.cpp
    $<TARGET_OBJECTS:tina_network_header_isolation>
)
target_compile_features(tina_network_tests PRIVATE cxx_std_23)
# The readiness layer is private to src/network because it names native socket
# types; testing it needs the module's own include root.
target_include_directories(tina_network_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/network
)
target_link_libraries(tina_network_tests PRIVATE
    Tina::ProjectOptions
    Tina::Network
    Tina::Task
    GTest::gtest_main
)
set_target_properties(tina_network_tests PROPERTIES CXX_EXTENSIONS OFF)
