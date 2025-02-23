#pragma once

#include <bgfx/bgfx.h>
#include <fmt/format.h>

template<>
struct fmt::formatter<bgfx::UniformType::Enum> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const bgfx::UniformType::Enum& type, FormatContext& ctx) const {
        const char* name;
        switch (type) {
            case bgfx::UniformType::Sampler: name = "Sampler"; break;
            case bgfx::UniformType::Vec4: name = "Vec4"; break;
            case bgfx::UniformType::Mat3: name = "Mat3"; break;
            case bgfx::UniformType::Mat4: name = "Mat4"; break;
            default: name = "Unknown"; break;
        }
        return fmt::format_to(ctx.out(), "{}", name);
    }
}; 