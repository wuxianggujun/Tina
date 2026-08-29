#pragma once

// Reads the platform's CA trust anchors. Private to src/network/tls: it names
// Windows CryptoAPI types on that platform.
//
// Loaded once and cached for the process. Every engine surveyed does the same --
// Godot loads at startup from main.cpp, UE5 in FSslModule::StartupModule() -- and
// the reason is measured: a naive implementation repopulates the X509 store for
// every new connection, and parsing the anchor set costs ~4.5 ms on OpenSSL 1.1.1
// but ~50 ms on 3.0. macOS is worse still, seconds in Go's measurements. None of
// that belongs on a connect path.

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <string>

namespace Tina::Network::Detail {

struct SystemTrustStoreStatistics final {
    // Certificates successfully parsed out of the platform store.
    Core::usize certificateCount = 0;
    // Entries the platform offered that could not be parsed. Counted rather than
    // failed: one malformed enterprise root must not make every other anchor
    // unusable.
    Core::usize skippedEntryCount = 0;
    bool loaded = false;
    bool loadAttempted = false;
};

// Concatenated PEM of the platform anchors, or empty when the platform offered
// none. The reference is to process-lifetime storage.
//
// Thread-safe: the first caller populates it under a lock and later callers read
// the same immutable string.
[[nodiscard]] const std::string& systemTrustAnchorsPem();

[[nodiscard]] SystemTrustStoreStatistics systemTrustStoreStatistics();

} // namespace Tina::Network::Detail
