#pragma once
#include "model.hpp"
#include "types.hpp"
#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

namespace eds {

// ---------------------------------------------------------------------------
// Parse EDS / DCF content from memory.
//
// Both functions accept the raw file content as a string_view — the caller
// owns the backing storage and it must outlive the call (it is not retained).
//
// A hard error (unterminated section header, UTF-16 file) returns ParseError.
// Everything else that is skipped or unreadable is listed in the result's
// `warnings`; an empty list means the whole file was understood.
//
// parse_eds() silently ignores DCF-only keys (ParameterValue, LastEDS,
// [DeviceComissioning]); they appear in EdsFile::lastEds and ObjectEntry
// fields but do not cause errors.
//
// parse_dcf() reads all EDS keys plus the DCF commissioning section.
// ---------------------------------------------------------------------------
std::expected<EdsFile, ParseError> parse_eds(std::string_view content);
std::expected<DcfFile, ParseError> parse_dcf(std::string_view content);

// Convenience wrappers that read the file from disk first.
std::expected<EdsFile, ParseError> parse_eds_file(const std::filesystem::path& path);
std::expected<DcfFile, ParseError> parse_dcf_file(const std::filesystem::path& path);

// ---------------------------------------------------------------------------
// Integer value parsing — CiA 306 §4.3
//
// An integer expression is one or more terms joined by '+'.  A term is a
// number — decimal ("10"), hex ("0x0A", "0X000A"), octal ("012" → 10), each
// optionally negative ("-12") — or the token $NODEID.  So all of
//   "0x200"   "$NODEID"   "$NODEID+0x200"   "1280+$NODEID"   "$NODEID+0x80+1"
// are accepted.  $NODEID may appear once, in any position.
//
// Result: a plain number as uint64_t, or — if $NODEID occurs — a NodeIdFormula
// whose offset is the sum of the other terms.
//
// Negative numbers come back as two's complement in the uint64_t ("-12" is
// 0xFFFFFFFFFFFFFFF4).  Narrow to the object's width with sign_extend() below.
//
// Returns ParseError for any string that is not a valid integer expression.
// Empty string is NOT valid — callers must check before calling.
//
// Deliberately lenient where real files differ from the spec (all of these
// are things Lely accepts too):
//   • "$NODEID" in any case, with blanks around the '+'.
//   • "$NODEID" after a number ("1280+$NODEID").
//   • A trailing ";comment" is cut off before parsing ("0x200 ;comment").
//     Only here — string values keep their ';'.
// ---------------------------------------------------------------------------
std::expected<IntValue, ParseError> parse_int_value(std::string_view s);

// Resolve an IntValue to a concrete uint32_t.
// If v holds a uint64_t it is returned (truncated to 32 bits).
// If v holds a NodeIdFormula, nodeId + formula.offset is returned.
uint32_t resolve_int_value(const IntValue& v, uint8_t nodeId = 0);

// ---------------------------------------------------------------------------
// Value helpers
// ---------------------------------------------------------------------------

// Width in bits of a fixed-size data type; 0 for VISIBLE_STRING, OCTET_STRING,
// UNICODE_STRING, DOMAIN and UNKNOWN (no fixed width).
unsigned data_type_bits(DataType);

// Interprets the low `bits` bits of `raw` as a two's complement number.  Use it
// on the result of parse_int_value() for INTEGERn defaults and limits:
// sign_extend(0xFFFFFFFFFF, 40) is -1, sign_extend(raw_of("-12"), 8) is -12.
// bits == 0 or >= 64 returns raw unchanged.
int64_t sign_extend(uint64_t raw, unsigned bits);

// The value the device actually holds: in a DCF, ParameterValue overrides
// DefaultValue (CiA 306 §5.3.1).  Empty when the file gives neither.
inline const std::optional<std::string>& effective_value(const ObjectEntry& o) {
    return o.parameterValue ? o.parameterValue : o.defaultValue;
}
inline const std::optional<std::string>& effective_value(const SubObjectEntry& s) {
    return s.parameterValue ? s.parameterValue : s.defaultValue;
}

// Encodes the text of a DefaultValue / ParameterValue into the bytes the object
// holds, in the object's own data type:
//   • BOOLEAN: one byte, 0 or 1.
//   • INTEGERn / UNSIGNEDn: n/8 bytes, little endian, two's complement for
//     negative numbers.  A $NODEID formula is resolved with `nodeId`.
//   • REAL32 / REAL64: IEEE 754, little endian.
//   • VISIBLE_STRING: the characters, no terminating zero.
//   • OCTET_STRING: two hex digits per byte (blanks between bytes are ignored).
//   • DOMAIN: always empty.
// Returns ParseError for text that does not fit the type (including a number
// that does not fit into the type's width) and for the types it does not
// encode: UNICODE_STRING, TIME_OF_DAY, TIME_DIFFERENCE, Unknown.
// Empty text is NOT valid — callers must check before calling.
std::expected<std::vector<uint8_t>, ParseError> encode_value(
    DataType type, std::string_view text, uint8_t nodeId = 0);

// ---------------------------------------------------------------------------
// Diagnostic stringifiers
// ---------------------------------------------------------------------------
std::string_view to_string(ObjectType);
std::string_view to_string(AccessType);
std::string_view to_string(DataType);

} // namespace eds
