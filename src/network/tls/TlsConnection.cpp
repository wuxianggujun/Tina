#include <tina/network/tls/TlsConnection.hpp>

#include <tina/core/base/ScopeExit.hpp>
#include "SystemTrustStore.hpp"

#include <tina/network/NetworkErrors.hpp>
#include <tina/network/TcpConnection.hpp>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace Tina::Network {
namespace {

// Identifies this session to the RNG personalisation input. Not a secret.
constexpr char PersonalisationLabel[] = "tina-tls-client";

} // namespace

struct TlsConnection::Impl final {
    Impl(std::pmr::memory_resource& resource, TcpConnection transportIn)
        : transport(std::move(transportIn))
        , plaintextSend(&resource)
        , plaintextReceive(&resource)
        , serverName(&resource)
    {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&config);
        mbedtls_x509_crt_init(&trustAnchors);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_entropy_init(&entropy);
    }

    ~Impl()
    {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&config);
        mbedtls_x509_crt_free(&trustAnchors);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // Owns the TCP transport outright: TLS is a layer over it, not a peer of it,
    // so there is no way for a caller to drive them out of step.
    TcpConnection transport;

    std::thread::id owner{};
    TlsConnectionState state = TlsConnectionState::ConnectingTransport;

    mbedtls_ssl_context ssl{};
    mbedtls_ssl_config config{};
    mbedtls_x509_crt trustAnchors{};
    mbedtls_ctr_drbg_context drbg{};
    mbedtls_entropy_context entropy{};

    // Plaintext queues. Ciphertext lives in the TCP connection's own buffers, so
    // there is exactly one copy of the encrypted bytes.
    std::pmr::vector<std::byte> plaintextSend;
    std::pmr::vector<std::byte> plaintextReceive;
    Core::usize sendPending = 0;
    Core::usize receivePending = 0;
    // close_notify may require multiple WANT_WRITE attempts. Starting it forbids
    // later application records; sent becomes true only once mbedTLS has handed
    // the complete alert to the TCP transport.
    bool shutdownStarted = false;
    bool shutdownSent = false;

    // mbedTLS keeps a borrowed pointer to the SNI string, so it must outlive the
    // session rather than the config argument.
    std::pmr::string serverName;

    TlsConnectionStatistics stats{};

    [[nodiscard]] bool isOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == owner;
    }

    // mbedTLS asks for ciphertext through this. Returning WANT_READ rather than 0
    // is essential: 0 would mean end-of-stream and abort the session, whereas the
    // transport merely has nothing buffered yet.
    static int transportRecv(void* context, unsigned char* buffer, size_t length) noexcept
    {
        auto* self = static_cast<Impl*>(context);
        auto available = self->transport.receive();
        if (!available) {
            return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        }
        if (available->empty()) {
            const auto transportState = self->transport.state();
            if (transportState == TcpConnectionState::PeerClosed) {
                // A real end-of-stream: the peer closed without close_notify.
                return 0;
            }
            if (transportState == TcpConnectionState::Failed
                || transportState == TcpConnectionState::Closed) {
                // The NET_* error codes live in mbedtls/net_sockets.h, which is
                // mbedTLS's own socket layer -- including it would drag in the very
                // thing these callbacks exist to replace. The SSL-level codes carry
                // the same meaning to the caller of mbedtls_ssl_read.
                return MBEDTLS_ERR_SSL_CONN_EOF;
            }
            return MBEDTLS_ERR_SSL_WANT_READ;
        }

        const Core::usize take = (std::min)(length, available->size());
        std::memcpy(buffer, available->data(), take);
        if (const auto status = self->transport.consume(take); !status) {
            return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        }
        return static_cast<int>(take);
    }

    // mbedTLS hands ciphertext here. A full transport queue is WANT_WRITE, not an
    // error: the same record is retried once pump() drains the socket.
    static int transportSend(void* context, const unsigned char* buffer, size_t length) noexcept
    {
        auto* self = static_cast<Impl*>(context);
        if (length > static_cast<size_t>(INT_MAX)) {
            return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;
        }
        const auto transportState = self->transport.state();
        if (transportState == TcpConnectionState::Failed
            || transportState == TcpConnectionState::Closed) {
            return MBEDTLS_ERR_SSL_CONN_EOF;
        }

        const auto payload = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(buffer),
            length};
        const auto status = self->transport.send(payload);
        if (!status) {
            if (status.error().code == NetworkErrorCode::CapacityExceeded) {
                // The queue is full, not broken: mbedTLS retries this record on a
                // later pump once the socket drains.
                return MBEDTLS_ERR_SSL_WANT_WRITE;
            }
            return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
        }
        return static_cast<int>(length);
    }

    void markFailed() noexcept
    {
        // TLS owns the TCP transport. Closing it at the failure boundary prevents
        // a failed handshake/record layer from leaving a live socket behind.
        transport.close();
        state = TlsConnectionState::Failed;
        sendPending = 0;
        stats.queuedSendBytes = 0;
    }
};

