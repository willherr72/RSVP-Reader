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

TEST_CASE("sanitizeUploadName caps an over-long stem (keeps the extension)") {
    std::string r = sanitizeUploadName(std::string(300, 'a') + ".epub");
    CHECK(r.size() == 125);                       // 120 stem + ".epub"
    CHECK(r.substr(r.size() - 5) == ".epub");
}

TEST_CASE("sanitizeUploadName forCreate=false keeps names verbatim (delete path)") {
    std::string longname = std::string(300, 'a') + ".epub";
    CHECK(sanitizeUploadName(longname, false) == longname);
    CHECK(sanitizeUploadName("../" + longname, false) == longname);   // still strips paths
    CHECK(sanitizeUploadName("foo.pdf", false) == "");                // still checks extension
    CHECK(sanitizeUploadName("Can\xE2\x80\x99t.epub", false) == "Can\xE2\x80\x99t.epub");
}

TEST_CASE("sanitizeUploadName forCreate makes names FatFs-safe ASCII") {
    CHECK(sanitizeUploadName("Can\xE2\x80\x99t Stop.epub") == "Can't Stop.epub");   // smart quote
    CHECK(sanitizeUploadName("b\xC3\xB6ok.epub") == "b_ok.epub");                   // ö -> one '_'
    CHECK(sanitizeUploadName("Book: Subtitle?.epub") == "Book_ Subtitle_.epub");    // FAT-invalid
    CHECK(sanitizeUploadName("plain name.txt") == "plain name.txt");                // untouched
}

TEST_CASE("sanitizeUploadName rejects bad names") {
    CHECK(sanitizeUploadName("") == "");
    CHECK(sanitizeUploadName(".hidden.epub") == "");
    CHECK(sanitizeUploadName("foo.pdf") == "");
    CHECK(sanitizeUploadName("/etc/passwd") == "");
    CHECK(sanitizeUploadName("noext") == "");
}
