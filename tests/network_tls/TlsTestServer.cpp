#include "TlsTestServer.hpp"

#include <mbedtls/error.h>

#include <array>
#include <cstring>

namespace Tina::Tests {
namespace {

constexpr char PersonalisationLabel[] = "tina-tls-test-server";

// Generates a self-signed certificate for the given CN and returns its PEM.
// Shared by the server fixture and the "unrelated CA" helper so both are real
// certificates rather than fabricated base64.
[[nodiscard]] bool makeSelfSigned(
    std::string_view commonName,
    mbedtls_pk_context& key,
    mbedtls_ctr_drbg_context& drbg,
    std::string& pemOut,
    std::string& errorOut)
{
    const std::string subject = std::string{"CN="} + std::string{commonName};

    if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) {
        errorOut = "mbedtls_pk_setup failed";
        return false;
    }
    // secp256r1 rather than RSA: keygen is milliseconds instead of seconds, which
    // matters when every fixture makes a fresh key.
    if (mbedtls_ecp_gen_key(
            MBEDTLS_ECP_DP_SECP256R1,
            mbedtls_pk_ec(key),
            mbedtls_ctr_drbg_random,
            &drbg)
        != 0) {
        errorOut = "mbedtls_ecp_gen_key failed";
        return false;
    }

    mbedtls_x509write_cert writer{};
    mbedtls_x509write_crt_init(&writer);
    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer, &key);
    mbedtls_x509write_crt_set_issuer_key(&writer, &key);

    bool ok = true;
    if (mbedtls_x509write_crt_set_subject_name(&writer, subject.c_str()) != 0
        || mbedtls_x509write_crt_set_issuer_name(&writer, subject.c_str()) != 0) {
        errorOut = "failed to set certificate names";
        ok = false;
    }

    mbedtls_mpi serial{};
    mbedtls_mpi_init(&serial);
    if (ok && mbedtls_mpi_lset(&serial, 1) != 0) {
        errorOut = "failed to set serial";
        ok = false;
    }
    if (ok && mbedtls_x509write_crt_set_serial(&writer, &serial) != 0) {
        errorOut = "mbedtls_x509write_crt_set_serial failed";
        ok = false;
    }

    // A wide window so the suite does not start failing on a future date.
    if (ok
        && mbedtls_x509write_crt_set_validity(&writer, "20240101000000", "20440101000000")
            != 0) {
        errorOut = "mbedtls_x509write_crt_set_validity failed";
        ok = false;
    }
    if (ok
        && mbedtls_x509write_crt_set_basic_constraints(&writer, 1, 0) != 0) {
        errorOut = "mbedtls_x509write_crt_set_basic_constraints failed";
        ok = false;
    }

    if (ok) {
        std::array<unsigned char, 4096> buffer{};
        const int written = mbedtls_x509write_crt_pem(
            &writer,
            buffer.data(),
            buffer.size(),
            mbedtls_ctr_drbg_random,
            &drbg);
        if (written != 0) {
            errorOut = "mbedtls_x509write_crt_pem failed";
            ok = false;
        } else {
            pemOut.assign(reinterpret_cast<const char*>(buffer.data()));
        }
    }

    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&writer);
    return ok;
}

} // namespace

TlsTestServer::TlsTestServer()
{
    mbedtls_pk_init(&m_key);
    mbedtls_x509_crt_init(&m_certificate);
    mbedtls_ctr_drbg_init(&m_drbg);
    mbedtls_entropy_init(&m_entropy);
    mbedtls_ssl_init(&m_ssl);
    mbedtls_ssl_config_init(&m_config);

    const auto scope = Network::Detail::TransportScope::acquire();
    m_scoped = scope.has_value();

    if (!generateCredentials()) {
        return;
    }
    if (!startListening()) {
        return;
    }
    m_valid = true;
}

TlsTestServer::~TlsTestServer()
{
    closeAccepted();
    Network::Detail::closeNativeSocket(m_listen);
    m_listen = Network::Detail::InvalidNativeSocket;

    mbedtls_ssl_free(&m_ssl);
    mbedtls_ssl_config_free(&m_config);
    mbedtls_x509_crt_free(&m_certificate);
    mbedtls_pk_free(&m_key);
    mbedtls_ctr_drbg_free(&m_drbg);
    mbedtls_entropy_free(&m_entropy);

    if (m_scoped) {
        Network::Detail::TransportScope::release();
    }
}

