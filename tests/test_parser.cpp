// Parser tests — plain executable, no framework.  Exit code 0 = all passed.
#include <eds/model.hpp>
#include <eds/parser.hpp>
#include <eds/pdo.hpp>

#include <cstdio>
#include <string>
#include <variant>
#include <vector>

namespace {

int g_failed = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failed;                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                        \
    } while (0)

// Plain number, or nullopt if not a plain number.
bool plain(const char* text, uint64_t expect) {
    const auto r = eds::parse_int_value(text);
    if (!r) return false;
    const auto* v = std::get_if<uint64_t>(&*r);
    return v && *v == expect;
}

bool formula(const char* text, uint64_t offset) {
    const auto r = eds::parse_int_value(text);
    if (!r) return false;
    const auto* f = std::get_if<eds::NodeIdFormula>(&*r);
    return f && f->offset == offset;
}

bool fails(const char* text) { return !eds::parse_int_value(text).has_value(); }

void test_integers() {
    CHECK(plain("10", 10));
    CHECK(plain("0x0A", 10));
    CHECK(plain("0X0a", 10));
    CHECK(plain("012", 10));        // octal
    CHECK(plain("000", 0));
    CHECK(plain("-1", 0xFFFFFFFFFFFFFFFFull));
    CHECK(plain("0x200 ;c", 0x200));
    CHECK(formula("$NODEID", 0));
    CHECK(formula("$nodeid", 0));
    CHECK(formula("$NODEID+0x200", 0x200));
    CHECK(formula("1280+$NODEID", 1280));
    CHECK(formula("$NODEID+0x80+1", 0x81));
    CHECK(formula("$NODEID + 0x200", 0x200));

    CHECK(fails(""));
    CHECK(fails("abc"));
    CHECK(fails("0x"));
    CHECK(fails("1+"));
    CHECK(fails("$NODEID+$NODEID"));
    CHECK(fails("+5"));
    CHECK(fails("12abc"));

    const auto neg = eds::parse_int_value("-12");
    CHECK(neg.has_value());
    if (neg) {
        const auto* v = std::get_if<uint64_t>(&*neg);
        CHECK(v != nullptr);
        if (v) {
            CHECK((*v & 0xFF) == 0xF4);
            CHECK(eds::sign_extend(*v, 8) == -12);
        }
    }

    CHECK(eds::sign_extend(0xFFFFFFFFFFull, 40) == -1);
    CHECK(eds::sign_extend(0x80, 8) == -128);
    CHECK(eds::sign_extend(0x7F, 8) == 127);

    const auto f = eds::parse_int_value("$NODEID+0x200");
    CHECK(f && eds::resolve_int_value(*f, 20) == 0x214);
}

const char* kMini =
    "[FileInfo]\r\nFileName=mini.eds\r\n"
    "[DeviceInfo]\r\nVendorName=x\r\n"
    "[MandatoryObjects]\r\nSupportedObjects=0\r\n";

void test_text_handling() {
    // BOM + CRLF
    const std::string bom = std::string("\xEF\xBB\xBF") + kMini;
    const auto a = eds::parse_eds(bom);
    CHECK(a.has_value());
    if (a) CHECK(a->fileInfo.fileName == "mini.eds");

    // UTF-16 BOM is a hard error
    const auto b = eds::parse_eds(std::string("\xFF\xFE", 2) + kMini);
    CHECK(!b.has_value());

    // a garbage line is a warning, not an error
    const auto c = eds::parse_eds(std::string(kMini) + "this is garbage\r\n");
    CHECK(c.has_value());
    if (c) CHECK(!c->warnings.empty());

    // clean file: no warnings; indented comment accepted without a warning
    const auto d = eds::parse_eds(std::string(kMini) + "   ; indented comment\r\n");
    CHECK(d.has_value());
    if (d) CHECK(d->warnings.empty());

    // unterminated section header is a hard error
    CHECK(!eds::parse_eds("[FileInfo\r\n").has_value());
}

