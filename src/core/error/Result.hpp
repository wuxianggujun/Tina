#pragma once

#include "Error.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace Tina::Core {

class Failure final {
public:
    explicit Failure(Error error) noexcept
        : m_error(std::move(error))
    {
    }

    [[nodiscard]] const Error& error() const& noexcept
    {
        return m_error;
    }

    [[nodiscard]] Error&& error() && noexcept
    {
        return std::move(m_error);
    }

private:
    Error m_error;
};

template <typename Value>
class [[nodiscard]] Result final {
public:
    Result(const Value& value)
        : m_storage(value)
    {
    }

    Result(Value&& value) noexcept(std::is_nothrow_move_constructible_v<Value>)
        : m_storage(std::move(value))
    {
    }

    Result(const Failure& failure)
        : m_storage(failure.error())
    {
    }

    Result(Failure&& failure) noexcept
        : m_storage(std::move(failure).error())
    {
    }

    [[nodiscard]] bool has_value() const noexcept
    {
        return std::holds_alternative<Value>(m_storage);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] Value& value() &
    {
        return std::get<Value>(m_storage);
    }

    [[nodiscard]] const Value& value() const&
    {
        return std::get<Value>(m_storage);
    }

    [[nodiscard]] Value&& value() &&
    {
        return std::get<Value>(std::move(m_storage));
    }

    [[nodiscard]] Error& error() &
    {
        return std::get<Error>(m_storage);
    }

    [[nodiscard]] const Error& error() const&
    {
        return std::get<Error>(m_storage);
    }

    [[nodiscard]] Error&& error() &&
    {
        return std::get<Error>(std::move(m_storage));
    }

private:
    std::variant<Value, Error> m_storage;
};

template <>
class [[nodiscard]] Result<void> final {
public:
    Result() noexcept = default;

    Result(const Failure& failure)
        : m_error(failure.error())
    {
    }

    Result(Failure&& failure) noexcept
        : m_error(std::move(failure).error())
    {
    }

    [[nodiscard]] bool has_value() const noexcept
    {
        return !m_error.has_value();
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    void value() const
    {
        if (m_error.has_value()) {
            throw std::bad_variant_access();
        }
    }

    [[nodiscard]] Error& error() &
    {
        return m_error.value();
    }

    [[nodiscard]] const Error& error() const&
    {
        return m_error.value();
    }

    [[nodiscard]] Error&& error() &&
    {
        return std::move(m_error).value();
    }

private:
    std::optional<Error> m_error;
};

using Status = Result<void>;

[[nodiscard]] inline Status success() noexcept
{
    return {};
}

[[nodiscard]] inline Failure failure(
    ErrorCode code,
    std::string_view message = {},
    SourceLocation location = SourceLocation::current())
{
    return Failure(Error{code, message, location});
}

} // namespace Tina::Core
