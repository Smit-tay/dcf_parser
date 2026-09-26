#include "ini_reader.hpp"
#include <algorithm>
#include <cctype>

namespace eds::detail {

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

static std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

std::expected<IniDocument, ParseError> parse_ini(std::string_view content) {
    IniDocument doc;
    // Synthetic section for entries that appear before the first [header].
    // Real EDS files never have such entries, but the slot simplifies
    // the "current section" pointer logic below.
    doc.push_back(IniSection{ .name = "", .line = 0, .entries = {} });
    IniSection* current = &doc.back();

    int lineNum = 0;
    std::string_view remaining = content;

    while (!remaining.empty()) {
        ++lineNum;

        // Split off one line.
        const auto eolPos = remaining.find('\n');
        const std::string_view rawLine = (eolPos == std::string_view::npos)
            ? remaining
            : remaining.substr(0, eolPos + 1);
        remaining = (eolPos == std::string_view::npos)
            ? std::string_view{}
            : remaining.substr(eolPos + 1);

        // Trimmed view for content inspection.
        const std::string_view line = trim(rawLine);
        if (line.empty())
            continue;

        // Comment: ';' in column 0 of the RAW line.
        // Spec §4.2: "Each line of a comment shall start with a semicolon (;).
        // It shall be in the leftmost column."  We check rawLine[0], not
        // line[0], so leading spaces do NOT make a non-comment line into a
        // comment.  A line like "  ;comment" is therefore malformed key=value,
        // not a comment — spec-faithful.
        if (rawLine[0] == ';')
            continue;

        // Section header: '[' in column 0 per §4.2.
        if (rawLine[0] == '[') {
            const auto close = line.find(']');
            if (close == std::string_view::npos)
                return std::unexpected(ParseError{
                    "unterminated section header", lineNum});
            const std::string_view name = trim(line.substr(1, close - 1));
            doc.push_back(IniSection{ .name = ascii_lower(name), .line = lineNum, .entries = {} });
            // doc may have reallocated — re-anchor the pointer.
            current = &doc.back();
            continue;
        }

        // key=value pair.
        const auto eqPos = line.find('=');
        if (eqPos == std::string_view::npos)
            continue; // malformed line — skip silently (lenient)

        std::string_view key   = trim(line.substr(0, eqPos));
        std::string_view value = trim(line.substr(eqPos + 1));

        if (key.empty())
            continue;

        // Note: we do NOT strip inline ';' from value.
        // Spec §4.2 says only column-0 semicolons are comments.
        // Lely strips inline ';' — that is non-standard.
        current->entries.push_back(IniEntry{
            .key   = ascii_lower(key),
            .value = std::string(value),
            .line  = lineNum,
        });
    }

    return doc;
}

const IniSection* find_section(const IniDocument& doc, std::string_view name) {
    for (const auto& s : doc)
        if (s.name == name)
            return &s;
    return nullptr;
}

const IniEntry* find_entry(const IniSection& sec, std::string_view key) {
    for (const auto& e : sec.entries)
        if (e.key == key)
            return &e;
    return nullptr;
}

} // namespace eds::detail
