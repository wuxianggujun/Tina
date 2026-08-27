#include <tina/runtime/GameSettings.hpp>

#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/core/text/Utf8.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <memory_resource>
#include <new>
#include <utility>

namespace Tina {
namespace {

constexpr std::string_view VersionKey = "version";
constexpr std::string_view VsyncKey = "vsync";
constexpr std::string_view BusVolumePrefix = "audio.bus";
constexpr std::string_view BindingPrefix = "input.binding";

[[nodiscard]] std::unexpected<Core::Error> fail(Core::ErrorCode code, std::string_view message)
{
    return Core::failure(code, message);
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept
{
    const auto isSpace = [](char value) noexcept {
        return value == ' ' || value == '\t' || value == '\r';
    };
    while (!text.empty() && isSpace(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool parseU32(std::string_view text, Core::u32& value) noexcept
{
    if (text.empty())
    {
        return false;
    }
    const char* first = text.data();
    const char* last = text.data() + text.size();
    Core::u32 parsed = 0;
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last)
    {
        return false;
    }
    value = parsed;
    return true;
}

[[nodiscard]] bool parseBool(std::string_view text, bool& value) noexcept
{
    if (text == "1" || text == "true")
    {
        value = true;
        return true;
    }
    if (text == "0" || text == "false")
    {
        value = false;
        return true;
    }
    return false;
}

// Volume is a player-visible gain, so the parse is strict about range: a value
// outside [0, 1] means the file was hand-edited wrong or written by a different
// build, and clamping it would hide that.
[[nodiscard]] bool parseUnitFloat(std::string_view text, float& value) noexcept
{
    if (text.empty() || text.size() > 32U)
    {
        return false;
    }
    std::array<char, 33> buffer{};
    std::copy(text.begin(), text.end(), buffer.begin());
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(buffer.data(), &end);
    if (errno != 0 || end != buffer.data() + text.size() || !std::isfinite(parsed))
    {
        return false;
    }
    if (parsed < 0.0F || parsed > 1.0F)
    {
        return false;
    }
    value = parsed;
    return true;
}

void appendUnitFloat(std::string& out, float value)
{
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), static_cast<Core::usize>(result.ptr - buffer.data()));
}

// Patterns are written as a leading discriminator token so an unknown future
// pattern kind is rejected loudly rather than silently reinterpreted as a key.
constexpr std::string_view KeyPatternTag = "key";
constexpr std::string_view PointerPatternTag = "pointer";
constexpr std::string_view GamepadButtonPatternTag = "gamepadButton";
constexpr std::string_view GamepadAxisPatternTag = "gamepadAxis";

void appendPattern(std::string& out, const ActionBindingPattern& pattern)
{
    if (const auto* key = std::get_if<PrimaryWindowKeyBinding>(&pattern))
    {
        out.append(KeyPatternTag);
        out.push_back(':');
        out.append(std::to_string(static_cast<Core::u32>(key->key)));
        return;
    }
    if (const auto* pointer = std::get_if<PrimaryPointerButtonBinding>(&pattern))
    {
        out.append(PointerPatternTag);
        out.push_back(':');
        out.append(std::to_string(static_cast<Core::u32>(pointer->button)));
        return;
    }
    if (const auto* button = std::get_if<StandardGamepadButtonBinding>(&pattern))
    {
        out.append(GamepadButtonPatternTag);
        out.push_back(':');
        out.append(std::to_string(static_cast<Core::u32>(button->button)));
        return;
    }
    const auto& axis = std::get<StandardGamepadAxisBinding>(pattern);
    out.append(GamepadAxisPatternTag);
    out.push_back(':');
    out.append(std::to_string(static_cast<Core::u32>(axis.axis)));
    out.push_back(':');
    out.append(std::to_string(static_cast<Core::u32>(axis.valueMode)));
}

[[nodiscard]] bool parsePattern(std::string_view text, ActionBindingPattern& pattern) noexcept
{
    const auto separator = text.find(':');
    if (separator == std::string_view::npos)
    {
        return false;
    }
    const std::string_view tag = text.substr(0, separator);
    std::string_view rest = text.substr(separator + 1U);

    // Range checks mirror EngineConfig's isValidActionBindingPattern, so a value
    // this accepts is one the mapper will also accept.
    if (tag == KeyPatternTag)
    {
        Core::u32 raw = 0;
        if (!parseU32(rest, raw) || raw <= static_cast<Core::u32>(Platform::Key::Unknown) ||
            raw >= static_cast<Core::u32>(Platform::Key::Count))
        {
            return false;
        }
        pattern = PrimaryWindowKeyBinding{.key = static_cast<Platform::Key>(raw)};
        return true;
    }
    if (tag == PointerPatternTag)
    {
        Core::u32 raw = 0;
        if (!parseU32(rest, raw) || raw >= static_cast<Core::u32>(Platform::PointerButton::Count))
        {
            return false;
        }
        pattern = PrimaryPointerButtonBinding{
            .pointer = Platform::PrimaryPointerId,
            .button = static_cast<Platform::PointerButton>(raw),
        };
        return true;
    }
    if (tag == GamepadButtonPatternTag)
    {
        Core::u32 raw = 0;
        if (!parseU32(rest, raw) || raw >= static_cast<Core::u32>(Platform::GamepadButton::Count))
        {
            return false;
        }
        pattern = StandardGamepadButtonBinding{.button = static_cast<Platform::GamepadButton>(raw)};
        return true;
    }
    if (tag != GamepadAxisPatternTag)
    {
        return false;
    }
    const auto modeSeparator = rest.find(':');
    if (modeSeparator == std::string_view::npos)
    {
        return false;
    }
    Core::u32 rawAxis = 0;
    Core::u32 rawMode = 0;
    if (!parseU32(rest.substr(0, modeSeparator), rawAxis) ||
        !parseU32(rest.substr(modeSeparator + 1U), rawMode))
    {
        return false;
    }
    if (rawAxis >= static_cast<Core::u32>(Platform::GamepadAxis::Count) ||
        rawMode > static_cast<Core::u32>(GamepadAxisValueMode::Trigger))
    {
        return false;
    }
    pattern = StandardGamepadAxisBinding{
        .axis = static_cast<Platform::GamepadAxis>(rawAxis),
        .valueMode = static_cast<GamepadAxisValueMode>(rawMode),
    };
    return true;
}

} // namespace

Core::Result<std::string> writeGameSettingsText(const GameSettings& settings)
try
{
    for (const AudioBusSetting& bus : settings.audioBuses)
    {
        if (!std::isfinite(bus.volume) || bus.volume < 0.0F || bus.volume > 1.0F)
        {
            return fail(Core::CoreErrorCode::InvalidArgument,
                        "audio bus volume must be finite and within [0, 1]");
        }
    }
    std::string out;
    out.append(VersionKey);
    out.push_back('=');
    out.append(std::to_string(GameSettingsWire::SchemaVersion));
    out.push_back('\n');
    out.append(VsyncKey);
    out.push_back('=');
    out.push_back(settings.vsyncEnabled ? '1' : '0');
    out.push_back('\n');
    for (Core::usize index = 0; index < settings.audioBuses.size(); ++index)
    {
        const AudioBusSetting& bus = settings.audioBuses[index];
        out.append(BusVolumePrefix);
        out.append(std::to_string(index));
        out.append(".volume=");
        appendUnitFloat(out, bus.volume);
        out.push_back('\n');
        out.append(BusVolumePrefix);
        out.append(std::to_string(index));
        out.append(".muted=");
        out.push_back(bus.muted ? '1' : '0');
        out.push_back('\n');
    }
    for (Core::usize index = 0; index < settings.inputBindings.size(); ++index)
    {
        const InputBindingSetting& setting = settings.inputBindings[index];
        if (!setting.action.hasValue())
        {
            return fail(Core::CoreErrorCode::InvalidArgument,
                        "persisted input binding requires a non-zero action id");
        }
        out.append(BindingPrefix);
        out.append(std::to_string(index));
        out.push_back('=');
        out.append(std::to_string(setting.action.value()));
        out.push_back(':');
        out.append(std::to_string(setting.binding.value()));
        out.push_back(':');
        out.append(std::to_string(static_cast<Core::u32>(setting.domain)));
        out.push_back(':');
        appendPattern(out, setting.input);
        out.push_back('\n');
    }
    return out;
}
catch (const std::bad_alloc&)
{
    return fail(Core::CoreErrorCode::OutOfMemory, "game settings serialization allocation failed");
}

Core::Result<GameSettings> parseGameSettingsText(std::string_view text)
try
{
    if (!Core::isStrictUtf8WithoutNul(text))
    {
        return fail(Core::CoreErrorCode::InvalidArgument,
                    "game settings text must be strict UTF-8 without NUL");
    }
    GameSettings settings{};
    bool sawVersion = false;
    Core::usize cursor = 0;
    while (cursor <= text.size())
    {
        const auto lineEnd = text.find('\n', cursor);
        const std::string_view rawLine =
            lineEnd == std::string_view::npos ? text.substr(cursor) : text.substr(cursor, lineEnd - cursor);
        cursor = lineEnd == std::string_view::npos ? text.size() + 1U : lineEnd + 1U;
        const std::string_view line = trim(rawLine);
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            return fail(Core::CoreErrorCode::InvalidArgument,
                        "game settings line is not a key=value pair");
        }
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1U));

