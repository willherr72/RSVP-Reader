# Wrap the vendored miniz (repo/third_party/miniz) as an ESP-IDF component.
# Expose the third_party PARENT dir so downstream code includes "miniz/miniz.h"
# — a bare "miniz.h" would collide with ESP-IDF's inflate-only esp_rom miniz.h.
idf_component_register(
    SRCS "../../../third_party/miniz/miniz.c"
    INCLUDE_DIRS "../../../third_party")
