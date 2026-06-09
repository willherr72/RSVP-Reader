# ESP-IDF component registration for the RSVP engine core.
# (The host test build in test/host/ references these sources directly and never
#  processes this file, so this only takes effect inside an ESP-IDF firmware build.)
# The full document pipeline is registered; zipreader/epub need miniz (a sibling
# firmware component wrapping third_party/miniz).
idf_component_register(
    SRCS
        "src/orp.cpp"
        "src/gesture.cpp"
        "src/cachepath.cpp"
        "src/pacing.cpp"
        "src/player.cpp"
        "src/tokenize.cpp"
        "src/entity.cpp"
        "src/htmltext.cpp"
        "src/opf.cpp"
        "src/index.cpp"
        "src/zipreader.cpp"
        "src/epub.cpp"
    INCLUDE_DIRS "include"
    REQUIRES miniz)

# These engine sources are host-tested on MSVC; silence GCC-only -Werror style
# warnings (e.g. the compact `if (c) return; stmt;` lines in index.cpp) so the
# firmware build matches the host build.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=misleading-indentation)