const char* kPdoEds =
    "[FileInfo]\nFileName=pdo.eds\n"
    "[MandatoryObjects]\nSupportedObjects=0\n"
    "[OptionalObjects]\nSupportedObjects=4\n1=0x1800\n2=0x1A00\n3=0x2000\n4=0x2001\n"
    "[1800]\nParameterName=TPDO1 comm\nObjectType=0x9\nSubNumber=3\n"
    "[1800sub0]\nParameterName=n\nObjectType=0x7\nDataType=0x0005\nAccessType=ro\nDefaultValue=2\n"
    "[1800sub1]\nParameterName=cob\nObjectType=0x7\nDataType=0x0007\nAccessType=rw\nDefaultValue=$NODEID+0x180\n"
    "[1800sub2]\nParameterName=type\nObjectType=0x7\nDataType=0x0005\nAccessType=rw\nDefaultValue=255\n"
    "[1A00]\nParameterName=TPDO1 map\nObjectType=0x9\nSubNumber=4\n"
    "[1A00sub0]\nParameterName=n\nObjectType=0x7\nDataType=0x0005\nAccessType=rw\nDefaultValue=3\n"
    "[1A00sub1]\nParameterName=m1\nObjectType=0x7\nDataType=0x0007\nAccessType=rw\nDefaultValue=0x20000001\n"
    "[1A00sub2]\nParameterName=m2\nObjectType=0x7\nDataType=0x0007\nAccessType=rw\nDefaultValue=0x00050008\n"
    "[1A00sub3]\nParameterName=m3\nObjectType=0x7\nDataType=0x0007\nAccessType=rw\nDefaultValue=0x20010010\n"
    "[2000]\nParameterName=Flag\nObjectType=0x7\nDataType=0x0001\nAccessType=rw\n"
    "[2001]\nParameterName=Value16\nObjectType=0x7\nDataType=0x0003\nAccessType=rw\n";

void test_inline_pdo() {
    const auto f = eds::parse_eds(kPdoEds);
    CHECK(f.has_value());
    if (!f) return;
    const auto v = eds::pdo_views(*f, eds::PdoDirection::Tx, 5);
    CHECK(v.size() == 1);
    if (v.size() != 1) return;
    CHECK(v[0].cobId == 0x185);
    CHECK(v[0].usesNodeId);
    CHECK(v[0].valid);
    CHECK(v[0].transmissionType && *v[0].transmissionType == 255);
    CHECK(v[0].entries.size() == 3);
    if (v[0].entries.size() != 3) return;
    CHECK(v[0].entries[0].bitOffset == 0 && v[0].entries[0].bitLength == 1);
    CHECK(v[0].entries[1].dummy && v[0].entries[1].bitOffset == 1 && v[0].entries[1].bitLength == 8);
    CHECK(v[0].entries[2].bitOffset == 9 && v[0].entries[2].bitLength == 16);
    CHECK(v[0].entries[2].index == 0x2001);
    CHECK(v[0].bitLength() == 25);
    CHECK(v[0].problems.empty());
}

struct ExpectedPdo {
    unsigned number;
    uint32_t cobId;
    unsigned entries;
    uint16_t firstIndex;
};

template <class V>
void check_pdos(const V& views, const ExpectedPdo* exp, size_t n) {
    CHECK(views.size() == n);
    if (views.size() != n) return;
    for (size_t i = 0; i < n; ++i) {
        CHECK(views[i].number() == exp[i].number);
        CHECK(views[i].cobId == exp[i].cobId);
        CHECK(views[i].valid);
        CHECK(views[i].transmissionType && *views[i].transmissionType == 1);
        CHECK(views[i].entries.size() == exp[i].entries);
        if (!views[i].entries.empty()) CHECK(views[i].entries[0].index == exp[i].firstIndex);
        CHECK(views[i].problems.empty());
        for (const auto& p : views[i].problems) std::printf("  problem: %s\n", p.c_str());
    }
}

void test_dcfs() {
    const std::string dir = EDS_TEST_DATA_DIR;

    // Slave, node 20: its TPDOs.
    const auto slave = eds::parse_dcf_file(dir + "/canopen-slave.dcf");
    CHECK(slave.has_value());
    if (slave) {
        CHECK(slave->commissioning.nodeId == 20);
        const auto tx = eds::pdo_views(*slave, eds::PdoDirection::Tx);
        const ExpectedPdo exp[] = {
            {1, 0x194, 1, 0x2003}, {2, 0x294, 2, 0x2011}, {4, 0x494, 1, 0x2013}, {5, 0x690, 1, 0x2014}};
        // Only check what is hand-derived; the file may hold further (invalid) PDOs.
        size_t found = 0;
        for (const auto& e : exp)
            for (const auto& v : tx)
                if (v.commIndex == 0x1800 + e.number - 1) {
                    ++found;
                    CHECK(v.cobId == e.cobId);
                    CHECK(v.entries.size() == e.entries);
                    if (!v.entries.empty()) CHECK(v.entries[0].index == e.firstIndex);
                    CHECK(v.problems.empty());
                }
        CHECK(found == 4);
        for (const auto& v : tx)
            if (v.commIndex == 0x1801 && v.entries.size() == 2) {
                CHECK(v.entries[0].bitOffset == 0 && v.entries[0].bitLength == 32);
                CHECK(v.entries[1].index == 0x2015 && v.entries[1].bitOffset == 32 && v.entries[1].bitLength == 16);
            }
    }

    // Master, node 1: its RPDOs.
    const auto master = eds::parse_dcf_file(dir + "/canopen-master.dcf");
    CHECK(master.has_value());
    if (master) {
        CHECK(master->commissioning.nodeId == 1);
        const auto rx = eds::pdo_views(*master, eds::PdoDirection::Rx);
        const ExpectedPdo exp[] = {
            {1, 0x194, 1, 0x2010}, {2, 0x294, 2, 0x2011}, {3, 0x394, 1, 0x2012},
            {4, 0x494, 1, 0x2013}, {5, 0x690, 1, 0x2014}};
        check_pdos(rx, exp, 5);
        if (rx.size() == 5 && rx[1].entries.size() == 2)
            CHECK(rx[1].entries[1].index == 0x2015 && rx[1].entries[1].bitLength == 16);
    }
}


