#pragma once
// Internal INI layer — not part of the public API.
#include <eds/types.hpp>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace eds::detail {

struct IniEntry {
    std::string key;    // ASCII-lowercased
    std::string value;  // raw; whitespace-trimmed but NOT comment-stripped
    int         line{0}; // 1-based source line
};

struct IniSection {
    std::string            name;  // ASCII-lowercased
    int                    line{0};
    std::vector<IniEntry>  entries;
};

using IniDocument = std::vector<IniSection>;

// ---------------------------------------------------------------------------
// parse_ini — CiA 306 §4.2 faithful INI tokeniser
//
// Strict behaviours (what the spec actually says):
//   • Lines end with LF or CR+LF.
//   • Max 255 characters per line — lenient: we do NOT return an error for
//     longer lines (real-world files routinely exceed this for VisibleString
//     objects).  The limit is noted here; a strict mode can be added later.
//   • Section header: '[' must be in column 0 of the raw line.
//   • Comment: ';' must be in column 0 of the raw line.  A semicolon that
//     appears after '=' is part of the value, NOT a comment delimiter.
//     >>> This is where lely deviates: lely strips everything from ';'
//     >>> onwards in any value string ("inline comment" stripping).  The
//     >>> spec does not permit this.  We preserve the full raw value.
//   • Key names: any combination of letters and digits, not case-sensitive
//     (lowercased on storage).
//   • Section names: not case-sensitive (lowercased on storage).
//   • A keyname that consists only of digits is still stored as a string key
//     (spec §4.2: "If the keyname consists only of digits, it is interpreted
//     as a string, not as a number.").  Object-list entries like "1=0x1000"
//     are therefore looked up by string "1", not integer 1.
//   • Sections and entries may appear in any order.
//   • Lines that are neither section headers, comments, nor key=value pairs
//     are silently skipped (lenient).
//   • A synthetic empty-named section collects any entries that appear before
//     the first '[' header; callers typically ignore it.
// ---------------------------------------------------------------------------
std::expected<IniDocument, ParseError> parse_ini(std::string_view content);

// Find a section by name — pass a lowercased name, returns nullptr if absent.
const IniSection* find_section(const IniDocument& doc, std::string_view name);

// Find an entry within a section — pass a lowercased key.
const IniEntry*   find_entry(const IniSection& sec, std::string_view key);

// Lowercase an ASCII string.
std::string ascii_lower(std::string_view s);

} // namespace eds::detail
