cmake_minimum_required(VERSION 3.16)
project(rsvp_core_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../core)

add_executable(rsvp_tests
    test_main.cpp
    test_token.cpp
    test_orp.cpp
    test_pacing.cpp
    test_player.cpp
    test_tokenize.cpp
    test_byteio.cpp
    test_index.cpp
    test_utf8.cpp
    test_entity.cpp
    ${CORE_DIR}/src/orp.cpp
    ${CORE_DIR}/src/pacing.cpp
    ${CORE_DIR}/src/player.cpp
    ${CORE_DIR}/src/tokenize.cpp
    ${CORE_DIR}/src/index.cpp
    ${CORE_DIR}/src/entity.cpp
)
target_include_directories(rsvp_tests PRIVATE
    ${CORE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

enable_testing()
add_test(NAME rsvp_tests COMMAND rsvp_tests)
