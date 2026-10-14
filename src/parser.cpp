#include <eds/parser.hpp>
#include "ini_reader.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace eds {

using namespace detail;

// ---------------------------------------------------------------------------
// §4.3 Integer value parsing
// ---------------------------------------------------------------------------

namespace {

std::string_view trim_blanks(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

// One number: [-]dec | [-]0x<hex> | [-]0<octal>.  The value is modulo 2^64, so
// a negative number comes out as its two's complement.
std::optional<uint64_t> parse_number_term(std::string_view t) {
    bool negative = false;
    if (!t.empty() && t.front() == '-') {
        negative = true;
        t = trim_blanks(t.substr(1));
    }
    // §4.3: "Hexadecimal numbers are preceded by 0x."
    //       "Octal numbers shall start with a leading 0 (not followed by x)."
    int base = 10;
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        base = 16;
        t.remove_prefix(2);
    } else if (t.size() >= 2 && t[0] == '0' && t[1] >= '0' && t[1] <= '7') {
        base = 8;
        t.remove_prefix(1);
    }
    if (t.empty())
        return std::nullopt;

    uint64_t magnitude = 0;
    const char* const end = t.data() + t.size();
    const auto [ptr, ec] = std::from_chars(t.data(), end, magnitude, base);
    if (ec != std::errc{} || ptr != end)
        return std::nullopt;
    return negative ? uint64_t{0} - magnitude : magnitude;
}

} // namespace

std::expected<IntValue, ParseError> parse_int_value(std::string_view s) {
    // A trailing ";comment" is not part of the number (lely strips it as well).
    if (const auto semi = s.find(';'); semi != std::string_view::npos)
        s = s.substr(0, semi);
    s = trim_blanks(s);
    if (s.empty())
        return std::unexpected(ParseError{"empty string is not an integer value"});

    const auto bad = [s](const char* why) {
        return std::unexpected(ParseError{
            std::string("not a valid integer expression (").append(why).append("): '").append(s).append("'")});
    };

    // Terms joined by '+', each a number or $NODEID (once, anywhere).
    bool hasNodeId = false;
    uint64_t sum = 0;
    for (std::string_view rest = s;;) {
        const auto plus = rest.find('+');
        const std::string_view term = trim_blanks(rest.substr(0, plus));

        if (ascii_lower(term) == "$nodeid") {
            if (hasNodeId)
                return bad("$NODEID twice");
            hasNodeId = true;
        } else if (const auto number = parse_number_term(term)) {
            sum += *number;
        } else {
            return bad("bad number");
        }

        if (plus == std::string_view::npos)
            break;
        rest.remove_prefix(plus + 1);
    }

    if (hasNodeId)
        return NodeIdFormula{static_cast<uint32_t>(sum)};
    return sum;
}

uint32_t resolve_int_value(const IntValue& v, uint8_t nodeId) {
    return std::visit([nodeId](auto&& arg) -> uint32_t {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, uint64_t>)
            return static_cast<uint32_t>(arg);
        else
            return static_cast<uint32_t>(nodeId) + arg.offset;
    }, v);
}

unsigned data_type_bits(DataType t) {
    switch (t) {
        case DataType::Boolean:        return 1;
        case DataType::Integer8:
        case DataType::Unsigned8:      return 8;
        case DataType::Integer16:
        case DataType::Unsigned16:     return 16;
        case DataType::Integer24:
        case DataType::Unsigned24:     return 24;
        case DataType::Integer32:
        case DataType::Unsigned32:
        case DataType::Real32:         return 32;
        case DataType::Integer40:
        case DataType::Unsigned40:     return 40;
        case DataType::Integer48:
        case DataType::Unsigned48:
        case DataType::TimeOfDay:
        case DataType::TimeDifference: return 48;
        case DataType::Integer56:
        case DataType::Unsigned56:     return 56;
        case DataType::Integer64:
        case DataType::Unsigned64:
        case DataType::Real64:         return 64;
        case DataType::VisibleString:
        case DataType::OctetString:
        case DataType::UnicodeString:
        case DataType::Domain:
        case DataType::Unknown:        return 0;
    }
    return 0;
}

int64_t sign_extend(uint64_t raw, unsigned bits) {
    if (bits == 0 || bits >= 64)
        return static_cast<int64_t>(raw);
    const uint64_t signBit = uint64_t{1} << (bits - 1);
    raw &= (uint64_t{1} << bits) - 1;
    return static_cast<int64_t>((raw ^ signBit) - signBit);
}

