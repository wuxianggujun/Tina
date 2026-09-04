#include <tina/render/RenderDevice.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace Tina::Render {
namespace {

// Both Sprite2D and Mesh3D are supported: each has a contract .sh header and engine vertex shader
// that custom fragment stages link against. The backend's createShader case list must match this
// set: accepting a kind here but having no program linker is headless-green/real-backend-red split.
[[nodiscard]] constexpr bool isSupportedShaderKind(GpuShaderKind kind) noexcept
{
    switch (kind)
    {
    case GpuShaderKind::Sprite2D:
    case GpuShaderKind::Mesh3D:
        return true;
    case GpuShaderKind::Invalid:
        break;
    }
    return false;
}

[[nodiscard]] constexpr bool isSupportedShaderBinaryProfile(GpuShaderBinaryProfile profile) noexcept
{
    switch (profile)
    {
    case GpuShaderBinaryProfile::Glsl120:
    case GpuShaderBinaryProfile::SpirV:
    case GpuShaderBinaryProfile::Dxbc50:
    case GpuShaderBinaryProfile::Essl300:
    case GpuShaderBinaryProfile::Metal:
        return true;
    case GpuShaderBinaryProfile::Invalid:
        break;
    }
    return false;
}

// Length up to the terminator. A table entry that fills every byte without one is treated as
// unterminated, which the caller reports as an empty name rather than reading past the array.
[[nodiscard]] constexpr usize
nameByteLength(const std::array<char, GpuShaderUniformValue::MaximumNameBytes + 1>& name) noexcept
{
    for (usize index = 0; index < name.size(); ++index)
    {
        if (name[index] == '\0')
        {
            return index;
        }
    }
    return 0;
}

// Every sampler macro bgfx_shader.sh defines. Matched as whole identifiers, so no prefix ordering is
// needed: SAMPLER2D cannot swallow the head of SAMPLER2DARRAY. Matching the macro rather than the
// expanded declaration is deliberate -- the expansion differs per backend and the register only
// survives in some of them.
constexpr std::array<std::string_view, 14> kSamplerMacros{
    "SAMPLER2D",       "SAMPLER3D",        "SAMPLER2DMS",      "SAMPLERCUBE",
    "SAMPLER2DARRAY",  "SAMPLER2DMSARRAY", "SAMPLERCUBEARRAY", "SAMPLER2DSHADOW",
    "SAMPLER2DARRAYSHADOW", "SAMPLERCUBESHADOW", "ISAMPLER2D", "USAMPLER2D",
    "ISAMPLER3D",      "USAMPLER3D"};

[[nodiscard]] constexpr bool isIdentifierChar(char value) noexcept
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_';
}

[[nodiscard]] constexpr bool isSpace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

// Advances past whitespace. Newlines count: a declaration may be wrapped, and shaderc's own
// preprocessor does not care where the line breaks fall.
[[nodiscard]] constexpr usize skipSpace(std::string_view source, usize index) noexcept
{
    while (index < source.size() && isSpace(source[index]))
    {
        ++index;
    }
    return index;
}

// Advances past spaces and tabs only, stopping at a newline. Used inside a preprocessor directive,
// where the line end terminates the directive and must not be stepped over.
[[nodiscard]] constexpr usize skipBlanks(std::string_view source, usize index) noexcept
{
    while (index < source.size() && (source[index] == ' ' || source[index] == '\t'))
    {
        ++index;
    }
    return index;
}

// The identifier immediately after a `#`, so a conditional can be told from any other directive.
// Whitespace between the hash and the name is legal, and shaderc's preprocessor accepts it.
[[nodiscard]] constexpr std::string_view directiveName(std::string_view source, usize hashIndex) noexcept
{
    usize cursor = skipBlanks(source, hashIndex + 1);
    const usize begin = cursor;
    while (cursor < source.size() && isIdentifierChar(source[cursor]))
    {
        ++cursor;
    }
    return source.substr(begin, cursor - begin);
}

