#pragma once
#include "types.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace eds {

// CiA 306 §4.4
struct FileInfo {
    std::string fileName;
    uint8_t     fileVersion{0};
    uint8_t     fileRevision{0};
    std::string edsVersion;     // defaults to "3.0" per spec §4.4 if missing
    std::string description;
    std::string creationTime;
    std::string creationDate;
    std::string createdBy;
    std::string modificationTime;
    std::string modificationDate;
    std::string modifiedBy;
};

// CiA 306 §4.5
struct DeviceInfo {
    std::string vendorName;
    uint32_t    vendorNumber{0};
    std::string productName;
    uint32_t    productNumber{0};
    uint32_t    revisionNumber{0};
    std::string orderCode;
    // Baud-rate support flags (false = not supported, true = supported)
    bool baudRate10{false};
    bool baudRate20{false};
    bool baudRate50{false};
    bool baudRate125{false};
    bool baudRate250{false};
    bool baudRate500{false};
    bool baudRate800{false};
    bool baudRate1000{false};
    bool simpleBootUpMaster{false};
    bool simpleBootUpSlave{false};
    uint8_t  granularity{0};
    bool dynamicChannelsSupported{false};
    bool groupMessaging{false};
    uint16_t nrOfRxPdo{0};
    uint16_t nrOfTxPdo{0};
    bool lssSupported{false};
    // §4.6.3.4.1 — CompactPDO bitmask; absent when the key is not in [DeviceInfo]
    std::optional<uint8_t> compactPdo;
};

// CiA 306 §4.6.2
struct DummyUsage {
    // Key = data-type index (uint16_t), value = true when usable as dummy
    std::map<uint16_t, bool> entries;
};

// CiA 306 §4.6.3.2 — one sub-object (lives inside a RECORD or ARRAY).
// Also reused for explicit [NNNNsubX] sections.
struct SubObjectEntry {
    std::string              parameterName;
    ObjectType               objectType{ObjectType::Var};
    DataType                 dataType{DataType::Unknown};
    AccessType               accessType{AccessType::ReadOnly};
    std::optional<std::string> defaultValue;
    std::optional<std::string> lowLimit;
    std::optional<std::string> highLimit;
    bool                     pdoMapping{false};
    uint32_t                 objFlags{0};
    // DCF §5.3.1
    std::optional<std::string> parameterValue;
    // DCF §5.3.2
    std::optional<std::string> denotation;
    // DCF §5.3.1 — DOMAIN objects only
    std::optional<std::string> uploadFile;
    std::optional<std::string> downloadFile;
};

// CiA 306 §4.6.3.2 — top-level object dictionary entry.
struct ObjectEntry {
    uint16_t    index{0};
    std::string parameterName;
    ObjectType  objectType{ObjectType::Var};
    DataType    dataType{DataType::Unknown};
    AccessType  accessType{AccessType::ReadOnly};
    std::optional<std::string> defaultValue;
    std::optional<std::string> lowLimit;
    std::optional<std::string> highLimit;
    bool        pdoMapping{false};
    uint32_t    objFlags{0};
    // SubNumber: count of sub-indexes, NOT counting 0xFF (spec §4.6.3.2).
    // Absent for VAR objects (the table on spec p.16 marks it 'n' for VAR).
    std::optional<uint8_t> subNumber;
    // §4.6.3.4.2 — non-zero means ARRAY uses compact storage
    uint8_t     compactSubObj{0};
    // Sub-objects keyed by sub-index.  Populated from explicit [NNNNsubX]
    // sections for non-compact ARRAYs/RECORDs, or synthesised by
    // synthesise_compact_sub_objects() for compact ones.
    std::map<uint8_t, SubObjectEntry> subObjects;
    // §4.6.3.4.2 — explicit sub-object names from [xxxxName] override
    // the generated "ParameterNameN" default naming rule.
    std::map<uint8_t, std::string> compactNames;
    // DCF §5.3.3.2 — per-sub-index values from [xxxxValue]
    std::map<uint8_t, std::string> compactValues;
    // DCF §5.3.1 — only meaningful for VAR or DOMAIN at the top level
    std::optional<std::string> parameterValue;
    std::optional<std::string> denotation;
    std::optional<std::string> uploadFile;
    std::optional<std::string> downloadFile;
};

// CiA 306 §4.6.4 — grouped object links
struct ObjectLinks {
    uint16_t              sourceIndex{0};
    std::vector<uint16_t> linkedIndices;
};

// CiA 306 §4.6.5 — [Comments] section
struct Comments {
    std::vector<std::string> lines;
};

// CiA 306 §5, p.24 — [DeviceComissioning] (spec spells it with one 'm';
// both spellings accepted on input)
struct DeviceCommissioning {
    uint8_t     nodeId{0};
    std::string nodeName;
    uint16_t    baudrate{0};
    uint32_t    netNumber{0};
    std::string networkName;
    bool        canopenManager{false};
    uint32_t    lssSerialNumber{0};
};

// Complete EDS model
struct EdsFile {
    FileInfo    fileInfo;
    DeviceInfo  deviceInfo;
    DummyUsage  dummyUsage;
    // §4.6.1 — object index lists from each of the three sections
    std::vector<uint16_t> mandatoryObjects;
    std::vector<uint16_t> optionalObjects;
    std::vector<uint16_t> manufacturerObjects;
    // All objects, keyed by index, merged from the three lists above
    std::map<uint16_t, ObjectEntry> objectDictionary;
    // §4.6.4
    std::vector<ObjectLinks> objectLinks;
    // §4.6.5
    Comments comments;
    // DCF §5.2 — LastEDS key from [FileInfo]; empty for pure EDS files
    std::string lastEds;
};

// DCF extends EDS with device-commissioning data (§5)
struct DcfFile : EdsFile {
    DeviceCommissioning commissioning;
};

} // namespace eds
