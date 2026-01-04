cmake_minimum_required(VERSION 3.20)
project(assignment2 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 預設會用目前系統 codepage 解析原始碼，若檔案是 UTF-8 且含非 ASCII 字元會造成怪異語法錯誤。
# 這裡強制 /utf-8，避免 C4819 與字串常數被錯誤切斷（C2001）。
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# ===== DCMTK (via vcpkg) =====
find_package(DCMTK CONFIG REQUIRED)

# ===== Source files =====
add_library(mcodec_lib
    src/cli/cli_parser.cpp
    src/io/medical_loader.cpp
    src/io/medical_saver.cpp
    src/preprocess/level_shift.cpp
    src/block/tiling.cpp
    src/block/zigzag.cpp
    src/transform/dct2d.cpp
    src/quant/quantizer.cpp
    src/entropy/rle.cpp
    src/entropy/huffman.cpp
    src/entropy/bitstream.cpp
    src/codec/encoder.cpp
    src/codec/decoder.cpp
    src/format/mcodec_format.cpp
)
# ===== Include directories =====
target_include_directories(mcodec_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(mcodec_lib
    PUBLIC
        DCMTK::dcmdata
        DCMTK::ofstd
        DCMTK::oflog
)

# ===== Executables =====
add_executable(encode src/encode_main.cpp)
target_link_libraries(encode PRIVATE mcodec_lib)

add_executable(decode src/decode_main.cpp)
target_link_libraries(decode PRIVATE mcodec_lib)

add_executable(evaluate src/evaluate.cpp)
target_link_libraries(evaluate PRIVATE mcodec_lib)
