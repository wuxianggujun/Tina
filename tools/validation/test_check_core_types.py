"""Headless regression tests for the Core scalar source-policy check."""

from pathlib import Path
import tempfile
import unittest

from check_core_types import (
    IDENTITY_TEST,
    SCALAR_ALIASES,
    TYPES_HEADER,
    check_source,
    source_files,
)


class CoreTypePolicyTest(unittest.TestCase):
    def test_all_standard_scalars_have_the_correct_replacement(self):
        source = "\n".join(f"std::{name} field;" for name in SCALAR_ALIASES)
        violations = check_source("src/example.cpp", source)
        self.assertEqual(len(violations), len(SCALAR_ALIASES))
        for line, (entry, alias) in enumerate(zip(violations, SCALAR_ALIASES.values()), 1):
            self.assertEqual(entry.line, line)
            self.assertEqual(entry.column, 1)
            self.assertEqual(entry.core_type, f"Tina::Core::{alias}")

    def test_containers_algorithms_and_private_expected_are_not_banned(self):
        source = """
            std::vector<Tina::Core::u32> values;
            std::pmr::vector<std::string> labels;
            std::span<const Tina::Core::u8> bytes;
            std::unique_ptr<State> owner;
            std::expected<int, PrivateError> result;
            auto smallest = std::min(left, right);
        """
        self.assertEqual(check_source("src/example.cpp", source), [])

    def test_line_and_block_comments_are_not_code(self):
        source = "// std::uint32_t\n/* std::size_t\nstd::uintptr_t */\nstd::int8_t live;"
        entries = check_source("src/example.cpp", source)
        self.assertEqual([(entry.line, entry.standard_type) for entry in entries], [(4, "std::int8_t")])

    def test_ordinary_and_character_literals_are_not_code(self):
        source = r'''auto a = "std::size_t and \"std::uint8_t\"";
auto b = u8"std::uint64_t";
auto c = '\''; auto d = L'"';
std::uint16_t live;'''
        entries = check_source("src/example.cpp", source)
        self.assertEqual([(entry.line, entry.standard_type) for entry in entries], [(4, "std::uint16_t")])

    def test_multiline_raw_strings_with_custom_delimiters_are_not_code(self):
        source = '''auto a = R"tag(std::uint8_t " // std::size_t
std::int64_t)tag";
auto b = u8R"(std::uintptr_t)";
std::uint32_t live;'''
        entries = check_source("src/example.cpp", source)
        self.assertEqual([(entry.line, entry.standard_type) for entry in entries], [(4, "std::uint32_t")])

    def test_digit_separators_do_not_hide_following_code(self):
        source = "auto value = 1'000'000 + sizeof(std::uint32_t); auto hex = 0xAB'CD; std::size_t count;"
        entries = check_source("src/example.cpp", source)
        self.assertEqual([entry.standard_type for entry in entries], ["std::uint32_t", "std::size_t"])

    def test_comments_between_namespace_tokens_are_whitespace(self):
        entries = check_source("src/example.cpp", "::std /* comment */ ::\n size_t count;")
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0].standard_type, "std::size_t")
        self.assertEqual((entries[0].line, entries[0].column), (1, 3))

    def test_line_splicing_preserves_physical_diagnostic_locations(self):
        source = "// continuation \\\r\nstd::size_t notCode;\r\n  std::ui\\\r\nnt32_t live;"
        entries = check_source("src/example.cpp", source)
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0].standard_type, "std::uint32_t")
        self.assertEqual((entries[0].line, entries[0].column), (3, 3))

    def test_inactive_preprocessor_branches_are_still_checked(self):
        source = "#if 0\nstd::size_t count;\n#endif"
        self.assertEqual(len(check_source("src/example.cpp", source)), 1)

    def test_type_definitions_are_exempt_only_at_the_definition_point(self):
        source = "\n".join(f"using {alias} = std::{name};" for name, alias in SCALAR_ALIASES.items())
        self.assertEqual(check_source(TYPES_HEADER, source), [])
        self.assertEqual(len(check_source("include/tina/other.hpp", source)), len(SCALAR_ALIASES))
        self.assertEqual(len(check_source(TYPES_HEADER, "using u32 = std::uint64_t;")), 1)
        self.assertEqual(len(check_source(TYPES_HEADER, "std::size_t helper();")), 1)

    def test_identity_assertions_are_narrow_exceptions_not_a_file_exemption(self):
        source = "static_assert(std::is_same_v<Core::u32, std::uint32_t>);"
        self.assertEqual(check_source(IDENTITY_TEST, source), [])
        self.assertEqual(len(check_source("tests/other.cpp", source)), 1)
        self.assertEqual(len(check_source(IDENTITY_TEST, "std::size_t ordinaryVariable;")), 1)
        self.assertEqual(len(check_source(IDENTITY_TEST, source.replace("Core::u32", "Core::u64"))), 1)

    def test_utf8_text_and_crlf_do_not_shift_lines(self):
        source = '// 中文\r\nconst char* value = "地图";\r\n    std::size_t count;'
        entry, = check_source("src/example.cpp", source)
        self.assertEqual((entry.line, entry.column), (3, 5))

    def test_source_discovery_is_scoped_and_excludes_generated_and_vendor_files(self):
        with tempfile.TemporaryDirectory(prefix="tina-core-policy-") as directory:
            root = Path(directory)
            files = [
                "src/runtime/Example.cpp", "include/tina/Example.hpp", "cmake/Example.cpp.in",
                "src/out/generated.cpp", "src/nested/thirdparty/vendor.cpp",
                "out/huge.cpp", "thirdparty/vendor.cpp", "tests/render_bgfx/fakes/bgfx/bgfx.h",
                "editor/app/node_modules/vendor.cpp", "src/runtime/image.png",
            ]
            for name in files:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("std::size_t count;", encoding="utf-8")
            actual = {relative for _, relative in source_files(root)}
            self.assertEqual(actual, {"src/runtime/Example.cpp", "include/tina/Example.hpp", "cmake/Example.cpp.in"})
        self.assertFalse(root.exists(), "the fixture directory must be reclaimed")


if __name__ == "__main__":
    unittest.main()
