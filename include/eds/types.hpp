#pragma once
#include <cstdint>
#include <string>
#include <variant>

namespace eds {

// CiA 301 §7.4.7 — data type indices used in EDS/DCF DataType= entries.
enum class DataType : uint16_t {
    Boolean         = 0x0001,
    Integer8        = 0x0002,
    Integer16       = 0x0003,
    Integer32       = 0x0004,
    Unsigned8       = 0x0005,
    Unsigned16      = 0x0006,
    Unsigned32      = 0x0007,
    Real32          = 0x0008,
    VisibleString   = 0x0009,
    OctetString     = 0x000A,
    UnicodeString   = 0x000B,
    TimeOfDay       = 0x000C,
    TimeDifference  = 0x000D,
    Domain          = 0x000F,
    Integer24       = 0x0010,
    Real64          = 0x0011,
    Integer40       = 0x0012,
    Integer48       = 0x0013,
    Integer56       = 0x0014,
    Integer64       = 0x0015,
    Unsigned24      = 0x0016,
    Unsigned40      = 0x0018,
    Unsigned48      = 0x0019,
    Unsigned56      = 0x001A,
    Unsigned64      = 0x001B,
    Unknown         = 0x0000,
};

// CiA 306 §4.6.3.2 — object type codes (ObjectType= key).
enum class ObjectType : uint8_t {
    Null      = 0x00,
    Domain    = 0x02,
    DefType   = 0x05,
    DefStruct = 0x06,
    Var       = 0x07,   // default when ObjectType is missing per spec §4.6.3.2
    Array     = 0x08,
    Record    = 0x09,
};

// CiA 306 §4.6.3.2 — access types (AccessType= key).
enum class AccessType {
    ReadOnly,           // "ro"
    WriteOnly,          // "wo"
    ReadWrite,          // "rw"
    ReadWriteProcess,   // "rwr" — read/write on process input
    ReadWriteOutput,    // "rww" — read/write on process output
    Const,              // "const"
};

// CiA 306 §4.3 — $NODEID formula: IntEntryValue = $NODEID { "+" number }
// offset is 0 for a bare "$NODEID" with no addition.
struct NodeIdFormula {
    uint32_t offset{0};
    constexpr bool operator==(const NodeIdFormula&) const noexcept = default;
};

// A parsed integer value from an EDS entry.  Either an absolute value or a
// node-id-relative formula.  Callers that need a concrete uint32_t call
// resolve_int_value() from parser.hpp.
using IntValue = std::variant<uint64_t, NodeIdFormula>;

// Error type returned by all parse operations.
struct ParseError {
    std::string message;
    int line{0};    // 1-based source line, 0 if location is not known

    ParseError(std::string msg, int ln = 0)
        : message(std::move(msg)), line(ln) {}
};

} // namespace eds
