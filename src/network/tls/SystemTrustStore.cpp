#include "SystemTrustStore.hpp"

#include <mbedtls/base64.h>

#include <array>
#include <cstdio>
#include <mutex>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace Tina::Network::Detail {
namespace {

SystemTrustStoreStatistics g_statistics{};

[[nodiscard]] std::mutex& storeMutex() noexcept
{
    static std::mutex instance;
    return instance;
}

// Wraps DER in a PEM block. mbedtls_x509_crt_parse accepts concatenated PEM, so
// building one string is simpler than feeding certificates one at a time and
// gives the caller something it can also log or diff.
void appendPemBlock(std::string& out, const unsigned char* der, Core::usize derSize)
{
    // base64 is 4 bytes per 3 input bytes, plus a newline every 64 output chars.
    const Core::usize encodedCapacity = ((derSize + 2) / 3) * 4 + (derSize / 48) + 64;
    std::vector<unsigned char> encoded(encodedCapacity, 0);

    size_t written = 0;
    if (mbedtls_base64_encode(encoded.data(), encoded.size(), &written, der, derSize) != 0) {
        ++g_statistics.skippedEntryCount;
        return;
    }

    out.append("-----BEGIN CERTIFICATE-----\n");
    const std::string_view body{reinterpret_cast<const char*>(encoded.data()), written};
    for (Core::usize offset = 0; offset < body.size(); offset += 64) {
        out.append(body.substr(offset, 64));
        out.push_back('\n');
    }
    out.append("-----END CERTIFICATE-----\n");
    ++g_statistics.certificateCount;
}

#if defined(_WIN32)

// Reads the CurrentUser ROOT store.
//
// Microsoft's guidance is to let chain building fetch roots on demand rather than
// enumerate, because this store is deliberately incomplete and trust is really
// expressed by a daily-updated CTL. Enumerating therefore gets a snapshot that can
// miss a root Windows would have fetched, and cannot express a distrust record at
// all. That is a real limitation of extracting anchors rather than delegating the
// verdict, and it is documented in the public header rather than hidden here.
[[nodiscard]] std::string readPlatformAnchors()
{
    std::string pem;

    HCERTSTORE store = ::CertOpenSystemStoreW(0, L"ROOT");
    if (store == nullptr) {
        return pem;
    }

    PCCERT_CONTEXT context = nullptr;
    while ((context = ::CertEnumCertificatesInStore(store, context)) != nullptr) {
        if (context->pbCertEncoded == nullptr || context->cbCertEncoded == 0) {
            ++g_statistics.skippedEntryCount;
            continue;
        }
        appendPemBlock(
            pem,
            context->pbCertEncoded,
            static_cast<Core::usize>(context->cbCertEncoded));
    }

    // CERT_CLOSE_STORE_FORCE_FLAG would free contexts still referenced elsewhere;
    // the enumeration above released each one as it advanced.
    ::CertCloseStore(store, 0);
    return pem;
}

#else

// Reads the first bundle file that exists. Distributions disagree on the path, so
// the list is ordered by how common each is rather than trying one and failing.
//
// A directory (CApath) is deliberately not walked: OpenSSL loads a hashed
// directory on demand, but mbedTLS has no equivalent, so walking it here would
// mean parsing every file eagerly for no benefit.
[[nodiscard]] std::string readPlatformAnchors()
{
    constexpr std::array<const char*, 6> candidates{
        "/etc/ssl/certs/ca-certificates.crt",  // Debian, Ubuntu, Alpine
        "/etc/pki/tls/certs/ca-bundle.crt",    // Fedora, RHEL
        "/etc/ssl/ca-bundle.pem",              // openSUSE
        "/etc/pki/tls/cacert.pem",             // older RHEL
        "/etc/ssl/cert.pem",                   // macOS with OpenSSL, FreeBSD
        "/usr/local/share/certs/ca-root-nss.crt",  // FreeBSD ports
    };

    for (const char* path : candidates) {
        std::FILE* file = std::fopen(path, "rb");
        if (file == nullptr) {
            continue;
        }

        std::string contents;
        std::array<char, 8192> buffer{};
        while (true) {
            const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
            if (read == 0) {
                break;
            }
            contents.append(buffer.data(), read);
        }
        std::fclose(file);

        if (contents.empty()) {
            continue;
        }

        // The file is already PEM, so it is used verbatim. Counting blocks keeps
        // the statistic comparable with the Windows path.
        Core::usize offset = 0;
        constexpr std::string_view marker = "-----BEGIN CERTIFICATE-----";
        while ((offset = contents.find(marker, offset)) != std::string::npos) {
            ++g_statistics.certificateCount;
            offset += marker.size();
        }
        return contents;
    }

    return {};
}

#endif

} // namespace

const std::string& systemTrustAnchorsPem()
{
    static std::string cached;

    const std::lock_guard<std::mutex> guard{storeMutex()};
    if (!g_statistics.loadAttempted) {
        g_statistics.loadAttempted = true;
        cached = readPlatformAnchors();
        g_statistics.loaded = !cached.empty();
    }
    return cached;
}

SystemTrustStoreStatistics systemTrustStoreStatistics()
{
    const std::lock_guard<std::mutex> guard{storeMutex()};
    return g_statistics;
}

} // namespace Tina::Network::Detail