TlsConnection::TlsConnection(Impl* impl) noexcept
    : m_impl(impl)
{
}

TlsConnection::TlsConnection(TlsConnection&& other) noexcept
    : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

TlsConnection::~TlsConnection() noexcept
{
    delete m_impl;
    m_impl = nullptr;
}

Core::Result<TlsConnection> TlsConnection::Create(TlsConnectionConfig config)
{
    if (config.sendBufferBytes == 0 || config.receiveBufferBytes == 0) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "TlsConnection send and receive buffer sizes must be greater than zero");
    }
    if (config.serverName.size() > 253
        || std::ranges::any_of(config.serverName, [](char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte <= 0x20U || byte == 0x7FU;
        })) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "TLS serverName is too long or contains whitespace/control bytes");
    }
    if (config.trustAnchorsPem.find('\0') != std::string_view::npos) {
        return Core::failure(
            NetworkErrorCode::InvalidConfiguration,
            "TLS trust anchors contain an embedded NUL");
    }

    if (config.verification == TlsVerificationMode::InsecureSkipVerify) {
#if defined(NDEBUG)
        return Core::failure(
            NetworkErrorCode::TlsInsecureConfigurationRejected,
            "TLS certificate verification cannot be skipped in a release build");
#else
        if (!config.allowInsecureVerification) {
            return Core::failure(
                NetworkErrorCode::TlsInsecureConfigurationRejected,
                "Skipping TLS verification also requires allowInsecureVerification");
        }
#endif
    } else {
        // Without a name there is nothing to match a certificate against, so
        // "Required" would verify a chain but not that it belongs to this peer.
        if (config.serverName.empty()) {
            return Core::failure(
                NetworkErrorCode::InvalidConfiguration,
                "TLS verification requires a serverName to match the certificate");
        }
    }

    std::pmr::memory_resource* resource = config.memoryResource != nullptr
        ? config.memoryResource
        : std::pmr::get_default_resource();

    TcpConnectionConfig transportConfig{};
    transportConfig.remoteEndpoint = config.remoteEndpoint;
    transportConfig.sendBufferBytes = config.sendBufferBytes;
    transportConfig.receiveBufferBytes = config.receiveBufferBytes;
    transportConfig.kernelSendBufferBytes = config.kernelSendBufferBytes;
    transportConfig.kernelReceiveBufferBytes = config.kernelReceiveBufferBytes;
    transportConfig.memoryResource = resource;

    auto transport = TcpConnection::Create(transportConfig);
    if (!transport) {
        return Core::failure(std::move(transport.error()));
    }

    Impl* impl = nullptr;
    try {
        impl = new Impl{*resource, std::move(*transport)};
        impl->plaintextSend.resize(config.sendBufferBytes);
        impl->plaintextReceive.resize(config.receiveBufferBytes);
        impl->serverName.assign(config.serverName);
    } catch (const std::bad_alloc&) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::AllocationFailed,
            "TlsConnection fixed buffer allocation failed");
    } catch (...) {
        delete impl;
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "TlsConnection construction failed");
    }

    auto implGuard = Core::makeScopeExit([&impl]() noexcept { delete impl; });

    impl->owner = std::this_thread::get_id();

    if (mbedtls_ctr_drbg_seed(
            &impl->drbg,
            mbedtls_entropy_func,
            &impl->entropy,
            reinterpret_cast<const unsigned char*>(PersonalisationLabel),
            sizeof(PersonalisationLabel) - 1)
        != 0) {
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "Failed to seed the TLS random number generator");
    }

    if (mbedtls_ssl_config_defaults(
            &impl->config,
            MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT)
        != 0) {
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "Failed to apply TLS client defaults");
    }

    if (config.verification == TlsVerificationMode::Required) {
        // Explicit anchors replace the platform set rather than extending it, so
        // pinning a private CA does not leave every public one trusted too.
        const bool useSystemStore = config.trustAnchorsPem.empty();

        // mbedtls_x509_crt_parse counts the terminator, so the explicit path needs
        // an owning copy. The system path already holds a NUL-terminated
        // process-lifetime string and is pointed at rather than copied -- that
        // string can be a few hundred KB.
        try {
            std::string owned;
            if (!useSystemStore) {
                owned.assign(config.trustAnchorsPem);
            }
            const std::string* anchors =
                useSystemStore ? &Detail::systemTrustAnchorsPem() : &owned;

            if (anchors->empty()) {
                // Nothing to chain to. Distinguished from a parse failure because
                // the fix is different: this platform has no readable store.
                return Core::failure(
                    NetworkErrorCode::InvalidConfiguration,
                    useSystemStore
                        ? "TLS verification found no platform trust anchors; supply "
                          "trustAnchorsPem for this platform"
                        : "TLS verification requires at least one PEM trust anchor");
            }

            const int parsed = mbedtls_x509_crt_parse(
                &impl->trustAnchors,
                reinterpret_cast<const unsigned char*>(anchors->c_str()),
                anchors->size() + 1);
            // A positive return means some entries failed. A platform snapshot
            // routinely contains one legacy entry mbedTLS cannot parse, but an
            // explicit caller bundle is a precise configuration and must parse
            // completely rather than silently weakening its intended anchors.
            const bool parseFailed = parsed < 0 || impl->trustAnchors.version == 0
                || (!useSystemStore && parsed != 0);
            if (parseFailed) {
                return Core::failure(
                    NetworkErrorCode::InvalidConfiguration,
                    "Failed to parse the PEM trust anchors");
            }
        } catch (const std::bad_alloc&) {
            return Core::failure(
                NetworkErrorCode::AllocationFailed,
                "TLS trust anchor allocation failed");
        } catch (...) {
            return Core::failure(
                NetworkErrorCode::ConstructionFailed,
                "TLS trust store loading failed");
        }
        mbedtls_ssl_conf_authmode(&impl->config, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&impl->config, &impl->trustAnchors, nullptr);
    } else {
        mbedtls_ssl_conf_authmode(&impl->config, MBEDTLS_SSL_VERIFY_NONE);
    }

    mbedtls_ssl_conf_rng(&impl->config, mbedtls_ctr_drbg_random, &impl->drbg);

    if (mbedtls_ssl_setup(&impl->ssl, &impl->config) != 0) {
        return Core::failure(
            NetworkErrorCode::ConstructionFailed,
            "Failed to set up the TLS session");
    }

    if (!impl->serverName.empty()) {
        if (mbedtls_ssl_set_hostname(&impl->ssl, impl->serverName.c_str()) != 0) {
            return Core::failure(
                NetworkErrorCode::ConstructionFailed,
                "Failed to set the TLS server name");
        }
    }

    // This is what keeps mbedTLS off the socket: it moves bytes only through these
    // callbacks, which read and write buffers the caller pumps.
    mbedtls_ssl_set_bio(
        &impl->ssl,
        impl,
        &Impl::transportSend,
        &Impl::transportRecv,
        nullptr);

    implGuard.release();
    return TlsConnection{impl};
}

