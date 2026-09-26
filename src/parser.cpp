#include <eds/parser.hpp>
#include "ini_reader.hpp"

#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace eds {

using namespace detail;

// ---------------------------------------------------------------------------
// §4.3 Integer value parsing
// ---------------------------------------------------------------------------

std::expected<IntValue, ParseError> parse_int_value(std::string_view s) {
    if (s.empty())
        return std::unexpected(ParseError{"empty string is not an integer value"});

    // $NODEID formula — spec §4.3 EBNF:
    //   IntEntryValue = $NODEID { "+" number }
    // "$NODEID" MUST appear at the very beginning (spec: "shall appear at the
    // beginning of the expression").  The spec writes it in uppercase and does
    // not declare the token case-insensitive.
    // Lely deviation: lely accepts any case for "$NODEID".  We require uppercase.
    if (s.starts_with("$NODEID")) {
        std::string_view rest = s.substr(7);
        // Strip optional whitespace between $NODEID and the '+'.
        while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t'))
            rest.remove_prefix(1);

        if (rest.empty())
            return NodeIdFormula{0}; // bare $NODEID, offset = 0

        if (rest[0] != '+')
            return std::unexpected(ParseError{
                std::string("invalid $NODEID expression: '").append(s).append("'")});
        rest.remove_prefix(1);
        while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t'))
            rest.remove_prefix(1);

        auto offset = parse_int_value(rest);
        if (!offset)
            return std::unexpected(offset.error());
        if (!std::holds_alternative<uint64_t>(*offset))
            return std::unexpected(ParseError{
                "nested $NODEID formula is not allowed"});
        return NodeIdFormula{static_cast<uint32_t>(std::get<uint64_t>(*offset))};
    }

    // Determine numeric base from prefix — §4.3:
    //   "Hexadecimal numbers are preceded by 0x."
    //   "Octal numbers shall start with a leading 0 (not followed by x)."
    uint64_t result = 0;
    const char* begin = s.data();
    const char* end   = s.data() + s.size();
    int base = 10;

    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base  = 16;
        begin += 2;
    } else if (s.size() >= 2 && s[0] == '0' && s[1] >= '0' && s[1] <= '7') {
        // Leading '0' followed by an octal digit → octal.
        // Consume the leading '0'; from_chars parses the rest in base 8.
        base  = 8;
        begin += 1;
    }
    // Otherwise: plain decimal.

    const auto [ptr, ec] = std::from_chars(begin, end, result, base);
    if (ec != std::errc{} || ptr != end)
        return std::unexpected(ParseError{
            std::string("not a valid integer: '").append(s).append("'")});

    return result;
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

std::string read_string(const IniSection& sec, std::string_view key) {
    if (const auto* e = find_entry(sec, key))
        return e->value;
    return {};
}

std::optional<uint64_t> read_uint(const IniSection& sec, std::string_view key) {
    const auto* e = find_entry(sec, key);
    if (!e || e->value.empty()) return std::nullopt;
    auto r = parse_int_value(e->value);
    if (!r || !std::holds_alternative<uint64_t>(*r)) return std::nullopt;
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

    if (const auto* e = find_entry(sec, "accesstype"))
        if (auto at = parse_access_type(ascii_lower(e->value)))
            obj.accessType = *at;

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

    const auto countVal = parse_int_value(countEntry->value);
    if (!countVal || !std::holds_alternative<uint64_t>(*countVal)) return {};
    const uint64_t n = std::get<uint64_t>(*countVal);

    std::vector<uint16_t> indices;
    indices.reserve(static_cast<size_t>(n));
    for (uint64_t i = 1; i <= n; ++i) {
        const auto* e = find_entry(*sec, std::to_string(i));
        if (!e) break; // stop at first gap
        const auto v = parse_int_value(e->value);
        if (v && std::holds_alternative<uint64_t>(*v))
            indices.push_back(static_cast<uint16_t>(std::get<uint64_t>(*v)));
    }
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
        if (!sec) continue; // listed but no section — skip silently

        ObjectEntry obj = parse_object_entry(idx, *sec);

        // Attach sub-object sections [NNNNsubX].
        for (const auto& s : doc) {
            if (s.name.size() < 7) continue;
            if (!s.name.starts_with(sectionName)) continue;
            const std::string_view rest = std::string_view(s.name).substr(4);
            if (!rest.starts_with("sub")) continue;
            const auto subIdx = parse_sub_index(rest.substr(3));
            if (!subIdx) continue;
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
    auto doc = parse_ini(content);
    if (!doc) return std::unexpected(doc.error());
    return build_eds(*doc);
}

std::expected<DcfFile, ParseError> parse_dcf(std::string_view content) {
    auto doc = parse_ini(content);
    if (!doc) return std::unexpected(doc.error());
    DcfFile f;
    static_cast<EdsFile&>(f) = build_eds(*doc);
    f.commissioning = parse_device_commissioning(*doc);
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