        if (key == VersionKey)
        {
            Core::u32 version = 0;
            if (!parseU32(value, version))
            {
                return fail(Core::CoreErrorCode::InvalidArgument,
                            "game settings version must be an unsigned integer");
            }
            // A different schema is not corruption: return defaults so an upgrade
            // or downgrade starts cleanly instead of refusing to run.
            if (version != GameSettingsWire::SchemaVersion)
            {
                return GameSettings{};
            }
            sawVersion = true;
            continue;
        }
        if (!sawVersion)
        {
            return fail(Core::CoreErrorCode::InvalidArgument,
                        "game settings must declare version before any other key");
        }
        if (key == VsyncKey)
        {
            if (!parseBool(value, settings.vsyncEnabled))
            {
                return fail(Core::CoreErrorCode::InvalidArgument, "vsync must be 0, 1, true or false");
            }
            continue;
        }
        if (key.starts_with(BusVolumePrefix))
        {
            const std::string_view remainder = key.substr(BusVolumePrefix.size());
            const auto dot = remainder.find('.');
            if (dot == std::string_view::npos)
            {
                return fail(Core::CoreErrorCode::InvalidArgument, "audio bus key is malformed");
            }
            Core::u32 busIndex = 0;
            if (!parseU32(remainder.substr(0, dot), busIndex) ||
                busIndex >= settings.audioBuses.size())
            {
                return fail(Core::CoreErrorCode::InvalidArgument, "audio bus index is out of range");
            }
            const std::string_view field = remainder.substr(dot + 1U);
            AudioBusSetting& bus = settings.audioBuses[busIndex];
            if (field == "volume")
            {
                if (!parseUnitFloat(value, bus.volume))
                {
                    return fail(Core::CoreErrorCode::InvalidArgument,
                                "audio bus volume must be finite and within [0, 1]");
                }
                continue;
            }
            if (field == "muted")
            {
                if (!parseBool(value, bus.muted))
                {
                    return fail(Core::CoreErrorCode::InvalidArgument,
                                "audio bus muted must be 0, 1, true or false");
                }
                continue;
            }
            return fail(Core::CoreErrorCode::InvalidArgument, "unknown audio bus field");
        }
        if (key.starts_with(BindingPrefix))
        {
            InputBindingSetting setting{};
            std::string_view rest = value;
            const auto readField = [&rest](std::string_view& out) noexcept {
                const auto separator = rest.find(':');
                if (separator == std::string_view::npos)
                {
                    return false;
                }
                out = rest.substr(0, separator);
                rest.remove_prefix(separator + 1U);
                return true;
            };
            std::string_view actionText;
            std::string_view bindingText;
            std::string_view domainText;
            if (!readField(actionText) || !readField(bindingText) || !readField(domainText))
            {
                return fail(Core::CoreErrorCode::InvalidArgument,
                            "input binding must be action:binding:domain:pattern");
            }
            Core::u32 action = 0;
            Core::u32 binding = 0;
            Core::u32 domain = 0;
            if (!parseU32(actionText, action) || action == 0U || !parseU32(bindingText, binding) ||
                !parseU32(domainText, domain) || domain > 1U)
            {
                return fail(Core::CoreErrorCode::InvalidArgument,
                            "input binding action, id, or domain is invalid");
            }
            if (!parsePattern(rest, setting.input))
            {
                return fail(Core::CoreErrorCode::InvalidArgument, "input binding pattern is invalid");
            }
            setting.action = InputActionId{action};
            setting.binding = InputBindingId{binding};
            setting.domain = static_cast<InputActionDomain>(domain);
            settings.inputBindings.push_back(setting);
            continue;
        }
        // Unknown keys are ignored so a downgrade does not destroy values a newer
        // build wrote for itself.
    }
    if (!sawVersion)
    {
        return fail(Core::CoreErrorCode::InvalidArgument, "game settings text has no version key");
    }
    return settings;
}
catch (const std::bad_alloc&)
{
    return fail(Core::CoreErrorCode::OutOfMemory, "game settings parse allocation failed");
}

