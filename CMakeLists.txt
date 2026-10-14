cmake_minimum_required(VERSION 3.25)
project(eds_parser VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(eds_parser STATIC
    src/ini_reader.cpp
    src/parser.cpp
    src/pdo.cpp
)

target_include_directories(eds_parser
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(eds_parser PRIVATE /W4 /WX)
else()
    target_compile_options(eds_parser PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Optional diagnostic tool — not installed, not part of the library.
option(EDS_PARSER_BUILD_TOOLS "Build the eds_dump diagnostic tool" ON)
if(EDS_PARSER_BUILD_TOOLS)
    add_executable(eds_dump tools/eds_dump.cpp)
    target_link_libraries(eds_dump PRIVATE eds_parser)
    if(MSVC)
        target_compile_options(eds_dump PRIVATE /W4 /WX)
    else()
        target_compile_options(eds_dump PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

# Tests — a plain executable with its own CHECK macro, no framework.
# Run with:  ctest --output-on-failure
option(EDS_PARSER_BUILD_TESTS "Build the parser tests" ON)
if(EDS_PARSER_BUILD_TESTS)
    enable_testing()
    add_executable(eds_parser_tests tests/test_parser.cpp)
    target_link_libraries(eds_parser_tests PRIVATE eds_parser)
    target_compile_definitions(eds_parser_tests PRIVATE
        EDS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
        EDS_SAMPLE_EDS="${CMAKE_CURRENT_SOURCE_DIR}/sample.eds")
    if(MSVC)
        target_compile_options(eds_parser_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(eds_parser_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME eds_parser COMMAND eds_parser_tests)
endif()
