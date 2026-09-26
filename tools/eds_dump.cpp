#include <eds/parser.hpp>
#include <eds/model.hpp>
#include <cstdio>
#include <cstdlib>

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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: eds_dump <file.eds|file.dcf>\n");
        return 1;
    }

    std::filesystem::path path{argv[1]};
    const bool isDcf = path.extension() == ".dcf";

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
        std::printf("NodeID : %u\n", f.commissioning.nodeId);
        std::printf("Objects: %zu\n\n", f.objectDictionary.size());
        for (const auto& [idx, obj] : f.objectDictionary)
            print_object(idx, obj);
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
        std::printf("Objects: %zu\n\n", f.objectDictionary.size());
        for (const auto& [idx, obj] : f.objectDictionary)
            print_object(idx, obj);
    }

    return 0;
}
