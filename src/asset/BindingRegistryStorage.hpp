#pragma once

#include <tina/asset/AssetErrors.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>

namespace Tina::Asset::Detail {

// Active binding entries use address-stable deques; candidate/retirement tables
// use vectors. Both grow before accepting GPU ownership. No frame completion
// callback points into a candidate or retirement vector.
template <typename Storage>
[[nodiscard]] Core::Status growBindingStorage(
    Storage& storage, Core::usize required,
    Core::usize indexLimit = (std::numeric_limits<Core::usize>::max)())
{
    if (required <= storage.size()) { return Core::success(); }
    const auto maximum = (std::min)(storage.max_size(), indexLimit);
    if (required > maximum) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "binding storage index or addressable size is exhausted");
    }
    const auto grown = storage.size() > maximum / 2U ? maximum : storage.size() * 2U;
    try {
        storage.resize((std::max)(required, grown));
        return Core::success();
    } catch (const std::bad_alloc&) {
        return Core::failure(AssetErrorCode::AllocationFailed, "binding storage growth allocation failed");
    } catch (const std::length_error&) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "binding storage exceeds addressable size");
    } catch (const std::exception& exception) {
        return Core::failure(Core::Error{Core::CoreErrorCode::Internal, exception.what()}.withContext(
            "growBindingStorage", "memory resource"));
    } catch (...) {
        return Core::failure(Core::CoreErrorCode::Internal,
                             "binding storage growth threw an unknown exception");
    }
}

template <typename Storage>
[[nodiscard]] Core::Status growBindingStorageBy(
    Storage& storage, Core::usize additional,
    Core::usize indexLimit = (std::numeric_limits<Core::usize>::max)())
{
    const auto maximum = (std::min)(storage.max_size(), indexLimit);
    if (storage.size() > maximum || additional > maximum - storage.size()) {
        return Core::failure(Core::CoreErrorCode::CapacityExceeded,
                             "binding storage index or addressable size is exhausted");
    }
    return growBindingStorage(storage, storage.size() + additional, indexLimit);
}

} // namespace Tina::Asset::Detail
