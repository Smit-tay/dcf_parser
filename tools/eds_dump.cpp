#include <eds/parser.hpp>
#include <eds/model.hpp>
#include <eds/pdo.hpp>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

static void print_object(uint16_t idx, const eds::ObjectEntry& obj) {
    std::printf("  [%04X] %-40s  type=%-8s  ot=%s\n",
        idx,
        obj.parameterName.c_str(),
        to_string(obj.dataType).data(),
        to_string(obj.objectType).data());
    for (const auto& [si, sub] : obj.subObjects)
        std::printf("    sub%02X  %-38s  type=%s\n",
            si, sub.parameterName.c_str(), to_string(sub.dataType).data());
}

static void print_warnings(const eds::EdsFile& f) {
    if (f.warnings.empty())
        return;
    std::printf("\nWarnings (%zu):\n", f.warnings.size());
    for (const auto& w : f.warnings) {
        if (w.line > 0)
            std::printf("  line %d: %s\n", w.line, w.message.c_str());
        else
            std::printf("  %s\n", w.message.c_str());
    }
}

static void print_pdos(const eds::EdsFile& f, eds::PdoDirection dir, unsigned nodeId) {
    const auto views = eds::pdo_views(f, dir, static_cast<uint8_t>(nodeId));
    const char* const name = dir == eds::PdoDirection::Tx ? "TPDO" : "RPDO";
    std::printf("\n%ss of the device (node-ID %u): %zu\n", name, nodeId, views.size());
    for (const auto& v : views) {
        std::printf("  %s%u  comm 0x%04X  map 0x%04X  COB-ID 0x%03X (raw 0x%08X)  %s  tx-type %s  %u bits\n",
            name, v.number(),
            static_cast<unsigned>(v.commIndex), static_cast<unsigned>(v.mapIndex),
            static_cast<unsigned>(v.cobId), static_cast<unsigned>(v.cobIdRaw),
            v.valid ? "valid" : "INVALID",
            v.transmissionType ? std::to_string(*v.transmissionType).c_str() : "-",
            v.bitLength());
        for (const auto& e : v.entries)
            std::printf("      bit %2u  len %2u  %04X:%02X  %-40s %s %s\n",
                e.bitOffset, e.bitLength,
                static_cast<unsigned>(e.index), static_cast<unsigned>(e.subIndex),
                e.name.c_str(), to_string(e.dataType).data(), to_string(e.accessType).data());
        for (const auto& p : v.problems)
            std::printf("      PROBLEM: %s\n", p.c_str());
    }
}

static void usage() {
    std::fprintf(stderr,
        "usage: eds_dump [--pdo] [--node N] <file.eds|file.dcf>\n"
        "  --pdo     print the PDO tables (both directions) instead of the object list\n"
        "  --node N  node-ID used to resolve $NODEID formulas (default: the DCF's own, else 0)\n");
}

int main(int argc, char* argv[]) {
    bool pdoMode = false;
    long node = -1;
    const char* file = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--pdo") {
            pdoMode = true;
        } else if (arg == "--node" && i + 1 < argc) {
            node = std::strtol(argv[++i], nullptr, 0);
            if (node < 0 || node > 255) {
                std::fprintf(stderr, "--node must be 0..255\n");
                return 1;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            usage();
            return 1;
        } else {
            file = argv[i];
        }
    }
    if (!file) {
        usage();
        return 1;
    }

    const std::filesystem::path path{file};
    std::string ext = path.extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool isDcf = ext == ".dcf";

    if (isDcf) {
        auto result = eds::parse_dcf_file(path);
        if (!result) {
            std::fprintf(stderr, "parse error (line %d): %s\n",
                result.error().line, result.error().message.c_str());
            return 2;
        }
        const eds::DcfFile& f = *result;
        std::printf("File   : %s\n", f.fileInfo.fileName.c_str());
        std::printf("Device : %s  (vendor 0x%08X)\n",
            f.deviceInfo.productName.c_str(), f.deviceInfo.vendorNumber);
        std::printf("NodeID : %u\n", static_cast<unsigned>(f.commissioning.nodeId));
        std::printf("Objects: %zu\n", f.objectDictionary.size());
        if (pdoMode) {
            const unsigned nodeId = node >= 0 ? static_cast<unsigned>(node) : f.commissioning.nodeId;
            print_pdos(f, eds::PdoDirection::Tx, nodeId);
            print_pdos(f, eds::PdoDirection::Rx, nodeId);
        } else {
            std::printf("\n");
            for (const auto& [idx, obj] : f.objectDictionary)
                print_object(idx, obj);
        }
        print_warnings(f);
    } else {
        auto result = eds::parse_eds_file(path);
        if (!result) {
            std::fprintf(stderr, "parse error (line %d): %s\n",
                result.error().line, result.error().message.c_str());
            return 2;
        }
        const eds::EdsFile& f = *result;
        std::printf("File   : %s\n", f.fileInfo.fileName.c_str());
        std::printf("Device : %s  (vendor 0x%08X)\n",
            f.deviceInfo.productName.c_str(), f.deviceInfo.vendorNumber);
        std::printf("Objects: %zu\n", f.objectDictionary.size());
        if (pdoMode) {
            const unsigned nodeId = node >= 0 ? static_cast<unsigned>(node) : 0u;
            print_pdos(f, eds::PdoDirection::Tx, nodeId);
            print_pdos(f, eds::PdoDirection::Rx, nodeId);
        } else {
            std::printf("\n");
            for (const auto& [idx, obj] : f.objectDictionary)
                print_object(idx, obj);
        }
        print_warnings(f);
    }

    return 0;
}
