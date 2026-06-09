# Wrap the vendored miniz (repo/third_party/miniz) as an ESP-IDF component.
# Expose the third_party PARENT dir so downstream code includes "miniz/miniz.h"
# — a bare "miniz.h" would collide with ESP-IDF's inflate-only esp_rom miniz.h.
idf_component_register(
    SRCS "../../../third_party/miniz/miniz.c"
    INCLUDE_DIRS "../../../third_party")

# The ESP32-S3 mask ROM exports the tinfl_*/tdefl_* symbols from an OLDER miniz
# with a different tinfl_decompressor struct layout. The linker bound our miniz's
# call to tinfl_decompress to the ROM copy, so our code inited OUR struct but the
# ROM function read it with the wrong layout -> garbage output + memory corruption
# (a 244-byte container.xml inflate crashed). Rename our copies so they can't
# collide with the ROM; our miniz then uses its own, ABI-matched implementation.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "tinfl_decompress=rsvp_tinfl_decompress"
    "tinfl_decompress_mem_to_heap=rsvp_tinfl_decompress_mem_to_heap"
    "tinfl_decompress_mem_to_mem=rsvp_tinfl_decompress_mem_to_mem"
    "tinfl_decompress_mem_to_callback=rsvp_tinfl_decompress_mem_to_callback"
    "tdefl_compress=rsvp_tdefl_compress"
    "tdefl_compress_buffer=rsvp_tdefl_compress_buffer"
    "tdefl_compress_mem_to_heap=rsvp_tdefl_compress_mem_to_heap"
    "tdefl_compress_mem_to_mem=rsvp_tdefl_compress_mem_to_mem"
    "tdefl_compress_mem_to_output=rsvp_tdefl_compress_mem_to_output")
