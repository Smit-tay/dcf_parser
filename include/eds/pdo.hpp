#pragma once
#include "model.hpp"
#include "types.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eds {

// ---------------------------------------------------------------------------
// PDO view — what a device's PDOs look like, read from its object dictionary
//
// A PDO is two objects: a communication parameter (COB-ID, transmission type)
// and a mapping parameter (which objects are packed into the payload, in
// which order).  This joins them into one table, resolved for a given node-ID.
//
//   Tx   the device sends:    comm 0x1800..0x19FF, mapping 0x1A00..0x1BFF
//   Rx   the device receives: comm 0x1400..0x15FF, mapping 0x1600..0x17FF
//
// Direction is the described device's own point of view.  A master that
// listens to a node's TPDOs models them as its RPDOs — that is the Tx view of
// the node's file.
//
// Nothing here throws or fails: whatever is wrong with a PDO (missing mapping
// object, mapped object not in the file, payload over 8 bytes, ...) is listed
// in PdoView::problems and the rest of the view is still filled in as far as
// it can be.
// ---------------------------------------------------------------------------

enum class PdoDirection { Tx, Rx };

// One mapped object inside the payload.
struct PdoMapEntry {
    uint16_t index{0};
    uint8_t  subIndex{0};
    unsigned bitOffset{0};  // from the start of the payload; bits are little-endian
    unsigned bitLength{0};  // as given by the mapping entry
    // Padding: the mapping points at a data type index (< 0x0020) instead of an
    // object.  Takes up bits, carries no value.
    bool     dummy{false};
    // From the mapped object (or sub-object); empty/Unknown when the file does
    // not contain it (see PdoView::problems).
    std::string name;
    DataType    dataType{DataType::Unknown};
    AccessType  accessType{AccessType::ReadOnly};
};

struct PdoView {
    PdoDirection direction{PdoDirection::Tx};
    uint16_t commIndex{0};      // 0x1800.. / 0x1400..
    uint16_t mapIndex{0};       // 0x1A00.. / 0x1600..

    // The COB-ID entry (sub 1) with $NODEID resolved:
    uint32_t cobIdRaw{0};       // including the flag bits
    uint32_t cobId{0};          // the CAN identifier: 11 bits, or 29 if extendedId
    bool     valid{true};       // bit 31 clear: the PDO exists / is enabled
    bool     extendedId{false}; // bit 29 set: 29-bit identifier
    bool     usesNodeId{false}; // the entry was a $NODEID formula, so cobId depends on the node-ID

    std::optional<uint8_t> transmissionType;    // sub 2

    std::vector<PdoMapEntry> entries;           // in payload order
    std::vector<std::string> problems;          // empty = nothing wrong found

    // Total payload length in bits (sum of the mapped lengths).
    unsigned bitLength() const {
        unsigned total = 0;
        for (const auto& e : entries) total += e.bitLength;
        return total;
    }

    // 1-based PDO number within its direction (0x1800 -> 1).
    unsigned number() const {
        return static_cast<unsigned>(commIndex - (direction == PdoDirection::Tx ? 0x1800 : 0x1400)) + 1u;
    }
};

// All PDOs of one direction that have a communication object in the file,
// ordered by index.  `nodeId` resolves $NODEID formulas.
std::vector<PdoView> pdo_views(const EdsFile& file, PdoDirection direction, uint8_t nodeId);

// Same, taking the node-ID from the DCF's [DeviceComissioning].
std::vector<PdoView> pdo_views(const DcfFile& file, PdoDirection direction);

} // namespace eds
