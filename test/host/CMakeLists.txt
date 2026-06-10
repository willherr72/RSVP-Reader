cmake_minimum_required(VERSION 3.16)
project(rsvp_core_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)
set(MINIZ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/miniz)

add_executable(rsvp_tests
    test_main.cpp
    test_token.cpp
    test_orp.cpp
    test_gesture.cpp
    test_cachepath.cpp
    test_pacing.cpp
    test_player.cpp
    test_tokenize.cpp
    test_byteio.cpp
    test_index.cpp
    test_indexbuilder.cpp
    test_utf8.cpp
    test_entity.cpp
    test_htmltext.cpp
    test_opf.cpp
    test_miniz.cpp
    test_zipreader.cpp
    test_epub.cpp
    test_rtctime.cpp
    test_orient.cpp
    test_battery.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/gesture.cpp
    ${CORE_DIR}/src/cachepath.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
    ${CORE_DIR}/src/tokenize.cpp
    ${CORE_DIR}/src/index.cpp
    ${CORE_DIR}/src/indexbuilder.cpp
    ${CORE_DIR}/src/entity.cpp
    ${CORE_DIR}/src/htmltext.cpp
    ${CORE_DIR}/src/opf.cpp
    ${CORE_DIR}/src/zipreader.cpp
    ${CORE_DIR}/src/epub.cpp
    ${CORE_DIR}/src/rtctime.cpp
    ${CORE_DIR}/src/orient.cpp
    ${CORE_DIR}/src/battery.cpp
    ${MINIZ_DIR}/miniz.c
)
target_include_directories(rsvp_tests PRIVATE
    ${CORE_DIR}/include
    ${MINIZ_DIR}
    ${MINIZ_DIR}/..        # third_party parent, so zipreader's "miniz/miniz.h" resolves
    ${CMAKE_CURRENT_SOURCE_DIR}
)

enable_testing()
add_test(NAME rsvp_tests COMMAND rsvp_tests)
