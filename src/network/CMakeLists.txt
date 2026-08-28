set(TINA_NETWORK_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/network/NetworkEndpoint.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/network/HttpClient.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/network/NetworkErrors.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/network/TcpConnection.hpp
    ${PROJECT_SOURCE_DIR}/include/tina/network/UdpSocket.hpp
)

add_library(tina_network STATIC
    ${TINA_NETWORK_PUBLIC_HEADERS}
    HttpClient.cpp
    NetworkEndpoint.cpp
    TcpConnection.cpp
    UdpSocket.cpp
    detail/NativeSocket.cpp
    detail/ReadinessPoller.cpp
)
add_library(Tina::Network ALIAS tina_network)

target_compile_features(tina_network PUBLIC cxx_std_23)
target_include_directories(tina_network PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tina_network
    PUBLIC Tina::Core
    PRIVATE "$<BUILD_INTERFACE:Tina::ProjectOptions>"
)

# Winsock is the platform transport on Windows; POSIX needs no extra library.
if (WIN32)
    target_link_libraries(tina_network PRIVATE ws2_32)
endif ()

set_target_properties(tina_network PROPERTIES CXX_EXTENSIONS OFF)