// Whether an `#if` condition is the literal 0, i.e. a block the author has switched off. Only the
// literal is recognised: anything else depends on macro state this scan does not model, so it is
// treated as live rather than guessed at.
[[nodiscard]] constexpr bool isLiteralFalseCondition(std::string_view source, usize hashIndex) noexcept
{
    usize cursor = skipBlanks(source, hashIndex + 1);
    while (cursor < source.size() && isIdentifierChar(source[cursor]))
    {
        ++cursor;
    }
    cursor = skipBlanks(source, cursor);
    if (cursor >= source.size() || source[cursor] != '0')
    {
        return false;
    }
    ++cursor;
    // A single 0 and nothing else. `#if 0x1` and `#if 00` are not this shape, and treating them as
    // false would switch off a block the compiler keeps.
    cursor = skipBlanks(source, cursor);
    return cursor >= source.size() || source[cursor] == '\n' || source[cursor] == '\r' ||
           (source[cursor] == '/' && cursor + 1 < source.size() &&
            (source[cursor + 1] == '/' || source[cursor + 1] == '*'));
}

} // namespace

Core::Status validateShaderUploadDesc(const GpuShaderUploadDesc& desc) noexcept
{
    if (!isSupportedShaderKind(desc.shaderKind))
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Shader upload kind is not a supported engine program");
    }
    if (desc.binaries.empty() || desc.binaries.size() > GpuShaderUploadDesc::MaximumBinaryCount)
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Shader upload must carry between one and MaximumBinaryCount binaries");
    }

    auto previousProfile = GpuShaderBinaryProfile::Invalid;
    for (const GpuShaderBinary& binary : desc.binaries)
    {
        if (!isSupportedShaderBinaryProfile(binary.profile))
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader upload carries an unsupported renderer profile");
        }
        // Strictly ascending, so the table has no duplicate profile for the backend to choose
        // between and can be searched in the order given.
        if (static_cast<u8>(binary.profile) <= static_cast<u8>(previousProfile))
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader upload binaries must be sorted by ascending profile");
        }
        previousProfile = binary.profile;
        if (binary.bytes.empty())
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader upload binaries must not be empty");
        }
    }
    return Core::success();
}

Core::Status validateShaderUniformBindingDesc(const GpuShaderUniformBindingDesc& desc) noexcept
{
    if (desc.values.size() > GpuShaderUniformBindingDesc::MaximumValueCount)
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Shader uniform binding carries more values than MaximumValueCount");
    }
    for (usize index = 0; index < desc.values.size(); ++index)
    {
        const GpuShaderUniformValue& entry = desc.values[index];
        const usize nameLength = nameByteLength(entry.name);
        if (nameLength == 0)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader uniform binding carries a value with an empty name");
        }
        for (const float component : entry.value)
        {
            if (!std::isfinite(component))
            {
                return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                     "Shader uniform binding values must be finite");
            }
        }
        // Quadratic, but MaximumValueCount is 16: 120 comparisons at bind time is cheaper than a
        // hash set allocation in a noexcept path.
        for (usize other = 0; other < index; ++other)
        {
            if (std::string_view{desc.values[other].name.data(), nameByteLength(desc.values[other].name)} ==
                std::string_view{entry.name.data(), nameLength})
            {
                return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                     "Shader uniform binding names must be unique");
            }
        }
    }
    return Core::success();
}

Core::Status validateShaderTextureBindingDesc(const GpuShaderTextureBindingDesc& desc) noexcept
{
    if (desc.values.size() > GpuShaderTextureBindingDesc::MaximumValueCount)
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Shader texture binding carries more textures than MaximumValueCount");
    }
    for (usize index = 0; index < desc.values.size(); ++index)
    {
        const GpuShaderTextureValue& entry = desc.values[index];
        const usize nameLength = nameByteLength(entry.name);
        if (nameLength == 0)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader texture binding carries a texture with an empty name");
        }
        // Only the shape of the id, not whether it is live: liveness is device state, so the backend's
        // own texture table is what decides it. A default-constructed id is caller error either way.
        if (!entry.texture)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "Shader texture binding carries an invalid texture id");
        }
        // Quadratic like the value table above, over an even smaller maximum.
        for (usize other = 0; other < index; ++other)
        {
            if (std::string_view{desc.values[other].name.data(), nameByteLength(desc.values[other].name)} ==
                std::string_view{entry.name.data(), nameLength})
            {
                return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                     "Shader texture binding names must be unique");
            }
        }
    }
    return Core::success();
}

