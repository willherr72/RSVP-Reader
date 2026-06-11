#include "doctest.h"
#include "rsvp/uploadname.hpp"
using namespace rsvp;

TEST_CASE("sanitizeUploadName keeps valid names (basename, right extension)") {
    CHECK(sanitizeUploadName("foo.epub") == "foo.epub");
    CHECK(sanitizeUploadName("My Book.txt") == "My Book.txt");
    CHECK(sanitizeUploadName("foo.EPUB") == "foo.EPUB");      // keeps case
    CHECK(sanitizeUploadName("a.b.epub") == "a.b.epub");
}

TEST_CASE("sanitizeUploadName strips any path") {
    CHECK(sanitizeUploadName("../../etc/foo.epub") == "foo.epub");
    CHECK(sanitizeUploadName("sub/book.txt") == "book.txt");
    CHECK(sanitizeUploadName("a\\b\\c.txt") == "c.txt");
}

TEST_CASE("sanitizeUploadName rejects bad names") {
    CHECK(sanitizeUploadName("") == "");
    CHECK(sanitizeUploadName(".hidden.epub") == "");
    CHECK(sanitizeUploadName("foo.pdf") == "");
    CHECK(sanitizeUploadName("/etc/passwd") == "");
    CHECK(sanitizeUploadName("noext") == "");
}