TlsConnectionState TlsConnection::state() const noexcept
{
    if (m_impl == nullptr) {
        return TlsConnectionState::Closed;
    }
    return m_impl->state;
}

Core::Result<NetworkEndpoint> TlsConnection::localEndpoint() const noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TlsConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TlsConnection must be used from its owner thread");
    }
    return m_impl->transport.localEndpoint();
}

Core::Result<NetworkEndpoint> TlsConnection::remoteEndpoint() const noexcept
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TlsConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TlsConnection must be used from its owner thread");
    }
    return m_impl->transport.remoteEndpoint();
}

Core::Status TlsConnection::send(std::span<const std::byte> payload)
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TlsConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TlsConnection must be used from its owner thread");
    }
    if (payload.empty()) {
        return Core::failure(
            NetworkErrorCode::InvalidDatagram,
            "TlsConnection cannot send an empty payload");
    }
    if (m_impl->state == TlsConnectionState::Closed) {
        return Core::failure(NetworkErrorCode::ConnectionClosed, "TlsConnection is closed");
    }
    if (m_impl->state == TlsConnectionState::Failed) {
        return Core::failure(NetworkErrorCode::ConnectionFailed, "TlsConnection has failed");
    }
    if (m_impl->state == TlsConnectionState::PeerClosed) {
        return Core::failure(
            NetworkErrorCode::ConnectionClosed,
            "TLS peer has already closed its sending side");
    }
    if (m_impl->shutdownStarted) {
        return Core::failure(
            NetworkErrorCode::ConnectionClosed,
            "TLS close_notify has already started");
    }

    const Core::usize available = m_impl->plaintextSend.size() - m_impl->sendPending;
    if (payload.size() > available) {
        return Core::failure(
            NetworkErrorCode::CapacityExceeded,
            "TlsConnection send buffer cannot hold the payload");
    }

    std::memcpy(
        m_impl->plaintextSend.data() + m_impl->sendPending,
        payload.data(),
        payload.size());
    m_impl->sendPending += payload.size();
    m_impl->stats.queuedSendBytes = m_impl->sendPending;
    return Core::success();
}

