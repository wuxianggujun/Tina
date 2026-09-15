#include <tina/serialization/JsonArchive.hpp>

#include <algorithm>
#include <cstring>

namespace Tina::Serialization {

Core::Result<Reader> Reader::member(std::string_view key) const
{
    auto child = value_.member(key);
    if (!child) return Core::failure(Core::Error{ErrorCode::InvalidData, "Required field missing or parent not an object"}
        .addContext("decode", path_ + "." + std::string(key)));
    return Reader{*child, path_ + "." + std::string(key), limits_};
}

Core::Result<std::vector<std::pair<std::string, Reader>>> Reader::members() const
try {
    if (!value_.isObject()) return Core::failure(error(ErrorCode::InvalidData, "Expected object map"));
    auto entries = value_.members();
    if (!entries) return Core::failure(entries.error());
    std::vector<std::pair<std::string, Reader>> result;
    result.reserve(entries->size());
    for (auto& [name, value] : *entries) {
        Reader child{std::move(value), path_ + "." + name, limits_};
        result.emplace_back(std::move(name), std::move(child));
    }
    return result;
} catch (const std::bad_alloc&) {
    return Core::failure(error(Core::CoreErrorCode::OutOfMemory, "Object reader allocation failed"));
}

Core::Result<Reader> Reader::element(Core::usize index) const
{
    auto child = value_.element(index);
    if (!child) return Core::failure(error(ErrorCode::InvalidData, "Array index out of range"));
    return Reader{*child, path_ + "[" + std::to_string(index) + "]", limits_};
}

Core::Status Reader::fieldsOnly(std::initializer_list<std::string_view> fields) const
{
    auto names = value_.memberNames();
    if (!names) return Core::failure(error(ErrorCode::InvalidData, "Expected object"));
    for (const auto& name : *names) {
        if (std::find(fields.begin(), fields.end(), name) == fields.end())
            return Core::failure(Core::Error{ErrorCode::InvalidData, "Unknown field"}.addContext("decode", path_ + "." + name));
    }
    return Core::success();
}

Core::Status Writer::status() const
{
    if (state_.failure) return Core::failure(*state_.failure);
    return state_.output.failed() ? fail("JSON writer failed", ErrorCode::CodecFailed) : Core::success();
}

Core::Status Writer::writeWith(const std::function<Core::Status(Writer&)>& write)
try {
    if (auto result = status(); !result) return result;
    if (!write) return fail("Empty encode callback", ErrorCode::CodecFailed);
    if (auto result = write(*this); !result) {
        if (!state_.failure) state_.failure = result.error();
        return Core::failure(*state_.failure);
    }
    if (!written_) return fail("Codec did not write a value", ErrorCode::CodecFailed);
    return status();
} catch (const std::bad_alloc&) {
    return fail("Encode allocation failed", Core::CoreErrorCode::OutOfMemory);
} catch (const std::exception& exception) {
    return fail(exception.what(), ErrorCode::CodecFailed);
} catch (...) { return fail("Encode callback threw", ErrorCode::CodecFailed); }

Core::Status Writer::countValue()
{
    if (auto result = status(); !result) return result;
    if (written_) return fail("Codec wrote multiple values", ErrorCode::CodecFailed);
    written_ = true;
    if (state_.output.depth() > state_.limits.maxDepth)
        return fail("Archive depth exceeded", ErrorCode::LimitExceeded);
    if (++state_.nodes > state_.limits.maxNodes)
        return fail("Archive node budget exceeded", ErrorCode::LimitExceeded);
    return status();
}

Core::Status Writer::null()
{
    if (auto result = countValue(); !result) return result;
    if (key_) state_.output.rawMember(*key_, "null"); else state_.output.nullElement();
    return status();
}

Core::Status Writer::string(std::string_view value)
{
    if (!Core::isStrictUtf8(value)) return fail("String is not strict UTF-8");
    if (value.size() > state_.limits.maxBytes - state_.stringBytes)
        return fail("Archive string budget exceeded", ErrorCode::LimitExceeded);
    state_.stringBytes += value.size();
    if (auto result = countValue(); !result) return result;
    if (key_) state_.output.member(*key_, value); else state_.output.element(value);
    return status();
}

Core::Status Writer::object(const std::function<Core::Status(ObjectWriter&)>& write)
{
    if (auto result = countValue(); !result) return result;
    if (state_.output.depth() > state_.limits.maxDepth)
        return fail("Archive depth exceeded", ErrorCode::LimitExceeded);
    if (key_) state_.output.beginObjectMember(*key_); else state_.output.beginObjectElement();
    if (auto result = status(); !result) return result;
    ObjectWriter object{state_, path_};
    if (auto result = write(object); !result) {
        if (!state_.failure) state_.failure = result.error();
        return Core::failure(*state_.failure);
    }
    if (auto result = status(); !result) return result;
    state_.output.endObject();
    return status();
}

Core::Status Writer::array(const std::function<Core::Status(ArrayWriter&)>& write)
{
    if (auto result = countValue(); !result) return result;
    if (state_.output.depth() > state_.limits.maxDepth)
        return fail("Archive depth exceeded", ErrorCode::LimitExceeded);
    if (key_) state_.output.beginArrayMember(*key_); else state_.output.beginArrayElement();
    if (auto result = status(); !result) return result;
    ArrayWriter array{state_, path_};
    if (auto result = write(array); !result) {
        if (!state_.failure) state_.failure = result.error();
        return Core::failure(*state_.failure);
    }
    if (auto result = status(); !result) return result;
    state_.output.endArray();
    return status();
}

Core::Status ObjectWriter::fieldWith(std::string_view key, const std::function<Core::Status(Writer&)>& write)
{
    Writer child{state_, key, path_ + "." + std::string(key)};
    if (state_.failure) return Core::failure(*state_.failure);
    if (!Core::isStrictUtf8(key)) return child.fail("Field name is not strict UTF-8");
    if (key.size() > state_.limits.maxBytes - state_.stringBytes)
        return child.fail("Archive field-name budget exceeded", ErrorCode::LimitExceeded);
    state_.stringBytes += key.size();
    if (!keys_.emplace(key).second) return child.fail("Duplicate field", ErrorCode::DuplicateIdentity);
    return child.writeWith(write);
}

Core::Status ArrayWriter::elementWith(const std::function<Core::Status(Writer&)>& write)
{
    Writer child{state_, std::nullopt, path_ + "[" + std::to_string(index_++) + "]"};
    return child.writeWith(write);
}

Core::Result<std::vector<std::byte>> encodeJsonWith(
    const std::function<Core::Status(Writer&)>& write, ArchiveLimits limits)
try {
    if (!write || limits.maxBytes == 0 || limits.maxDepth == 0 || limits.maxDepth > 128 || limits.maxNodes < 3)
        return Core::failure(ErrorCode::LimitExceeded, "Invalid archive limits");
    std::ostringstream stream;
    Core::JsonWriter output{stream, limits.maxDepth + 1};
    output.beginObject();
    output.member("schema", 1);
    Detail::WriteState state{output, limits, 2, 0, {}};
    Writer root{state, "data", "$.data"};
    if (auto result = root.writeWith(write); !result) return Core::failure(result.error());
    output.endObject();
    if (output.failed() || !output.balanced()) return Core::failure(ErrorCode::CodecFailed, "Incomplete JSON archive");
    const auto text = stream.str();
    if (text.size() > limits.maxBytes) return Core::failure(ErrorCode::LimitExceeded, "Encoded archive exceeds byte budget");
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
} catch (const std::bad_alloc&) {
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "Archive encoding allocation failed");
} catch (const std::exception& exception) {
    return Core::failure(ErrorCode::CodecFailed, exception.what());
} catch (...) { return Core::failure(ErrorCode::CodecFailed, "Encode callback threw"); }

Core::Result<Reader> decodeJson(std::span<const std::byte> bytes, ArchiveLimits limits)
try {
    if (limits.maxBytes == 0 || limits.maxDepth == 0 || limits.maxDepth > 128 || limits.maxNodes < 3)
        return Core::failure(ErrorCode::LimitExceeded, "Invalid archive limits");
    auto document = Core::JsonDocument::parse(bytes, {limits.maxBytes, limits.maxDepth, limits.maxNodes});
    if (!document) return Core::failure(document.error());
    Reader root{document->root(), "$", limits};
    if (auto result = root.fieldsOnly({"schema", "data"}); !result) return Core::failure(result.error());
    auto schema = root.field<Core::u32>("schema");
    if (!schema) return Core::failure(schema.error());
    if (*schema != 1) return Core::failure(ErrorCode::InvalidData, "Unsupported archive schema");
    return root.member("data");
} catch (const std::bad_alloc&) { return Core::failure(Core::CoreErrorCode::OutOfMemory, "Archive decoding allocation failed"); }

} // namespace Tina::Serialization
