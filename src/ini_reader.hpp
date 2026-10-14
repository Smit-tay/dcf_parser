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
// parse_ini — CiA 306 §4.2 INI tokeniser, tolerant where real files are sloppy
//
//   • Encoding: a UTF-8 byte order mark is skipped (Windows editors write one).
//     UTF-16 is not supported and is reported as an error rather than parsed
//     as garbage.
//   • Lines end with LF or CR+LF.
//   • Max 255 characters per line — lenient: we do NOT return an error for
//     longer lines (real-world files routinely exceed this for VisibleString
//     objects).
//   • Comment: a line whose first non-blank character is ';'.  The spec wants
//     the ';' in column 0; a key can never start with ';', so accepting leading
//     blanks cannot misread a real entry.  A ';' after '=' stays part of the
//     value here (string values may contain one); the integer parser strips it.
//   • Section header: a line whose first non-blank character is '['.  An
//     unterminated header is an error: everything after it would otherwise land
//     in the wrong section.
//   • Key names: not case-sensitive (lowercased on storage).
//   • Section names: not case-sensitive (lowercased on storage).
//   • A keyname that consists only of digits is still stored as a string key
//     (spec §4.2).  Object-list entries like "1=0x1000" are therefore looked up
//     by string "1", not integer 1.
//   • Sections and entries may appear in any order.
//   • Lines that are neither section headers, comments, nor key=value pairs are
//     skipped and reported in `warnings`.
//   • A synthetic empty-named section collects any entries that appear before
//     the first '[' header; callers typically ignore it.
// ---------------------------------------------------------------------------
std::expected<IniDocument, ParseError> parse_ini(std::string_view content,
                                                 std::vector<Warning>& warnings);

// Find a section by name — pass a lowercased name, returns nullptr if absent.
const IniSection* find_section(const IniDocument& doc, std::string_view name);

// Find an entry within a section — pass a lowercased key.
const IniEntry*   find_entry(const IniSection& sec, std::string_view key);

// Lowercase an ASCII string.
std::string ascii_lower(std::string_view s);

} // namespace eds::detail