Core::Result<GpuShaderSamplerDeclarationTable>
parseShaderSamplerDeclarations(std::string_view source) noexcept
{
    GpuShaderSamplerDeclarationTable table{};

    usize index = 0;
    // Tracks whether the scan is at the start of a line, because a preprocessor line is skipped
    // whole: `#define MASK SAMPLER2D(s_mask, 2)` declares nothing by itself, and the expansion (if
    // any) appears where the macro is used.
    bool atLineStart = true;
    // Nesting depth of conditionals whose body the compiler drops, so `#if 0` blocks contribute no
    // declarations. Without this a switched-off sampler is collected and then rejected for never
    // being sampled, which fails a shader the compiler would accept.
    //
    // Only `#if 0` is recognised. Every other condition is treated as live, which is the safe
    // direction: a declaration wrongly kept is checked against a register rule the author can read
    // and satisfy, while one wrongly dropped puts a stage mismatch back on the GPU where nothing
    // reports it. Deeper nesting is counted rather than tracked per-branch because a conditional
    // inside a dead block is dead whatever it says.
    u32 deadConditionalDepth = 0;
    while (index < source.size())
    {
        const char current = source[index];
        if (current == '\n')
        {
            atLineStart = true;
            ++index;
            continue;
        }
        if (isSpace(current))
        {
            ++index;
            continue;
        }
        if (current == '/' && index + 1 < source.size())
        {
            if (source[index + 1] == '/')
            {
                while (index < source.size() && source[index] != '\n')
                {
                    ++index;
                }
                continue;
            }
            if (source[index + 1] == '*')
            {
                index += 2;
                while (index + 1 < source.size() && !(source[index] == '*' && source[index + 1] == '/'))
                {
                    if (source[index] == '\n')
                    {
                        atLineStart = true;
                    }
                    ++index;
                }
                // An unterminated block comment swallows the rest of the source, which is what the
                // compiler would do too.
                index = index + 1 < source.size() ? index + 2 : source.size();
                continue;
            }
        }
        if (atLineStart && current == '#')
        {
            const std::string_view directive = directiveName(source, index);
            if (deadConditionalDepth > 0)
            {
                // Inside a dropped block. Only the directives that change nesting matter; an `#else`
                // at the outermost dead level revives the scan, because the dead branch was the one
                // before it.
                if (directive == "if" || directive == "ifdef" || directive == "ifndef")
                {
                    ++deadConditionalDepth;
                }
                else if (directive == "endif")
                {
                    --deadConditionalDepth;
                }
                else if (deadConditionalDepth == 1 && (directive == "else" || directive == "elif"))
                {
                    deadConditionalDepth = 0;
                }
            }
            else if (directive == "if" && isLiteralFalseCondition(source, index))
            {
                deadConditionalDepth = 1;
            }

            // Line continuations included: a macro definition spanning lines is still one directive.
            while (index < source.size())
            {
                if (source[index] == '\n')
                {
                    const bool continued = index > 0 && source[index - 1] == '\\';
                    if (!continued)
                    {
                        break;
                    }
                }
                ++index;
            }
            continue;
        }
        if (deadConditionalDepth > 0)
        {
            // Body of a dropped conditional: neither a declaration nor a reference in here reaches
            // the compiler, so skip to the next line without touching the table.
            while (index < source.size() && source[index] != '\n')
            {
                ++index;
            }
            continue;
        }
        atLineStart = false;

        // Only try to match a macro at an identifier boundary, so `MY_SAMPLER2D` is not read as a
        // sampler declaration.
        const bool atIdentifierBoundary = index == 0 || !isIdentifierChar(source[index - 1]);
        if (!atIdentifierBoundary || !isIdentifierChar(current))
        {
            ++index;
            continue;
        }

        // Take the whole identifier first and compare it entire, rather than testing prefixes: a
        // prefix test would need the macro list sorted longest-first and would read SAMPLER2DSHADOW as
        // SAMPLER2D followed by junk.
        usize identifierEnd = index;
        while (identifierEnd < source.size() && isIdentifierChar(source[identifierEnd]))
        {
            ++identifierEnd;
        }
        const std::string_view token = source.substr(index, identifierEnd - index);

        std::string_view matched{};
        for (const std::string_view macro : kSamplerMacros)
        {
            if (token == macro)
            {
                matched = macro;
                break;
            }
        }
        if (matched.empty())
        {
            // Not a sampler macro, so see whether it names one of the samplers declared so far.
            for (u8 entry = 0; entry < table.count; ++entry)
            {
                GpuShaderSamplerDeclaration& declaration = table.declarations[entry];
                if (std::string_view{declaration.name.data(), nameByteLength(declaration.name)} == token)
                {
                    declaration.referenced = true;
                    break;
                }
            }
            index = identifierEnd;
            continue;
        }

        usize cursor = skipSpace(source, identifierEnd);
        if (cursor >= source.size() || source[cursor] != '(')
        {
            // The macro's own `#define` line is already skipped above, so reaching here means the
            // token is used as something other than a call. Not an error, just not a declaration.
            index = identifierEnd;
            continue;
        }
        cursor = skipSpace(source, cursor + 1);

        const usize nameBegin = cursor;
        if (cursor < source.size() && (source[cursor] == '_' || !((source[cursor] >= '0') && (source[cursor] <= '9'))))
        {
            while (cursor < source.size() && isIdentifierChar(source[cursor]))
            {
                ++cursor;
            }
        }
        const std::string_view name = source.substr(nameBegin, cursor - nameBegin);
        if (name.empty())
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "A sampler declaration has no name");
        }
        if (name.size() > GpuShaderTextureValue::MaximumNameBytes)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "A sampler name is longer than a texture binding can carry");
        }

        cursor = skipSpace(source, cursor);
        if (cursor >= source.size() || source[cursor] != ',')
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "A sampler declaration is missing its register argument");
        }
        cursor = skipSpace(source, cursor + 1);

        const usize registerBegin = cursor;
        u32 registerValue = 0;
        while (cursor < source.size() && source[cursor] >= '0' && source[cursor] <= '9')
        {
            registerValue = registerValue * 10U + static_cast<u32>(source[cursor] - '0');
            if (registerValue > GpuShaderTextureStages::MaximumCount)
            {
                return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                     "A sampler declares a register beyond what any renderer can bind");
            }
            ++cursor;
        }
        if (cursor == registerBegin)
        {
            // Fails closed rather than skipping: a macro register is exactly the case where the
            // cooker cannot prove the stage matches, and silently accepting it would put the
            // mismatch back on the GPU where nothing reports it.
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "A sampler register must be a decimal literal so the cooker can "
                                 "check it against the stage the engine will bind");
        }

        if (table.count >= GpuShaderSamplerDeclarationTable::MaximumCount)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "A shader declares more samplers than any renderer can bind");
        }
        GpuShaderSamplerDeclaration& entry = table.declarations[table.count];
        std::copy_n(name.begin(), name.size(), entry.name.begin());
        entry.stage = static_cast<u8>(registerValue);
        ++table.count;

        index = cursor;
    }

    return table;
}