Core::Result<GameSettingsLoadResult> loadGameSettingsFromFile(std::string_view utf8Path)
{
    auto bytes = Core::readFile(utf8Path, {
                                              .maxBytes = GameSettingsWire::MaxFileBytes,
                                              .memoryResource = std::pmr::get_default_resource(),
                                          });
    if (!bytes)
    {
        // First run has no file yet, which is expected rather than a failure.
        if (bytes.error().code == Core::CoreErrorCode::NotFound)
        {
            return GameSettingsLoadResult{};
        }
        return Core::failure(std::move(bytes.error()));
    }
    const std::string_view text{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
    auto parsed = parseGameSettingsText(text);
    if (!parsed)
    {
        return Core::failure(std::move(parsed.error()));
    }
    return GameSettingsLoadResult{.settings = std::move(*parsed), .loaded = true};
}

Core::Status saveGameSettingsToFile(std::string_view utf8Path, const GameSettings& settings)
{
    auto text = writeGameSettingsText(settings);
    if (!text)
    {
        return Core::failure(std::move(text.error()));
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(text->data()), text->size()};
    return Core::writeFile(utf8Path, bytes);
}

Core::Result<std::vector<InputActionBinding>>
mergeInputBindingSettings(std::span<const InputActionBinding> startupBindings,
                          std::span<const InputBindingSetting> persisted)
try
{
    std::vector<InputActionBinding> merged{startupBindings.begin(), startupBindings.end()};
    for (const InputBindingSetting& setting : persisted)
    {
        if (!setting.action.hasValue())
        {
            return fail(Core::CoreErrorCode::InvalidArgument,
                        "persisted input binding requires a non-zero action id");
        }
        auto target = merged.end();
        if (setting.binding.hasValue())
        {
            target = std::find_if(merged.begin(), merged.end(), [&setting](const InputActionBinding& candidate) {
                return candidate.binding == setting.binding;
            });
        }
        if (target == merged.end())
        {
            target = std::find_if(merged.begin(), merged.end(), [&setting](const InputActionBinding& candidate) {
                return candidate.action == setting.action && candidate.domain == setting.domain;
            });
        }
        // A stale entry for an action the game removed must not block startup.
        if (target == merged.end())
        {
            continue;
        }
        target->input = setting.input;
    }
    return merged;
}
catch (const std::bad_alloc&)
{
    return fail(Core::CoreErrorCode::OutOfMemory, "input binding merge allocation failed");
}

} // namespace Tina
