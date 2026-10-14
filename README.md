# dcf_parser

A small C++23 library that reads CANopen **EDS** and **DCF** files (CiA 306) into a plain data model, and turns the PDO objects in them into ready-to-use tables.

- Static library, no dependencies beyond the standard library.
- Never throws. Hard errors come back as `std::expected`; everything merely odd is a warning.
- Handles real-world files: UTF-8 BOM, CRLF, indented comments, `$NODEID` formulas, negative values.

## Build

Needs a C++23 compiler with `std::expected` (GCC 12+, MSVC 2022 17.3+, recent clang with a matching standard library) and CMake 3.25+.

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

No CMake? The library and tests are plain C++:

```
g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc \
  src/ini_reader.cpp src/parser.cpp src/pdo.cpp tests/test_parser.cpp \
  -DEDS_TEST_DATA_DIR='"tests/data"' -DEDS_SAMPLE_EDS='"tests/data/sample.eds"' \
  -o /tmp/eds_tests && /tmp/eds_tests
```

Options (default ON only when this is the top-level project): `EDS_PARSER_BUILD_TOOLS`, `EDS_PARSER_BUILD_TESTS`.

## Use it in your project

```cmake
add_subdirectory(extern/dcf_parser)
target_link_libraries(my_target PRIVATE eds_parser::eds_parser)
```

The target carries `cxx_std_23` and position-independent code, so it links into shared libraries (plugins) on Linux and Windows. Built through `add_subdirectory`, it inherits your toolchain and runtime flags.

## API

```cpp
#include <eds/parser.hpp>
#include <eds/pdo.hpp>

auto dcf = eds::parse_dcf_file("node20.dcf");        // std::expected<DcfFile, ParseError>
if (!dcf) { /* dcf.error().message, dcf.error().line */ return; }

for (const auto& w : dcf->warnings) { /* w.line, w.message */ }

for (const auto& pdo : eds::pdo_views(*dcf, eds::PdoDirection::Tx)) {
    // pdo.cobId, pdo.valid, pdo.transmissionType, pdo.problems
    for (const auto& e : pdo.entries) {
        // e.index, e.subIndex, e.bitOffset, e.bitLength, e.name, e.dataType, e.dummy
    }
}
```

| Header | Contents |
|---|---|
| `eds/parser.hpp` | `parse_eds`, `parse_dcf` (memory) and `parse_eds_file`, `parse_dcf_file` (disk); `parse_int_value`, `resolve_int_value`, `sign_extend`, `data_type_bits`, `effective_value` |
| `eds/model.hpp` | `EdsFile`, `DcfFile`, `ObjectEntry`, `SubObjectEntry`, ... The object dictionary is a `std::map<uint16_t, ObjectEntry>` |
| `eds/pdo.hpp` | `pdo_views()`: joins each PDO's communication and mapping objects into one table |
| `eds/types.hpp` | Enums (`DataType`, `AccessType`, `ObjectType`), `ParseError`, `Warning`, `IntValue` |

### Things worth knowing

- **Values stay as text.** `DefaultValue` and `ParameterValue` are strings. Convert with `parse_int_value()`. It returns a number or a `$NODEID` formula. Resolve a formula with `resolve_int_value(v, nodeId)`.
- **ParameterValue beats DefaultValue** in a DCF. `effective_value()` applies that rule.
- **Negative numbers** come back as two's complement in a `uint64_t`. Narrow them to the object's width with `sign_extend(raw, data_type_bits(type))`.
- **Direction in `pdo_views` is the described device's own.** A node's TPDOs are its `Tx` view. A master that listens to them models them as RPDOs.
- **Hard errors:** an unterminated `[section` header, or a UTF-16 file. Everything else lands in `warnings`, including object sections that no object list references (a common reason PDOs seem to vanish).
- **PDO problems** (missing mapping object, mapped object not in the file, length/type mismatch, payload over 64 bits) go into `PdoView::problems`. The rest of the view is still filled in.
- **COB-ID flags** are handled: bit 31 = invalid, bit 29 = 29-bit ID. `cobId` is the masked identifier and `cobIdRaw` the full value. Bit 30 (no RTR) is only visible in `cobIdRaw`.

## eds_dump

Diagnostic tool, built by default when this is the top-level project.

```
eds_dump file.eds|file.dcf            # object list
eds_dump --pdo file.dcf               # TPDO and RPDO tables
eds_dump --pdo --node 20 file.eds     # resolve $NODEID for node 20
```

Warnings are printed at the end. Exit code 2 means a hard parse error.

## Tests

`tests/test_parser.cpp` is a single executable with its own `CHECK` macro and no framework. Test data lives in `tests/data/`: `sample.eds` and two DCFs (a master, node 1, and a slave, node 20). The expected PDO tables for the DCFs are derived by hand from the files, not from the parser's output.

## Layout

```
include/eds/   public headers
src/           parser.cpp, pdo.cpp, ini_reader.{hpp,cpp} (private)
tools/         eds_dump.cpp
tests/         test_parser.cpp, data/
```

## Scope

Reads EDS and DCF content: file/device info, the object dictionary, DCF commissioning, comments, and PDO views. It does not write files, validate a device against CiA 301, or interpret object semantics beyond PDOs.