std::expected<std::vector<uint8_t>, ParseError> encode_value(
    DataType type, std::string_view text, uint8_t nodeId) {
    const auto bad = [&](const char* why) {
        return std::unexpected(ParseError(std::string(why) + ": \"" + std::string(text) + "\""));
    };

    switch (type) {
        case DataType::Domain:
            return std::vector<uint8_t>{};

        case DataType::VisibleString:
            return std::vector<uint8_t>(text.begin(), text.end());

        case DataType::OctetString: {
            std::vector<uint8_t> bytes;
            int high = -1;
            for (const char c : text) {
                if (c == ' ' || c == '\t') {
                    if (high >= 0) return bad("odd number of hex digits");
                    continue;
                }
                int digit;
                if (c >= '0' && c <= '9')      digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                else return bad("not a hex digit");
                if (high < 0) high = digit;
                else { bytes.push_back(static_cast<uint8_t>(high << 4 | digit)); high = -1; }
            }
            if (high >= 0) return bad("odd number of hex digits");
            return bytes;
        }

        case DataType::Real32:
        case DataType::Real64: {
            // Same cut-off as parse_int_value: a trailing ";comment" is not part of the number
            std::string_view number = text.substr(0, text.find(';'));
            while (!number.empty() && (number.front() == ' ' || number.front() == '\t')) number.remove_prefix(1);
            while (!number.empty() && (number.back() == ' ' || number.back() == '\t')) number.remove_suffix(1);
            if (!number.empty() && number.front() == '+') number.remove_prefix(1);
            double value = 0;
            const auto [end, ec] = std::from_chars(number.data(), number.data() + number.size(), value);
            if (ec != std::errc{} || end != number.data() + number.size()) return bad("not a floating point number");
            std::vector<uint8_t> bytes;
            if (type == DataType::Real32) {
                const float f = static_cast<float>(value);
                uint32_t bits;
                std::memcpy(&bits, &f, sizeof bits);
                for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(bits >> (8 * i)));
            } else {
                uint64_t bits;
                std::memcpy(&bits, &value, sizeof bits);
                for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(bits >> (8 * i)));
            }
            return bytes;
        }

        case DataType::UnicodeString:
        case DataType::TimeOfDay:
        case DataType::TimeDifference:
        case DataType::Unknown:
            return bad("this data type cannot be encoded");

        // The integer types are encoded below. Listed one by one (no default) so that a new data type
        // is flagged here by the compiler's switch warnings.
        case DataType::Boolean:
        case DataType::Integer8:
        case DataType::Integer16:
        case DataType::Integer24:
        case DataType::Integer32:
        case DataType::Integer40:
        case DataType::Integer48:
        case DataType::Integer56:
        case DataType::Integer64:
        case DataType::Unsigned8:
        case DataType::Unsigned16:
        case DataType::Unsigned24:
        case DataType::Unsigned32:
        case DataType::Unsigned40:
        case DataType::Unsigned48:
        case DataType::Unsigned56:
        case DataType::Unsigned64:
            break;
    }

    const auto parsed = parse_int_value(text);
    if (!parsed) return std::unexpected(parsed.error());
    const uint64_t raw = std::visit([nodeId](auto&& v) -> uint64_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, uint64_t>) return v;
        else return static_cast<uint64_t>(nodeId) + v.offset;
    }, *parsed);

    if (type == DataType::Boolean) return std::vector<uint8_t>{static_cast<uint8_t>(raw != 0 ? 1 : 0)};

    const unsigned bits = data_type_bits(type);
    const bool isSigned = type == DataType::Integer8 || type == DataType::Integer16 || type == DataType::Integer24 ||
                          type == DataType::Integer32 || type == DataType::Integer40 || type == DataType::Integer48 ||
                          type == DataType::Integer56 || type == DataType::Integer64;
    if (bits < 64) {
        // Must fit the width: as an unsigned number, or (negative numbers arrive as two's complement
        // of 64 bits) as a signed one for the signed types.
        const uint64_t limit = uint64_t{1} << bits;
        const bool fitsUnsigned = raw < limit;
        const bool fitsSigned = isSigned && sign_extend(raw, bits) == static_cast<int64_t>(raw);
        if (!fitsUnsigned && !fitsSigned) return bad("value does not fit the data type");
    }
    std::vector<uint8_t> bytes(bits / 8);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(raw >> (8 * i));
    return bytes;
}

