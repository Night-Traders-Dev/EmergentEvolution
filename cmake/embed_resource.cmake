# embed_resource.cmake — Convert a binary file to a C++ source with a byte array
# Usage: cmake -DINPUT=file.spv -DOUTPUT=file_spv.cpp -DSYMBOL=file_spv -P embed_resource.cmake

if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "Usage: cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DSYMBOL=<name> -P embed_resource.cmake")
endif()

file(READ "${INPUT}" content HEX)

# Convert hex pairs to 0xNN, format (16 bytes per line for readability)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex_array "${content}")
string(REGEX REPLACE ",([^\n]{120,140}[0-9a-f],)" ",\n    \\1" hex_array "${hex_array}")

file(WRITE "${OUTPUT}"
"// Auto-generated from ${INPUT} — do not edit\n"
"#include <cstddef>\n"
"extern const unsigned char ${SYMBOL}_data[];\n"
"extern const size_t ${SYMBOL}_size;\n"
"const unsigned char ${SYMBOL}_data[] = {\n    ${hex_array}\n};\n"
"const size_t ${SYMBOL}_size = sizeof(${SYMBOL}_data);\n"
)
