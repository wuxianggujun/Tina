set(TINA_NETWORK_TLS_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/tina/network/tls/TlsConnection.hpp
)

add_library(tina_network_tls STATIC
    ${TINA_NETWORK_TLS_PUBLIC_HEADERS}
    TlsConnection.cpp
)
add_library(Tina::NetworkTls ALIAS tina_network_tls)

target_compile_features(tina_network_tls PUBLIC cxx_std_23)
target_include_directories(tina_network_tls PUBLIC ${PROJECT_SOURCE_DIR}/include)

# mbedTLS is PRIVATE: its types never appear in include/tina, so a consumer of the
# Game SDK never sees them.
target_link_libraries(tina_network_tls
    PUBLIC Tina::Network
    PRIVATE
        "$<BUILD_INTERFACE:Tina::ProjectOptions>"
        MbedTLS::mbedtls
        MbedTLS::mbedx509
        MbedTLS::mbedcrypto
)

set_target_properties(tina_network_tls PROPERTIES CXX_EXTENSIONS OFF)
