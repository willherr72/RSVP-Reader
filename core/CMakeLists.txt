# ESP-IDF component registration for the RSVP engine core.
# (The host test build in test/host/ references these sources directly and never
#  processes this file, so this only takes effect inside an ESP-IDF firmware build.)
# Only the hardware-independent engine pieces needed on-device are listed here;
# index/entity/htmltext/opf/epub/zipreader come online once miniz is added as a
# firmware component too.
idf_component_register(
    SRCS
        "src/orp.cpp"
        "src/gesture.cpp"
        "src/cachepath.cpp"
        "src/pacing.cpp"
        "src/player.cpp"
        "src/tokenize.cpp"
    INCLUDE_DIRS "include")
