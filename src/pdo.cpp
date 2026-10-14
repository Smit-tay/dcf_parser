#include <eds/pdo.hpp>
#include <eds/parser.hpp>

#include <cstdio>
#include <string>
#include <variant>

namespace eds {

namespace {

constexpr uint32_t kInvalidBit  = 0x80000000u;  // CiA 301: PDO does not exist / is not valid
constexpr uint32_t kExtendedBit = 0x20000000u;  // 29-bit identifier
constexpr unsigned kMaxMapEntries = 64;         // CiA 301: at most 64 mapped objects
constexpr unsigned kMaxPayloadBits = 64;        // classic CAN: 8 bytes

struct Number {
    uint32_t value{0};
    bool     usesNodeId{false};
};

// The effective value of a sub-object as a number, $NODEID resolved.
std::optional<Number> number_of(const SubObjectEntry* sub, uint8_t nodeId) {
    if (!sub)
        return std::nullopt;
    const auto& text = effective_value(*sub);
    if (!text)
        return std::nullopt;
    const auto parsed = parse_int_value(*text);
    if (!parsed)
        return std::nullopt;
    return Number{resolve_int_value(*parsed, nodeId), std::holds_alternative<NodeIdFormula>(*parsed)};
}

const SubObjectEntry* find_sub(const ObjectEntry& obj, uint8_t sub) {
    const auto it = obj.subObjects.find(sub);
    return it == obj.subObjects.end() ? nullptr : &it->second;
}

std::string hex(unsigned value, int digits) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%0*X", digits, value);
    return buf;
}

// "0x2003:00"
std::string address(uint16_t index, uint8_t sub) {
    return "0x" + hex(index, 4) + ":" + hex(sub, 2);
}

// Name, type and access of the object a mapping entry points at.
bool describe_target(const EdsFile& file, uint16_t index, uint8_t sub, PdoMapEntry& out) {
    const auto it = file.objectDictionary.find(index);
    if (it == file.objectDictionary.end())
        return false;
    const ObjectEntry& obj = it->second;
    if (const SubObjectEntry* s = find_sub(obj, sub)) {
        out.name       = s->parameterName;
        out.dataType   = s->dataType;
        out.accessType = s->accessType;
        return true;
    }
    if (obj.subObjects.empty() && sub == 0) {   // a plain VAR
        out.name       = obj.parameterName;
        out.dataType   = obj.dataType;
        out.accessType = obj.accessType;
        return true;
    }
    return false;
}

PdoView build_view(const EdsFile& file, PdoDirection direction, const ObjectEntry& comm, uint8_t nodeId) {
    const uint16_t commBase = direction == PdoDirection::Tx ? 0x1800 : 0x1400;
    const uint16_t mapBase  = direction == PdoDirection::Tx ? 0x1A00 : 0x1600;

    PdoView view;
    view.direction = direction;
    view.commIndex = comm.index;
    view.mapIndex  = static_cast<uint16_t>(comm.index - commBase + mapBase);

    const std::string who = "PDO 0x" + hex(view.commIndex, 4) + ": ";

    // --- communication parameter -------------------------------------------
    if (const auto cob = number_of(find_sub(comm, 1), nodeId)) {
        view.cobIdRaw   = cob->value;
        view.usesNodeId = cob->usesNodeId;
        view.valid      = (cob->value & kInvalidBit) == 0;
        view.extendedId = (cob->value & kExtendedBit) != 0;
        view.cobId      = cob->value & (view.extendedId ? 0x1FFFFFFFu : 0x7FFu);
    } else {
        view.problems.push_back(who + "no readable COB-ID value in sub-index 1");
    }

    if (const auto type = number_of(find_sub(comm, 2), nodeId)) {
        if (type->value <= 0xFF)
            view.transmissionType = static_cast<uint8_t>(type->value);
        else
            view.problems.push_back(who + "transmission type " + std::to_string(type->value) + " does not fit 8 bits");
    }

    // --- mapping parameter -------------------------------------------------
    const auto mapIt = file.objectDictionary.find(view.mapIndex);
    if (mapIt == file.objectDictionary.end()) {
        view.problems.push_back(who + "mapping object 0x" + hex(view.mapIndex, 4) + " is not in the file");
        return view;
    }
    const ObjectEntry& map = mapIt->second;

    const auto count = number_of(find_sub(map, 0), nodeId);
    if (!count) {
        view.problems.push_back(who + "mapping object 0x" + hex(view.mapIndex, 4) + " has no readable entry count (sub-index 0)");
        return view;
    }
    unsigned entries = count->value;
    if (entries > kMaxMapEntries) {
        view.problems.push_back(who + "mapping count " + std::to_string(entries) + " exceeds " +
                                std::to_string(kMaxMapEntries) + " - clamped");
        entries = kMaxMapEntries;
    }

    unsigned bitOffset = 0;
    for (unsigned i = 1; i <= entries; ++i) {
        const std::string at = who + "mapping entry " + std::to_string(i) + ": ";
        const auto value = number_of(find_sub(map, static_cast<uint8_t>(i)), nodeId);
        if (!value) {
            // Without this entry's length the offsets of all later ones are unknown.
            view.problems.push_back(at + "no readable value - later entries skipped");
            break;
        }
        if (value->value == 0) {
            view.problems.push_back(at + "is 0 although it is within the count - skipped");
            continue;
        }

        PdoMapEntry entry;
        entry.index     = static_cast<uint16_t>(value->value >> 16);
        entry.subIndex  = static_cast<uint8_t>((value->value >> 8) & 0xFF);
        entry.bitLength = value->value & 0xFF;
        entry.bitOffset = bitOffset;
        entry.dummy     = entry.index < 0x0020;

        if (entry.dummy) {
            // A dummy mapping's "index" is the data type index (CiA 301 §7.4.7).
            entry.name     = "(dummy)";
            entry.dataType = static_cast<DataType>(entry.index);
        } else if (!describe_target(file, entry.index, entry.subIndex, entry)) {
            view.problems.push_back(at + "maps " + address(entry.index, entry.subIndex) + ", which is not in the file");
        }

        const unsigned width = data_type_bits(entry.dataType);
        if (!entry.dummy && width != 0 && entry.dataType != DataType::Boolean && width != entry.bitLength)
            view.problems.push_back(at + "mapped length " + std::to_string(entry.bitLength) +
                                    " bits differs from the " + std::to_string(width) + "-bit data type of " +
                                    address(entry.index, entry.subIndex));

        bitOffset += entry.bitLength;
        view.entries.push_back(std::move(entry));
    }

    if (bitOffset > kMaxPayloadBits)
        view.problems.push_back(who + "payload is " + std::to_string(bitOffset) + " bits, more than the 64 a CAN frame holds");

    return view;
}

} // namespace

std::vector<PdoView> pdo_views(const EdsFile& file, PdoDirection direction, uint8_t nodeId) {
    const uint16_t commBase = direction == PdoDirection::Tx ? 0x1800 : 0x1400;
    const uint16_t commEnd  = static_cast<uint16_t>(commBase + 0x200);

    std::vector<PdoView> views;
    for (auto it = file.objectDictionary.lower_bound(commBase);
         it != file.objectDictionary.end() && it->first < commEnd; ++it)
        views.push_back(build_view(file, direction, it->second, nodeId));
    return views;
}

std::vector<PdoView> pdo_views(const DcfFile& file, PdoDirection direction) {
    return pdo_views(static_cast<const EdsFile&>(file), direction, file.commissioning.nodeId);
}

} // namespace eds
