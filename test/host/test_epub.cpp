#include "doctest.h"
#include "rsvp/epub.hpp"
using namespace rsvp;

TEST_CASE("resolveHref: resolves spine hrefs against the OPF directory") {
    CHECK(resolveHref("OEBPS/content.opf", "chap1.xhtml") == "OEBPS/chap1.xhtml");
    CHECK(resolveHref("content.opf", "a.xhtml") == "a.xhtml");
    CHECK(resolveHref("OEBPS/text/content.opf", "../img/c.png") == "OEBPS/img/c.png");
    CHECK(resolveHref("OEBPS/content.opf", "sub/d.xhtml") == "OEBPS/sub/d.xhtml");
    CHECK(resolveHref("OEBPS/content.opf", "./x.xhtml") == "OEBPS/x.xhtml");
}