bool TlsTestServer::generateCredentials()
{
    if (mbedtls_ctr_drbg_seed(
            &m_drbg,
            mbedtls_entropy_func,
            &m_entropy,
            reinterpret_cast<const unsigned char*>(PersonalisationLabel),
            sizeof(PersonalisationLabel) - 1)
        != 0) {
        m_lastError = "mbedtls_ctr_drbg_seed failed";
        return false;
    }

    if (!makeSelfSigned(
            TlsTestCertificates::ServerName,
            m_key,
            m_drbg,
            m_certificatePem,
            m_lastError)) {
        return false;
    }

    if (mbedtls_x509_crt_parse(
            &m_certificate,
            reinterpret_cast<const unsigned char*>(m_certificatePem.c_str()),
            m_certificatePem.size() + 1)
        != 0) {
        m_lastError = "failed to reparse the generated certificate";
        return false;
    }

    if (mbedtls_ssl_config_defaults(
            &m_config,
            MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT)
        != 0) {
        m_lastError = "mbedtls_ssl_config_defaults failed";
        return false;
    }
    mbedtls_ssl_conf_rng(&m_config, mbedtls_ctr_drbg_random, &m_drbg);
    if (mbedtls_ssl_conf_own_cert(&m_config, &m_certificate, &m_key) != 0) {
        m_lastError = "mbedtls_ssl_conf_own_cert failed";
        return false;
    }
    return true;
}

bool TlsTestServer::startListening()
{
    m_listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listen == Network::Detail::InvalidNativeSocket) {
        m_lastError = "socket() failed";
        return false;
    }
    (void)Network::Detail::setNativeSocketNonBlocking(m_listen);

    const Network::NetworkEndpoint local{Network::IpAddress::v4Loopback(), 0};
    sockaddr_storage address{};
    const auto length = Network::Detail::toNativeAddress(local, address);
    if (::bind(m_listen, reinterpret_cast<const sockaddr*>(&address), length) != 0) {
        m_lastError = "bind() failed";
        return false;
    }
    if (::listen(m_listen, 4) != 0) {
        m_lastError = "listen() failed";
        return false;
    }

    sockaddr_storage bound{};
    auto boundLength = static_cast<Network::Detail::NativeAddressLength>(sizeof(bound));
    if (::getsockname(m_listen, reinterpret_cast<sockaddr*>(&bound), &boundLength) != 0
        || !Network::Detail::fromNativeAddress(bound, m_endpoint)) {
        m_lastError = "getsockname() failed";
        return false;
    }
    return true;
}

void TlsTestServer::closeAccepted() noexcept
{
    Network::Detail::closeNativeSocket(m_accepted);
    m_accepted = Network::Detail::InvalidNativeSocket;
}

