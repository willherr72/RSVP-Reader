#include "doctest.h"
#include "rsvp/cachepath.hpp"
using namespace rsvp;

TEST_CASE("cacheIndexPath puts the .idx in a sibling .rsvp dir") {
    CHECK(cacheIndexPath("/sdcard/Foo.epub") == "/sdcard/.rsvp/Foo.epub.idx");
}
TEST_CASE("cacheIndexPath keeps the extension so .epub and .txt don't collide") {
    CHECK(cacheIndexPath("/sdcard/Foo.epub") == "/sdcard/.rsvp/Foo.epub.idx");
    CHECK(cacheIndexPath("/sdcard/Foo.txt")  == "/sdcard/.rsvp/Foo.txt.idx");
}
TEST_CASE("cacheIndexPath preserves spaces and nests beside the file") {
    CHECK(cacheIndexPath("/sdcard/My Book.epub") == "/sdcard/.rsvp/My Book.epub.idx");
    CHECK(cacheIndexPath("/sdcard/sub/Bar.epub") == "/sdcard/sub/.rsvp/Bar.epub.idx");
}
