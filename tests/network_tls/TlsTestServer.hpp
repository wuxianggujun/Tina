#pragma once

// An in-process TLS server for handshake tests, plus the certificate it presents.
//
// The key pair and certificate are generated at construction rather than embedded
// as PEM literals. That keeps a private key out of source control entirely -- a
// committed test key is still a committed private key, and it also acquires an
// expiry date that eventually breaks the suite. secp256r1 keygen is fast enough
// to do per fixture.
//
// The server is non-blocking and pumped from the test thread, so a stalled
// handshake surfaces as a failed assertion instead of a hung process.

#include "detail/NativeSocket.hpp"

#include <tina/core/base/Types.hpp>
#include <tina/network/NetworkEndpoint.hpp>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>

#include <string>
#include <string_view>

namespace Tina::Tests {

namespace TlsTestCertificates {
// Must match the certificate's CN, or hostname verification fails by design.
inline constexpr std::string_view ServerName = "tina.test";
} // namespace TlsTestCertificates

class TlsTestServer final {
  public:
    TlsTestServer();
    ~TlsTestServer();

    TlsTestServer(const TlsTestServer&) = delete;
    TlsTestServer& operator=(const TlsTestServer&) = delete;

    [[nodiscard]] bool isValid() const noexcept { return m_valid; }
    [[nodiscard]] const std::string& lastError() const noexcept { return m_lastError; }
    [[nodiscard]] const Network::NetworkEndpoint& endpoint() const noexcept
    {
        return m_endpoint;
    }

    // PEM of the self-signed certificate this server presents. A client trusting
    // it verifies successfully; a client trusting anything else must not.
    [[nodiscard]] const std::string& certificatePem() const noexcept
    {
        return m_certificatePem;
    }

    // Echoes application data back to the client.
    void setEcho(bool value) noexcept { m_echo = value; }

    // Replies with these bytes verbatim once a request terminator is seen, so an
    // HTTP exchange can run over the encrypted stream.
    void setHttpReply(std::string reply) { m_httpReply = std::move(reply); }

    [[nodiscard]] const std::string& observedRequest() const noexcept { return m_request; }

    // Accepts, advances the handshake, and moves application data. Safe to call
    // before a client connects.
    void pump();

    // Sends close_notify, which is what lets a client tell an orderly shutdown
    // from a truncated stream.
    void closeNotify() noexcept;

    // True once close_notify has actually left. Asserting on this separates "the
    // fixture never sent it" from "the client failed to notice".
    [[nodiscard]] bool closeNotifySent() const noexcept { return m_closeNotifySent; }

  private:
    [[nodiscard]] bool generateCredentials();
    [[nodiscard]] bool startListening();
    void closeAccepted() noexcept;

    bool m_valid = false;
    bool m_scoped = false;
    std::string m_lastError;

    Network::Detail::NativeSocket m_listen = Network::Detail::InvalidNativeSocket;
    Network::Detail::NativeSocket m_accepted = Network::Detail::InvalidNativeSocket;
    Network::NetworkEndpoint m_endpoint{};

    mbedtls_pk_context m_key{};
    mbedtls_x509_crt m_certificate{};
    mbedtls_ctr_drbg_context m_drbg{};
    mbedtls_entropy_context m_entropy{};
    mbedtls_ssl_context m_ssl{};
    mbedtls_ssl_config m_config{};

    std::string m_certificatePem;
    std::string m_request;
    std::string m_httpReply;
    bool m_sessionReady = false;
    bool m_handshakeDone = false;
    bool m_httpReplied = false;
    bool m_echo = false;
    // close_notify can report WANT_WRITE on a non-blocking socket, so the request
    // is latched and retried from pump() until it actually leaves.
    bool m_closeNotifyRequested = false;
    bool m_closeNotifySent = false;
};

// A second, unrelated self-signed certificate. Used as a trust anchor that must
// fail to verify the server, proving the chain check is real.
[[nodiscard]] const std::string& unrelatedCertificatePem();

} // namespace Tina::Tests
