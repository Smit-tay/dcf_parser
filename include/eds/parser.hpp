#pragma once
#include "model.hpp"
#include "types.hpp"
#include <expected>
#include <filesystem>
#include <string_view>

namespace eds {

// ---------------------------------------------------------------------------
// Parse EDS / DCF content from memory.
//
// Both functions accept the raw file content as a string_view — the caller
// owns the backing storage and it must outlive the call (it is not retained).
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
// Accepts decimal ("10"), hex ("0x0A", "0xa", "0X000A"), octal ("012" → 8),
// and $NODEID formula ("$NODEID+0x200", "$NODEID").
//
// Returns ParseError for any string that is not a valid integer expression.
// Empty string is NOT valid — callers must check before calling.
//
// Non-standard lely behaviours (documented here for awareness):
//   • Lely accepts "$nodeid" in any case.  The spec (§4.3 EBNF) writes
//     $NODEID verbatim and does not state case-insensitivity for it.
//     This parser requires uppercase "$NODEID".
//   • Lely strips inline semicolons from values before parsing, turning
//     "0x200;comment" into "0x200".  The spec §4.2 only marks column-0
//     semicolons as comments.  This parser does NOT strip inline semicolons
//     — the raw value is passed through so callers receive the full string.
//     If a value genuinely contains a semicolon (unusual but not forbidden),
//     the caller sees it as part of the value.
// ---------------------------------------------------------------------------
std::expected<IntValue, ParseError> parse_int_value(std::string_view s);

// Resolve an IntValue to a concrete uint32_t.
// If v holds a uint64_t it is returned (truncated to 32 bits).
// If v holds a NodeIdFormula, nodeId + formula.offset is returned.
uint32_t resolve_int_value(const IntValue& v, uint8_t nodeId = 0);

// ---------------------------------------------------------------------------
// Diagnostic stringifiers
// ---------------------------------------------------------------------------
std::string_view to_string(ObjectType);
std::string_view to_string(AccessType);
std::string_view to_string(DataType);

} // namespace eds
