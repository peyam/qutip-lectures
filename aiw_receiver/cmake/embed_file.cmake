# Usage: cmake -DINPUT=<file> -DOUTPUT=<cpp> -P embed_file.cmake
# Emits aiw::dashboard_page() returning the file contents.  A byte array
# is used because MSVC limits the length of string literals.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR n "${hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}" "// Generated from web/index.html - do not edit.\n#include <string_view>\nnamespace aiw {\nnamespace {\nconst unsigned char kPage[] = {\n${bytes}0x00};\n}\nstd::string_view dashboard_page() {\n    return {reinterpret_cast<const char*>(kPage), ${n}};\n}\n}  // namespace aiw\n")