bool bytes(eds::DataType type, const char* text, std::vector<uint8_t> expect, uint8_t nodeId = 0) {
    const auto r = eds::encode_value(type, text, nodeId);
    return r && *r == expect;
}

bool encode_fails(eds::DataType type, const char* text) { return !eds::encode_value(type, text).has_value(); }

void test_encode_value() {
    using eds::DataType;
    CHECK(bytes(DataType::Boolean, "1", {1}));
    CHECK(bytes(DataType::Boolean, "0", {0}));
    CHECK(bytes(DataType::Unsigned8, "0xFF", {0xFF}));
    CHECK(bytes(DataType::Unsigned16, "0x1234", {0x34, 0x12}));
    CHECK(bytes(DataType::Unsigned32, "0xFFFFFFFF", {0xFF, 0xFF, 0xFF, 0xFF}));
    CHECK(bytes(DataType::Unsigned64, "0xA5A5A5A5A5A5A5A5", {0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5}));
    CHECK(bytes(DataType::Integer8, "-1", {0xFF}));
    CHECK(bytes(DataType::Integer8, "-128", {0x80}));
    CHECK(bytes(DataType::Integer16, "-32768", {0x00, 0x80}));
    CHECK(bytes(DataType::Integer40, "-1", {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
    CHECK(bytes(DataType::Integer24, "0x010203", {0x03, 0x02, 0x01}));
    CHECK(bytes(DataType::Integer64, "-9223372036854775808", {0, 0, 0, 0, 0, 0, 0, 0x80}));
    // $NODEID formulas
    CHECK(bytes(DataType::Unsigned32, "$NODEID+0x600", {0x21, 0x06, 0, 0}, 33));
    CHECK(bytes(DataType::Unsigned8, "$NODEID", {33}, 33));
    // floating point
    CHECK(bytes(DataType::Real32, "1.0", {0x00, 0x00, 0x80, 0x3F}));
    CHECK(bytes(DataType::Real32, "-2.5", {0x00, 0x00, 0x20, 0xC0}));
    CHECK(bytes(DataType::Real64, "0.5", {0, 0, 0, 0, 0, 0, 0xE0, 0x3F}));
    // strings
    CHECK(bytes(DataType::VisibleString, "abc", {'a', 'b', 'c'}));
    CHECK(bytes(DataType::OctetString, "DEADBEEF", {0xDE, 0xAD, 0xBE, 0xEF}));
    CHECK(bytes(DataType::OctetString, "de ad be ef", {0xDE, 0xAD, 0xBE, 0xEF}));
    CHECK(bytes(DataType::Domain, "", {}));
    // things that do not fit or cannot be encoded
    CHECK(encode_fails(DataType::Unsigned8, "256"));
    CHECK(encode_fails(DataType::Integer8, "-129"));
    CHECK(encode_fails(DataType::Unsigned16, "-1"));
    CHECK(encode_fails(DataType::Unsigned8, "abc"));
    CHECK(encode_fails(DataType::OctetString, "ABC"));
    CHECK(encode_fails(DataType::OctetString, "ZZ"));
    CHECK(encode_fails(DataType::Real32, "1.0x"));
    CHECK(encode_fails(DataType::UnicodeString, "x"));
    CHECK(encode_fails(DataType::TimeOfDay, "0"));
}

void test_sample_eds() {
    const auto f = eds::parse_eds_file(EDS_SAMPLE_EDS);
    CHECK(f.has_value());
    if (!f) return;
    bool orphan = false;
    for (const auto& w : f->warnings)
        if (w.message.find("1400") != std::string::npos && w.message.find("6") != std::string::npos)
            orphan = true;
    CHECK(orphan);
}

} // namespace

int main() {
    test_integers();
    test_text_handling();
    test_inline_pdo();
    test_dcfs();
    test_sample_eds();
    test_encode_value();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