Core::Result<Core::usize> TlsConnection::pump()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TlsConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TlsConnection must be used from its owner thread");
    }

    ++m_impl->stats.pumpCallCount;

    if (m_impl->state == TlsConnectionState::Closed) {
        return Core::failure(NetworkErrorCode::ConnectionClosed, "TlsConnection is closed");
    }
    if (m_impl->state == TlsConnectionState::Failed) {
        return Core::failure(NetworkErrorCode::ConnectionFailed, "TlsConnection has failed");
    }

    // Always advance the transport first: every TLS operation below reads and
    // writes through it, so stale buffers would stall the handshake.
    auto transportPumped = m_impl->transport.pump();
    if (!transportPumped) {
        m_impl->markFailed();
        return Core::failure(std::move(transportPumped.error()));
    }

    if (m_impl->state == TlsConnectionState::ConnectingTransport) {
        const auto transportState = m_impl->transport.state();
        if (transportState == TcpConnectionState::Connecting) {
            return Core::usize{0};
        }
        if (transportState != TcpConnectionState::Connected) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::ConnectionFailed,
                "TLS transport failed before the handshake");
        }
        m_impl->state = TlsConnectionState::HandshakingTls;
    }

    if (m_impl->state == TlsConnectionState::HandshakingTls) {
        ++m_impl->stats.handshakePumpCount;
        const int result = mbedtls_ssl_handshake(&m_impl->ssl);
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // Normal: the handshake needs more records, which arrive on a later
            // pump. Flush whatever it produced.
            auto flushed = m_impl->transport.pump();
            if (!flushed) {
                m_impl->markFailed();
                return Core::failure(std::move(flushed.error()));
            }
            return Core::usize{0};
        }
        if (result != 0) {
            m_impl->markFailed();
            // A verification failure is reported separately so a caller can tell a
            // trust problem from a protocol problem.
            if (result == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
                || mbedtls_ssl_get_verify_result(&m_impl->ssl) != 0) {
                return Core::failure(
                    NetworkErrorCode::TlsVerificationFailed,
                    "TLS certificate verification failed");
            }
            return Core::failure(
                NetworkErrorCode::TlsHandshakeFailed,
                "TLS handshake failed");
        }

        m_impl->state = TlsConnectionState::Connected;
        m_impl->stats.handshakeComplete = true;
        auto flushed = m_impl->transport.pump();
        if (!flushed) {
            m_impl->markFailed();
            return Core::failure(std::move(flushed.error()));
        }
    }

    // Encrypt queued plaintext. A partial write is normal, so the tail is kept and
    // compacted for the next pump.
    if (m_impl->sendPending != 0 && m_impl->state != TlsConnectionState::PeerClosed
        && !m_impl->shutdownStarted) {
        const auto writeLength = (std::min)(
            m_impl->sendPending,
            static_cast<Core::usize>(INT_MAX));
        const int written = mbedtls_ssl_write(
            &m_impl->ssl,
            reinterpret_cast<const unsigned char*>(m_impl->plaintextSend.data()),
            writeLength);
        if (written > 0) {
            const auto sent = static_cast<Core::usize>(written);
            if (sent < m_impl->sendPending) {
                std::memmove(
                    m_impl->plaintextSend.data(),
                    m_impl->plaintextSend.data() + sent,
                    m_impl->sendPending - sent);
            }
            m_impl->sendPending -= sent;
            m_impl->stats.totalSentBytes += sent;
            m_impl->stats.queuedSendBytes = m_impl->sendPending;
        } else if (
            written != MBEDTLS_ERR_SSL_WANT_READ && written != MBEDTLS_ERR_SSL_WANT_WRITE) {
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::TlsProtocolFailure,
                "TLS write failed");
        }
        auto flushed = m_impl->transport.pump();
        if (!flushed) {
            m_impl->markFailed();
            return Core::failure(std::move(flushed.error()));
        }
    }

    // Decrypt whatever arrived. Loop because one TCP read can carry several
    // records, and stopping after the first would delay the rest by a frame.
    Core::usize newlyReceived = 0;
    while (m_impl->receivePending < m_impl->plaintextReceive.size()) {
        const Core::usize space = m_impl->plaintextReceive.size() - m_impl->receivePending;
        const auto readLength = (std::min)(space, static_cast<Core::usize>(INT_MAX));
        const int read = mbedtls_ssl_read(
            &m_impl->ssl,
            reinterpret_cast<unsigned char*>(
                m_impl->plaintextReceive.data() + m_impl->receivePending),
            readLength);

        if (read > 0) {
            const auto got = static_cast<Core::usize>(read);
            m_impl->receivePending += got;
            newlyReceived += got;
            m_impl->stats.totalReceivedBytes += got;
            m_impl->stats.bufferedReceiveBytes = m_impl->receivePending;
            continue;
        }
        if (read == MBEDTLS_ERR_SSL_WANT_READ || read == MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        }
        if (read == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // An orderly TLS shutdown, distinguishable from a truncated stream --
            // which is the whole reason close_notify exists.
            m_impl->state = TlsConnectionState::PeerClosed;
            break;
        }
        if (read == 0 || read == MBEDTLS_ERR_SSL_CONN_EOF) {
            // A raw TCP EOF without close_notify is a truncated TLS stream. Do
            // not expose it as PeerClosed: callers must be able to distinguish a
            // clean shutdown from a possible record truncation.
            m_impl->markFailed();
            return Core::failure(
                NetworkErrorCode::TlsTruncated,
                "TLS peer closed without close_notify");
        }
        m_impl->markFailed();
        return Core::failure(
            NetworkErrorCode::TlsProtocolFailure,
            "TLS read failed");
    }

    return newlyReceived;
}