Core::Status validateAuthorSamplerRegisters(GpuShaderKind kind,
                                            const GpuShaderSamplerDeclarationTable& declarations) noexcept
{
    if (!isSupportedShaderKind(kind))
    {
        return Core::failure(RenderErrorCode::InvalidShaderUpload,
                             "Sampler registers cannot be checked without a supported shader kind");
    }

    const std::span<const std::string_view> engineNames = GpuShaderTextureStages::engineSamplerNames(kind);
    const u8 firstAuthorStage = GpuShaderTextureStages::firstAuthorStage(kind);
    const u8 maximumAuthorCount = GpuShaderTextureStages::maximumAuthorCount(kind);

    u8 authorIndex = 0;
    std::array<char, 256> message{};
    for (u8 index = 0; index < declarations.count; ++index)
    {
        const GpuShaderSamplerDeclaration& entry = declarations.declarations[index];
        const usize nameLength = nameByteLength(entry.name);
        if (nameLength == 0)
        {
            return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                 "A sampler declaration carries an empty name");
        }
        const std::string_view name{entry.name.data(), nameLength};

        for (u8 other = 0; other < index; ++other)
        {
            const GpuShaderSamplerDeclaration& previous = declarations.declarations[other];
            if (std::string_view{previous.name.data(), nameByteLength(previous.name)} == name)
            {
                return Core::failure(RenderErrorCode::InvalidShaderUpload,
                                     "A shader declares the same sampler name twice");
            }
        }

        const auto engineSlot = std::find(engineNames.begin(), engineNames.end(), name);
        if (engineSlot != engineNames.end())
        {
            // Re-declaring an engine sampler is allowed (the contract header does exactly that), but
            // only at its own stage: any other register would make the engine's setTexture and the
            // shader's sample disagree for a texture the author does not even own.
            const auto expected = static_cast<u8>(engineSlot - engineNames.begin());
            if (entry.stage != expected)
            {
                std::snprintf(message.data(), message.size(),
                              "Engine sampler '%s' must be declared with register %u, not %u",
                              name.data(), static_cast<unsigned>(expected),
                              static_cast<unsigned>(entry.stage));
                return Core::failure(RenderErrorCode::InvalidShaderUpload, message.data());
            }
            continue;
        }

        if (authorIndex >= maximumAuthorCount)
        {
            std::snprintf(message.data(), message.size(),
                          "A %s shader can declare at most %u samplers of its own, so '%s' could only "
                          "ever sample an engine texture",
                          kind == GpuShaderKind::Mesh3D ? "Mesh3D" : "Sprite2D",
                          static_cast<unsigned>(maximumAuthorCount), name.data());
            return Core::failure(RenderErrorCode::InvalidShaderUpload, message.data());
        }

        if (!entry.referenced)
        {
            // Not style advice. Every shader compiler drops a sampler that is never sampled, so it is
            // absent from the reflected list the backend numbers stages from: the sampler after it
            // moves down one stage and reads the wrong texture, while the source still shows a
            // perfectly consecutive run of registers. Rejecting it is the only way the author hears
            // about it before the pixels are wrong.
            std::snprintf(message.data(), message.size(),
                          "Sampler '%s' is declared but never sampled, so the compiler drops it and "
                          "every later sampler shifts down one stage; sample it or remove it",
                          name.data());
            return Core::failure(RenderErrorCode::InvalidShaderUpload, message.data());
        }

        // Positional, and it has to be: the backend assigns stages by walking the reflected sampler
        // list in order, so the Nth author sampler always lands on firstAuthorStage + N. Requiring
        // the source to say the same number is what makes the two agree on every backend -- the
        // cooked binary cannot arbitrate, since GLSL writes register 0 for every sampler and SPIR-V
        // writes a shifted binding.
        const auto expected = static_cast<u8>(firstAuthorStage + authorIndex);
        if (entry.stage != expected)
        {
            std::snprintf(message.data(), message.size(),
                          "Sampler '%s' is declared with register %u but the engine binds a %s "
                          "shader's sampler number %u at stage %u; declare it as %u",
                          name.data(), static_cast<unsigned>(entry.stage),
                          kind == GpuShaderKind::Mesh3D ? "Mesh3D" : "Sprite2D",
                          static_cast<unsigned>(authorIndex), static_cast<unsigned>(expected),
                          static_cast<unsigned>(expected));
            return Core::failure(RenderErrorCode::InvalidShaderUpload, message.data());
        }
        ++authorIndex;
    }

    return Core::success();
}

} // namespace Tina::Render