void TlsTestServer::pump()
{
    if (!m_valid) {
        return;
    }

    if (m_accepted == Network::Detail::InvalidNativeSocket) {
        const auto accepted = ::accept(m_listen, nullptr, nullptr);
        if (accepted == Network::Detail::InvalidNativeSocket) {
            return;
        }
        m_accepted = accepted;
        (void)Network::Detail::setNativeSocketNonBlocking(m_accepted);

        if (mbedtls_ssl_setup(&m_ssl, &m_config) != 0) {
            m_lastError = "mbedtls_ssl_setup failed";
            closeAccepted();
            return;
        }
        // Blocking-shaped callbacks over a non-blocking socket: WANT_READ tells
        // mbedTLS to retry rather than treating an empty read as end-of-stream.
        mbedtls_ssl_set_bio(
            &m_ssl,
            this,
            [](void* context, const unsigned char* buffer, size_t length) -> int {
                auto* self = static_cast<TlsTestServer*>(context);
#if defined(_WIN32)
                const int sent = ::send(
                    self->m_accepted,
                    reinterpret_cast<const char*>(buffer),
                    static_cast<int>(length),
                    0);
#else
                const ssize_t sent = ::send(self->m_accepted, buffer, length, 0);
#endif
                if (sent < 0) {
                    return Network::Detail::isWouldBlockError(
                               Network::Detail::lastSocketError())
                        ? MBEDTLS_ERR_SSL_WANT_WRITE
                        : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
                }
                return static_cast<int>(sent);
            },
            [](void* context, unsigned char* buffer, size_t length) -> int {
                auto* self = static_cast<TlsTestServer*>(context);
#if defined(_WIN32)
                const int got = ::recv(
                    self->m_accepted,
                    reinterpret_cast<char*>(buffer),
                    static_cast<int>(length),
                    0);
#else
                const ssize_t got = ::recv(self->m_accepted, buffer, length, 0);
#endif
                if (got < 0) {
                    return Network::Detail::isWouldBlockError(
                               Network::Detail::lastSocketError())
                        ? MBEDTLS_ERR_SSL_WANT_READ
                        : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
                }
                if (got == 0) {
                    return MBEDTLS_ERR_SSL_CONN_EOF;
                }
                return static_cast<int>(got);
            },
            nullptr);
        m_sessionReady = true;
    }

    if (!m_sessionReady) {
        return;
    }

    if (!m_handshakeDone) {
        const int result = mbedtls_ssl_handshake(&m_ssl);
        if (result == 0) {
            m_handshakeDone = true;
        } else if (
            result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            // A client that rejects the certificate aborts here; that is the
            // expected outcome of the negative tests, not a fixture failure.
            m_sessionReady = false;
        }
        // Falls through rather than returning: a close_notify requested while the
        // handshake was still finishing must still go out on this turn.
    }

    if (m_handshakeDone) {
        std::array<unsigned char, 4096> scratch{};
        const int read = mbedtls_ssl_read(&m_ssl, scratch.data(), scratch.size());
        if (read > 0) {
            m_request.append(
                reinterpret_cast<const char*>(scratch.data()),
                static_cast<Core::usize>(read));

            if (m_echo) {
                (void)mbedtls_ssl_write(&m_ssl, scratch.data(), static_cast<size_t>(read));
            }
        }

        if (!m_httpReply.empty() && !m_httpReplied
            && m_request.find("\r\n\r\n") != std::string::npos) {
            (void)mbedtls_ssl_write(
                &m_ssl,
                reinterpret_cast<const unsigned char*>(m_httpReply.data()),
                m_httpReply.size());
            m_httpReplied = true;
            // Closing delimits a reply with no Content-Length and ends the exchange
            // for one that has it.
            m_closeNotifyRequested = true;
        }
    }

    // Retried here rather than sent inline: on a non-blocking socket the alert can
    // report WANT_WRITE, and dropping it would leave the client unable to tell an
    // orderly shutdown from a truncated stream. Requires a live session, so it also
    // covers a close requested during the handshake.
    if (m_closeNotifyRequested && !m_closeNotifySent && m_handshakeDone) {
        const int result = mbedtls_ssl_close_notify(&m_ssl);
        if (result == 0) {
            m_closeNotifySent = true;
        } else if (
            result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
            // Nothing more can be done for it; stop retrying.
            m_closeNotifySent = true;
        }
    }
}

void TlsTestServer::closeNotify() noexcept
{
    // Latched unconditionally rather than gated on m_handshakeDone: a client
    // reports Connected once it has the server's Finished, which can be a pump
    // before the server's own handshake call returns. Gating here silently
    // dropped the request.
    m_closeNotifyRequested = true;
}

const std::string& unrelatedCertificatePem()
{
    // Built once: a second self-signed certificate that shares nothing with the
    // server's, so trusting it must fail the chain check.
    static const std::string pem = [] {
        mbedtls_pk_context key{};
        mbedtls_ctr_drbg_context drbg{};
        mbedtls_entropy_context entropy{};
        mbedtls_pk_init(&key);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_entropy_init(&entropy);

        std::string result;
        std::string error;
        constexpr char label[] = "tina-tls-test-unrelated";
        if (mbedtls_ctr_drbg_seed(
                &drbg,
                mbedtls_entropy_func,
                &entropy,
                reinterpret_cast<const unsigned char*>(label),
                sizeof(label) - 1)
            == 0) {
            (void)makeSelfSigned("unrelated.invalid", key, drbg, result, error);
        }

        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
        return result;
    }();
    return pem;
}

} // namespace Tina::Tests