Core::Result<std::span<const std::byte>> TlsConnection::receive()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TlsConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TlsConnection must be used from its owner thread");
    }
    return std::span<const std::byte>{
        m_impl->plaintextReceive.data(),
        m_impl->receivePending};
}

Core::Status TlsConnection::consume(Core::usize byteCount)
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TlsConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TlsConnection must be used from its owner thread");
    }
    if (byteCount > m_impl->receivePending) {
        return Core::failure(
            Core::CoreErrorCode::InvalidArgument,
            "TlsConnection cannot consume more bytes than are buffered");
    }
    if (byteCount == 0) {
        return Core::success();
    }

    const Core::usize remaining = m_impl->receivePending - byteCount;
    if (remaining != 0) {
        std::memmove(
            m_impl->plaintextReceive.data(),
            m_impl->plaintextReceive.data() + byteCount,
            remaining);
    }
    m_impl->receivePending = remaining;
    m_impl->stats.bufferedReceiveBytes = remaining;
    return Core::success();
}

Core::Status TlsConnection::shutdownTls()
{
    if (m_impl == nullptr) {
        return Core::failure(NetworkErrorCode::SocketClosed, "TlsConnection is closed");
    }
    if (!m_impl->isOwnerThread()) {
        return Core::failure(
            NetworkErrorCode::WrongOwnerThread,
            "TlsConnection must be used from its owner thread");
    }
    if (m_impl->shutdownSent) {
        return Core::success();
    }
    if (m_impl->state != TlsConnectionState::Connected
        && m_impl->state != TlsConnectionState::PeerClosed) {
        return Core::failure(
            NetworkErrorCode::NotConnected,
            "TlsConnection is not connected");
    }

    // Application records must be handed to mbedTLS before the close alert is
    // started. Silently dropping this queue would turn an orderly shutdown into
    // data loss that the caller cannot observe.
    if (!m_impl->shutdownStarted && m_impl->sendPending != 0) {
        return Core::failure(
            NetworkErrorCode::WouldBlock,
            "TLS application data is still queued; pump before shutdownTls");
    }

    m_impl->shutdownStarted = true;

    // close_notify may need several attempts if the transport is congested.
    // WouldBlock is explicit so success always means mbedTLS accepted the whole
    // alert; the caller can then use pendingSendBytes() to wait for socket handoff.
    const int result = mbedtls_ssl_close_notify(&m_impl->ssl);
    if (result != 0 && result != MBEDTLS_ERR_SSL_WANT_READ
        && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
        m_impl->markFailed();
        return Core::failure(
            NetworkErrorCode::TlsProtocolFailure,
            "Failed to send TLS close_notify");
    }
    auto flushed = m_impl->transport.pump();
    if (!flushed) {
        m_impl->markFailed();
        return Core::failure(std::move(flushed.error()));
    }
    if (result == 0) {
        m_impl->shutdownSent = true;
        return Core::success();
    }
    return Core::failure(
        NetworkErrorCode::WouldBlock,
        "TLS close_notify is waiting for transport capacity");
}