// ---------------------------------------------------------------------------
// Diagnostic stringifiers
// ---------------------------------------------------------------------------

std::string_view to_string(ObjectType t) {
    switch (t) {
        case ObjectType::Null:      return "NULL";
        case ObjectType::Domain:    return "DOMAIN";
        case ObjectType::DefType:   return "DEFTYPE";
        case ObjectType::DefStruct: return "DEFSTRUCT";
        case ObjectType::Var:       return "VAR";
        case ObjectType::Array:     return "ARRAY";
        case ObjectType::Record:    return "RECORD";
    }
    return "UNKNOWN";
}

std::string_view to_string(AccessType t) {
    switch (t) {
        case AccessType::ReadOnly:          return "ro";
        case AccessType::WriteOnly:         return "wo";
        case AccessType::ReadWrite:         return "rw";
        case AccessType::ReadWriteProcess:  return "rwr";
        case AccessType::ReadWriteOutput:   return "rww";
        case AccessType::Const:             return "const";
    }
    return "rw";
}

std::string_view to_string(DataType t) {
    switch (t) {
        case DataType::Boolean:       return "BOOLEAN";
        case DataType::Integer8:      return "INTEGER8";
        case DataType::Integer16:     return "INTEGER16";
        case DataType::Integer32:     return "INTEGER32";
        case DataType::Unsigned8:     return "UNSIGNED8";
        case DataType::Unsigned16:    return "UNSIGNED16";
        case DataType::Unsigned32:    return "UNSIGNED32";
        case DataType::Real32:        return "REAL32";
        case DataType::VisibleString: return "VISIBLE_STRING";
        case DataType::OctetString:   return "OCTET_STRING";
        case DataType::UnicodeString: return "UNICODE_STRING";
        case DataType::TimeOfDay:     return "TIME_OF_DAY";
        case DataType::TimeDifference:return "TIME_DIFFERENCE";
        case DataType::Domain:        return "DOMAIN";
        case DataType::Integer24:     return "INTEGER24";
        case DataType::Real64:        return "REAL64";
        case DataType::Integer40:     return "INTEGER40";
        case DataType::Integer48:     return "INTEGER48";
        case DataType::Integer56:     return "INTEGER56";
        case DataType::Integer64:     return "INTEGER64";
        case DataType::Unsigned24:    return "UNSIGNED24";
        case DataType::Unsigned40:    return "UNSIGNED40";
        case DataType::Unsigned48:    return "UNSIGNED48";
        case DataType::Unsigned56:    return "UNSIGNED56";
        case DataType::Unsigned64:    return "UNSIGNED64";
        case DataType::Unknown:       return "UNKNOWN";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// Internal model-building helpers
// ---------------------------------------------------------------------------

namespace {

// Where warn() collects.  The model-building helpers below are many and small;
// threading a list through every one of them would bury the logic, so the list
// of the file being parsed is installed here for the duration of the call
// (WarningScope).  Thread-local, so parsing on several threads is fine.
thread_local std::vector<Warning>* g_warnings = nullptr;

void warn(int line, std::string message) {
    if (g_warnings)
        g_warnings->push_back(Warning{std::move(message), line});
}

class WarningScope {
public:
    explicit WarningScope(std::vector<Warning>& list) : previous_(g_warnings) { g_warnings = &list; }
    ~WarningScope() { g_warnings = previous_; }
    WarningScope(const WarningScope&) = delete;
    WarningScope& operator=(const WarningScope&) = delete;
private:
    std::vector<Warning>* previous_;
};

std::string read_string(const IniSection& sec, std::string_view key) {
    if (const auto* e = find_entry(sec, key))
        return e->value;
    return {};
}

// A plain number.  A missing or empty key is just "absent"; a key that is
// present but unreadable, or that holds a $NODEID formula where only a plain
// number makes sense, is reported.
std::optional<uint64_t> read_uint(const IniSection& sec, std::string_view key) {
    const auto* e = find_entry(sec, key);
    if (!e || e->value.empty()) return std::nullopt;
    auto r = parse_int_value(e->value);
    if (!r) {
        warn(e->line, "[" + sec.name + "] " + std::string(key) + ": " + r.error().message + " - ignored");
        return std::nullopt;
    }
    if (!std::holds_alternative<uint64_t>(*r)) {
        warn(e->line, "[" + sec.name + "] " + std::string(key) + ": $NODEID is not allowed here: '" + e->value + "' - ignored");
        return std::nullopt;
    }
    return std::get<uint64_t>(*r);
}

bool read_bool(const IniSection& sec, std::string_view key, bool def = false) {
    const auto v = read_uint(sec, key);
    return v ? (*v != 0) : def;
}

std::optional<AccessType> parse_access_type(std::string_view s) {
    if (s == "ro")    return AccessType::ReadOnly;
    if (s == "wo")    return AccessType::WriteOnly;
    if (s == "rw")    return AccessType::ReadWrite;
    if (s == "rwr")   return AccessType::ReadWriteProcess;
    if (s == "rww")   return AccessType::ReadWriteOutput;
    if (s == "const") return AccessType::Const;
    return std::nullopt;
}

// "1A00" for 0x1A00 — the way indices are written in messages.
std::string upper_hex(uint16_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned>(v));
    return buf;
}

// Parse a 4-hex-digit index string with no "0x" prefix — spec §4.6.3.2.
std::optional<uint16_t> parse_hex_index(std::string_view s) {
    if (s.empty() || s.size() > 4) return std::nullopt;
    uint16_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 16);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

// Parse the hex digits after "sub" — 1 or 2 hex digits, e.g. "0", "1", "FF".
std::optional<uint8_t> parse_sub_index(std::string_view s) {
    if (s.empty() || s.size() > 2) return std::nullopt;
    uint8_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 16);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

// Populate the fields common to both ObjectEntry and SubObjectEntry.
// Returns the AccessType string lowercased, or "ro" default.
template<typename T>
void fill_common_fields(T& obj, const IniSection& sec) {
    obj.parameterName = read_string(sec, "parametername");

    if (auto v = read_uint(sec, "objecttype"))
        obj.objectType = static_cast<ObjectType>(*v);
    else
        obj.objectType = ObjectType::Var; // spec §4.6.3.2 default

    if (auto v = read_uint(sec, "datatype"))
        obj.dataType = static_cast<DataType>(*v);

    if (const auto* e = find_entry(sec, "accesstype")) {
        if (auto at = parse_access_type(ascii_lower(e->value)))
            obj.accessType = *at;
        else
            warn(e->line, "[" + sec.name + "] AccessType: unknown value '" + e->value + "' - read-only assumed");
    }

    if (const auto* e = find_entry(sec, "defaultvalue"); e && !e->value.empty())
        obj.defaultValue = e->value;

    if (const auto* e = find_entry(sec, "lowlimit"); e && !e->value.empty())
        obj.lowLimit = e->value;

    if (const auto* e = find_entry(sec, "highlimit"); e && !e->value.empty())
        obj.highLimit = e->value;

    obj.pdoMapping = read_bool(sec, "pdomapping");

    if (auto v = read_uint(sec, "objflags"))
        obj.objFlags = static_cast<uint32_t>(*v);

    // DCF extensions — §5.3.1 / §5.3.2
    if (const auto* e = find_entry(sec, "parametervalue"); e && !e->value.empty())
        obj.parameterValue = e->value;
    if (const auto* e = find_entry(sec, "denotation"); e && !e->value.empty())
        obj.denotation = e->value;
    if (const auto* e = find_entry(sec, "uploadfile"); e && !e->value.empty())
        obj.uploadFile = e->value;
    if (const auto* e = find_entry(sec, "downloadfile"); e && !e->value.empty())
        obj.downloadFile = e->value;
}

SubObjectEntry parse_sub_entry(const IniSection& sec) {
    SubObjectEntry e;
    fill_common_fields(e, sec);
    return e;
}

ObjectEntry parse_object_entry(uint16_t index, const IniSection& sec) {
    ObjectEntry e;
    e.index = index;
    fill_common_fields(e, sec);

    if (auto v = read_uint(sec, "subnumber"))
        e.subNumber = static_cast<uint8_t>(*v);

    if (auto v = read_uint(sec, "compactsubobj"))
        e.compactSubObj = static_cast<uint8_t>(*v);

    return e;
}

// §4.6.3.4.2 — synthesise sub-objects for a compact ARRAY.
// Spec rules:
//   sub0  : type UNSIGNED8, access ReadOnly, defaultValue = CompactSubObj count
//   sub1..N: type = main DataType, access = main AccessType,
//            defaultValue = main DefaultValue, pdoMapping = main PDOMapping
//   Names : "ParameterNameN" unless overridden by [xxxxName]
void synthesise_compact_sub_objects(ObjectEntry& obj) {
    const uint8_t count = obj.compactSubObj;
    if (count == 0) return;

    SubObjectEntry sub0;
    sub0.parameterName = "NrOfObjects";
    sub0.objectType    = ObjectType::Var;
    sub0.dataType      = DataType::Unsigned8;
    sub0.accessType    = AccessType::ReadOnly;
    sub0.defaultValue  = std::to_string(count);
    obj.subObjects[0x00] = std::move(sub0);

    for (uint8_t i = 1; i <= count; ++i) {
        SubObjectEntry sub;
        if (const auto it = obj.compactNames.find(i); it != obj.compactNames.end())
            sub.parameterName = it->second;
        else
            sub.parameterName = obj.parameterName + std::to_string(i);

        sub.objectType   = ObjectType::Var;
        sub.dataType     = obj.dataType;
        sub.accessType   = obj.accessType;
        sub.defaultValue = obj.defaultValue;
        sub.pdoMapping   = obj.pdoMapping;

        if (const auto it = obj.compactValues.find(i); it != obj.compactValues.end())
            sub.parameterValue = it->second;

        obj.subObjects[i] = std::move(sub);
    }
}

// Parse a SupportedObjects= list from [MandatoryObjects] / [OptionalObjects] /
// [ManufacturerObjects].  Spec §4.6.1: entries numbered 1..N where the last
// entry number equals SupportedObjects.
std::vector<uint16_t> parse_object_list(const IniSection* sec) {
    if (!sec) return {};
    const auto* countEntry = find_entry(*sec, "supportedobjects");
    if (!countEntry || countEntry->value.empty()) return {};

    const auto n64 = read_uint(*sec, "supportedobjects");
    if (!n64) return {};
    // A list this long cannot be real (indices are 16 bit); do not reserve for garbage.
    const uint64_t n = std::min<uint64_t>(*n64, 0x10000);

    std::vector<uint16_t> indices;
    indices.reserve(static_cast<size_t>(n));
    for (uint64_t i = 1; i <= n; ++i) {
        const auto key = std::to_string(i);
        const auto* e = find_entry(*sec, key);
        if (!e) {
            warn(sec->line, "[" + sec->name + "] SupportedObjects=" + std::to_string(*n64) +
                            " but entry " + key + " is missing - the rest of the list is ignored");
            break; // stop at first gap
        }
        if (const auto v = read_uint(*sec, key))
            indices.push_back(static_cast<uint16_t>(*v));
    }
    // The count is what decides, not the entries that happen to follow it.
    // Entries past it are almost always a count that was not updated.
    if (find_entry(*sec, std::to_string(n + 1)))
        warn(countEntry->line, "[" + sec->name + "] SupportedObjects=" + std::to_string(*n64) +
                               " but entry " + std::to_string(n + 1) + " exists - entries past the count are ignored");
    return indices;
}

FileInfo parse_file_info(const IniDocument& doc) {
    FileInfo fi;
    const auto* sec = find_section(doc, "fileinfo");
    if (!sec) return fi;

    fi.fileName         = read_string(*sec, "filename");
    if (auto v = read_uint(*sec, "fileversion"))  fi.fileVersion  = static_cast<uint8_t>(*v);
    if (auto v = read_uint(*sec, "filerevision")) fi.fileRevision = static_cast<uint8_t>(*v);
    fi.edsVersion       = read_string(*sec, "edsversion");
    if (fi.edsVersion.empty()) fi.edsVersion = "3.0"; // spec §4.4 default
    fi.description      = read_string(*sec, "description");
    fi.creationTime     = read_string(*sec, "creationtime");
    fi.creationDate     = read_string(*sec, "creationdate");
    fi.createdBy        = read_string(*sec, "createdby");
    fi.modificationTime = read_string(*sec, "modificationtime");
    fi.modificationDate = read_string(*sec, "modificationdate");
    fi.modifiedBy       = read_string(*sec, "modifiedby");
    return fi;
}

DeviceInfo parse_device_info(const IniDocument& doc) {
    DeviceInfo di;
    const auto* sec = find_section(doc, "deviceinfo");
    if (!sec) return di;

    di.vendorName  = read_string(*sec, "vendorname");
    if (auto v = read_uint(*sec, "vendornumber"))   di.vendorNumber   = static_cast<uint32_t>(*v);
    di.productName = read_string(*sec, "productname");
    if (auto v = read_uint(*sec, "productnumber"))  di.productNumber  = static_cast<uint32_t>(*v);
    if (auto v = read_uint(*sec, "revisionnumber")) di.revisionNumber = static_cast<uint32_t>(*v);
    di.orderCode   = read_string(*sec, "ordercode");

    di.baudRate10   = read_bool(*sec, "baudrate_10");
    di.baudRate20   = read_bool(*sec, "baudrate_20");
    di.baudRate50   = read_bool(*sec, "baudrate_50");
    di.baudRate125  = read_bool(*sec, "baudrate_125");
    di.baudRate250  = read_bool(*sec, "baudrate_250");
    di.baudRate500  = read_bool(*sec, "baudrate_500");
    di.baudRate800  = read_bool(*sec, "baudrate_800");
    di.baudRate1000 = read_bool(*sec, "baudrate_1000");

    di.simpleBootUpMaster       = read_bool(*sec, "simplebootupmaster");
    di.simpleBootUpSlave        = read_bool(*sec, "simplebootupslave");
    if (auto v = read_uint(*sec, "granularity"))  di.granularity = static_cast<uint8_t>(*v);
    di.dynamicChannelsSupported = read_bool(*sec, "dynamicchannelssupported");
    di.groupMessaging           = read_bool(*sec, "groupmessaging");
    if (auto v = read_uint(*sec, "nrofrxpdo"))    di.nrOfRxPdo = static_cast<uint16_t>(*v);
    if (auto v = read_uint(*sec, "nroftxpdo"))    di.nrOfTxPdo = static_cast<uint16_t>(*v);
    di.lssSupported             = read_bool(*sec, "lss_supported");

    if (const auto* e = find_entry(*sec, "compactpdo"); e && !e->value.empty()) {
        if (auto v = parse_int_value(e->value);
            v && std::holds_alternative<uint64_t>(*v))
            di.compactPdo = static_cast<uint8_t>(std::get<uint64_t>(*v));
    }
    return di;
}

DummyUsage parse_dummy_usage(const IniDocument& doc) {
    DummyUsage du;
    const auto* sec = find_section(doc, "dummyusage");
    if (!sec) return du;
    // Entries: "Dummy0001=0", "Dummy0002=1", ... (lowercased: "dummy0001" etc.)
    for (const auto& e : sec->entries) {
        std::string_view k = e.key;
        if (!k.starts_with("dummy")) continue;
        k.remove_prefix(5);
        const auto idx = parse_hex_index(k);
        if (!idx) continue;
        const auto v = parse_int_value(e.value);
        if (v && std::holds_alternative<uint64_t>(*v))
            du.entries[*idx] = (std::get<uint64_t>(*v) != 0);
    }
    return du;
}

Comments parse_comments(const IniDocument& doc) {
    Comments c;
    const auto* sec = find_section(doc, "comments");
    if (!sec) return c;
    const auto* linesEntry = find_entry(*sec, "lines");
    if (!linesEntry) return c;
    const auto linesCount = parse_int_value(linesEntry->value);
    if (!linesCount || !std::holds_alternative<uint64_t>(*linesCount)) return c;
    const uint64_t n = std::get<uint64_t>(*linesCount);
    c.lines.resize(static_cast<size_t>(n));
    for (uint64_t i = 1; i <= n; ++i) {
        if (const auto* e = find_entry(*sec, "line" + std::to_string(i)))
            c.lines[static_cast<size_t>(i - 1)] = e->value;
    }
    return c;
}

DeviceCommissioning parse_device_commissioning(const IniDocument& doc) {
    DeviceCommissioning dc;
    // Spec §5 p.24 spells this "DeviceComissioning" (one 'm').  Many
    // real-world files and tools use the double-'m' spelling.  Accept both.
    const IniSection* sec = find_section(doc, "devicecomissioning");
    if (!sec) sec = find_section(doc, "devicecommissioning");
    if (!sec) return dc;

    if (auto v = read_uint(*sec, "nodeid"))           dc.nodeId          = static_cast<uint8_t>(*v);
    dc.nodeName = read_string(*sec, "nodename");
    if (auto v = read_uint(*sec, "baudrate"))         dc.baudrate        = static_cast<uint16_t>(*v);
    if (auto v = read_uint(*sec, "netnumber"))        dc.netNumber       = static_cast<uint32_t>(*v);
    dc.networkName = read_string(*sec, "networkname");
    dc.canopenManager = read_bool(*sec, "canopenmanager");
    if (auto v = read_uint(*sec, "lss_serialnumber")) dc.lssSerialNumber = static_cast<uint32_t>(*v);
    return dc;
}

// Build the object dictionary from the merged index list.  For each index we:
//  1. Find the [NNNN] section.
//  2. Parse the top-level ObjectEntry from it.
//  3. Scan all sections for matching [NNNNsubX] and attach sub-entries.
//  4. Read optional [xxxxName] and [xxxxValue] compact-storage side-tables.
//  5. Synthesise compact sub-objects if CompactSubObj != 0.
std::map<uint16_t, ObjectEntry>
build_object_dictionary(const IniDocument& doc,
                        const std::vector<uint16_t>& allIndices) {
    std::map<uint16_t, ObjectEntry> dict;

    for (const uint16_t idx : allIndices) {
        // Spec §4.6.3.2: section name = hex index, no "0x" prefix, no leading
        // zeros beyond the 4-digit padding.  E.g. index 0x1003 → section "1003".
        // Note: the spec says "without further leading 0", which is ambiguous —
        // "1003" has no leading zeros, and "1A00" has none either.  In practice
        // EDS files always use 4 hex digits.  Lely normalises flexibly;
        // we always emit 4 lowercase hex digits for lookup.
        char buf[5];
        std::snprintf(buf, sizeof(buf), "%04x", static_cast<unsigned>(idx));
        const std::string sectionName(buf);

        const IniSection* sec = find_section(doc, sectionName);
        if (!sec) {
            warn(0, "object 0x" + upper_hex(idx) + " is listed but has no [" + sectionName + "] section - skipped");
            continue;
        }

        ObjectEntry obj = parse_object_entry(idx, *sec);

        // Attach sub-object sections [NNNNsubX].
        for (const auto& s : doc) {
            if (s.name.size() < 7) continue;
            if (!s.name.starts_with(sectionName)) continue;
            const std::string_view rest = std::string_view(s.name).substr(4);
            if (!rest.starts_with("sub")) continue;
            const auto subIdx = parse_sub_index(rest.substr(3));
            if (!subIdx) {
                warn(s.line, "[" + s.name + "]: sub-index is not 1-2 hex digits - section ignored");
                continue;
            }
            obj.subObjects[*subIdx] = parse_sub_entry(s);
        }

        // Compact-storage side-tables.
        if (obj.compactSubObj > 0) {
            // [xxxxName] — explicit sub-object name overrides
            if (const auto* ns = find_section(doc, sectionName + "name")) {
                if (auto v = read_uint(*ns, "nrofentries")) {
                    for (uint64_t i = 1; i <= *v; ++i) {
                        if (const auto* e = find_entry(*ns, std::to_string(i)))
                            obj.compactNames[static_cast<uint8_t>(i)] = e->value;
                    }
                }
            }
            // [xxxxValue] — DCF per-sub-index parameter values
            if (const auto* vs = find_section(doc, sectionName + "value")) {
                if (auto v = read_uint(*vs, "nrofentries")) {
                    for (uint64_t i = 1; i <= *v; ++i) {
                        if (const auto* e = find_entry(*vs, std::to_string(i)))
                            obj.compactValues[static_cast<uint8_t>(i)] = e->value;
                    }
                }
            }
            synthesise_compact_sub_objects(obj);
        }

        dict[idx] = std::move(obj);
    }
    return dict;
}

// §4.6.4 — collect [xxxxObjectLinks] sections for any index in the dictionary.
std::vector<ObjectLinks>
parse_object_links(const IniDocument& doc,
                   const std::vector<uint16_t>& allIndices) {
    std::vector<ObjectLinks> links;
    for (const uint16_t idx : allIndices) {
        char buf[20];
        std::snprintf(buf, sizeof(buf), "%04xobjectlinks", static_cast<unsigned>(idx));
        const auto* sec = find_section(doc, buf);
        if (!sec) continue;
        const auto* countEntry = find_entry(*sec, "objectlinks");
        if (!countEntry) continue;
        const auto count = parse_int_value(countEntry->value);
        if (!count || !std::holds_alternative<uint64_t>(*count)) continue;
        const uint64_t n = std::get<uint64_t>(*count);
        ObjectLinks ol;
        ol.sourceIndex = idx;
        for (uint64_t i = 1; i <= n; ++i) {
            if (const auto* e = find_entry(*sec, std::to_string(i))) {
                const auto v = parse_int_value(e->value);
                if (v && std::holds_alternative<uint64_t>(*v))
                    ol.linkedIndices.push_back(
                        static_cast<uint16_t>(std::get<uint64_t>(*v)));
            }
        }
        if (!ol.linkedIndices.empty())
            links.push_back(std::move(ol));
    }
    return links;
}

EdsFile build_eds(const IniDocument& doc) {
    EdsFile f;
    f.fileInfo   = parse_file_info(doc);
    f.deviceInfo = parse_device_info(doc);
    f.dummyUsage = parse_dummy_usage(doc);
    f.comments   = parse_comments(doc);

    // DCF §5.2 — LastEDS key lives in [FileInfo]
    if (const auto* sec = find_section(doc, "fileinfo"))
        f.lastEds = read_string(*sec, "lasteds");

    f.mandatoryObjects    = parse_object_list(find_section(doc, "mandatoryobjects"));
    f.optionalObjects     = parse_object_list(find_section(doc, "optionalobjects"));
    f.manufacturerObjects = parse_object_list(find_section(doc, "manufacturerobjects"));

    std::vector<uint16_t> allIndices;
    allIndices.insert(allIndices.end(),
        f.mandatoryObjects.begin(), f.mandatoryObjects.end());
    allIndices.insert(allIndices.end(),
        f.optionalObjects.begin(), f.optionalObjects.end());
    allIndices.insert(allIndices.end(),
        f.manufacturerObjects.begin(), f.manufacturerObjects.end());

    f.objectDictionary = build_object_dictionary(doc, allIndices);
    f.objectLinks      = parse_object_links(doc, allIndices);

    // An object section that no list refers to is not part of the dictionary
    // (the lists are authoritative, §4.6.1) — but silently losing it is how a
    // forgotten SupportedObjects count turns into "the PDOs are missing".
    // One summary warning, not one per section.
    const std::set<uint16_t> listed(allIndices.begin(), allIndices.end());
    std::string orphans;
    int firstOrphanLine = 0;
    for (const auto& s : doc) {
        if (s.name.size() != 4) continue;
        const auto idx = parse_hex_index(s.name);
        if (!idx || listed.contains(*idx)) continue;
        if (orphans.empty()) firstOrphanLine = s.line;
        else orphans += ", ";
        orphans += upper_hex(*idx);
    }
    if (!orphans.empty())
        warn(firstOrphanLine, "object sections not listed in [MandatoryObjects], [OptionalObjects] or "
                              "[ManufacturerObjects] are ignored: " + orphans);
    return f;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// File I/O helper
// ---------------------------------------------------------------------------

static std::expected<std::string, ParseError>
read_file_content(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return std::unexpected(ParseError{
            "cannot open file: " + path.string()});
    std::ostringstream ss;
    ss << f.rdbuf();
    if (!f)
        return std::unexpected(ParseError{
            "error reading file: " + path.string()});
    return ss.str();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::expected<EdsFile, ParseError> parse_eds(std::string_view content) {
    std::vector<Warning> warnings;
    auto doc = parse_ini(content, warnings);
    if (!doc) return std::unexpected(doc.error());
    const WarningScope scope(warnings);
    EdsFile f = build_eds(*doc);
    f.warnings = std::move(warnings);
    return f;
}

std::expected<DcfFile, ParseError> parse_dcf(std::string_view content) {
    std::vector<Warning> warnings;
    auto doc = parse_ini(content, warnings);
    if (!doc) return std::unexpected(doc.error());
    const WarningScope scope(warnings);
    DcfFile f;
    static_cast<EdsFile&>(f) = build_eds(*doc);
    f.commissioning = parse_device_commissioning(*doc);
    f.warnings = std::move(warnings);
    return f;
}

std::expected<EdsFile, ParseError> parse_eds_file(const std::filesystem::path& path) {
    auto content = read_file_content(path);
    if (!content) return std::unexpected(content.error());
    return parse_eds(*content);
}

std::expected<DcfFile, ParseError> parse_dcf_file(const std::filesystem::path& path) {
    auto content = read_file_content(path);
    if (!content) return std::unexpected(content.error());
    return parse_dcf(*content);
}

} // namespace eds