void TlsConnection::close() noexcept
{
    if (m_impl == nullptr || !m_impl->isOwnerThread()) {
        return;
    }
    if (m_impl->state == TlsConnectionState::Closed) {
        return;
    }
    m_impl->transport.close();
    m_impl->state = TlsConnectionState::Closed;
    m_impl->sendPending = 0;
    m_impl->stats.queuedSendBytes = 0;
}

TlsConnectionStatistics TlsConnection::statistics() const noexcept
{
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->stats;
}

Core::usize TlsConnection::sendBufferCapacity() const noexcept
{
    return m_impl != nullptr ? m_impl->plaintextSend.size() : 0;
}

Core::usize TlsConnection::receiveBufferCapacity() const noexcept
{
    return m_impl != nullptr ? m_impl->plaintextReceive.size() : 0;
}

TlsTrustStoreInfo tlsTrustStoreInfo()
{
    // Reading it here is what populates the cache, so a caller can probe at
    // startup rather than discovering an empty store on its first connect.
    const std::string& anchors = Detail::systemTrustAnchorsPem();
    const auto stats = Detail::systemTrustStoreStatistics();

    TlsTrustStoreInfo info{};
    info.certificateCount = stats.certificateCount;
    info.skippedEntryCount = stats.skippedEntryCount;
    info.available = !anchors.empty();
    return info;
}

ByteStreamState TlsConnection::streamState() const noexcept
{
    switch (state()) {
    case TlsConnectionState::ConnectingTransport:
    case TlsConnectionState::HandshakingTls:
        // Both mean "not usable yet". Surfacing which one would make every
        // protocol above care about TLS.
        return ByteStreamState::Connecting;
    case TlsConnectionState::Connected:
        return ByteStreamState::Connected;
    case TlsConnectionState::PeerClosed:
        return ByteStreamState::PeerClosed;
    case TlsConnectionState::Closed:
        return ByteStreamState::Closed;
    case TlsConnectionState::Failed:
        return ByteStreamState::Failed;
    }
    return ByteStreamState::Failed;
}

Core::Status TlsConnection::sendBytes(std::span<const std::byte> payload)
{
    return send(payload);
}

Core::usize TlsConnection::pendingSendBytes() const noexcept
{
    if (m_impl == nullptr) {
        return 0;
    }
    const Core::usize plaintext = m_impl->sendPending;
    const Core::usize ciphertext = m_impl->transport.pendingSendBytes();
    const Core::usize limit = (std::numeric_limits<Core::usize>::max)();
    return ciphertext > limit - plaintext ? limit : plaintext + ciphertext;
}

Core::Result<Core::usize> TlsConnection::pumpStream()
{
    return pump();
}

Core::Result<std::span<const std::byte>> TlsConnection::peekReceived()
{
    return receive();
}

Core::Status TlsConnection::consumeReceived(Core::usize byteCount)
{
    return consume(byteCount);
}

void TlsConnection::closeStream() noexcept
{
    close();
}

} // namespace Tina::Network
