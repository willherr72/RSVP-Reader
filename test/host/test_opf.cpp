#include "doctest.h"
#include "rsvp/opf.hpp"
using namespace rsvp;

TEST_CASE("attrValue: extracts double- and single-quoted values") {
    CHECK(attrValue("item id=\"x\" href=\"a.xhtml\"", "href") == "a.xhtml");
    CHECK(attrValue("rootfile full-path='OEBPS/content.opf'", "full-path") == "OEBPS/content.opf");
}

TEST_CASE("attrValue: respects name boundaries and missing attrs") {
    CHECK(attrValue("a hrefx=\"no\" href=\"yes\"", "href") == "yes");
    CHECK(attrValue("itemref idref=\"ch1\"", "id") == "");          // 'id' must not match 'idref'
    CHECK(attrValue("item id=\"x\"", "href") == "");                // absent
    CHECK(attrValue("itemref idref = \"ch1\"", "idref") == "ch1");  // spaces around '='
}

TEST_CASE("parseContainerOpfPath: returns the OPF rootfile full-path") {
    std::string xml =
        "<?xml version=\"1.0\"?>\n"
        "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
        "  <rootfiles>\n"
        "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
        "  </rootfiles>\n"
        "</container>";
    CHECK(parseContainerOpfPath(xml) == "OEBPS/content.opf");
    CHECK(parseContainerOpfPath("<container></container>") == "");
}

TEST_CASE("parseOpf: extracts title, author, spine order, and EPUB3 cover") {
    std::string opf =
        "<?xml version=\"1.0\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">\n"
        "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
        "    <dc:title>The Great Book</dc:title>\n"
        "    <dc:creator>Jane Doe</dc:creator>\n"
        "  </metadata>\n"
        "  <manifest>\n"
        "    <item id=\"cover\" href=\"cover.jpg\" media-type=\"image/jpeg\" properties=\"cover-image\"/>\n"
        "    <item id=\"ch1\" href=\"chap1.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
        "    <item id=\"ch2\" href=\"chap2.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
        "  </manifest>\n"
        "  <spine>\n"
        "    <itemref idref=\"ch1\"/>\n"
        "    <itemref idref=\"ch2\"/>\n"
        "  </spine>\n"
        "</package>";
    OpfData d = parseOpf(opf);
    CHECK(d.title == "The Great Book");
    CHECK(d.author == "Jane Doe");
    REQUIRE(d.spineHrefs.size() == 2);
    CHECK(d.spineHrefs[0] == "chap1.xhtml");
    CHECK(d.spineHrefs[1] == "chap2.xhtml");
    CHECK(d.coverHref == "cover.jpg");
}

TEST_CASE("parseOpf: EPUB2 cover via <meta name=cover>; decodes title entities") {
    std::string opf =
        "<package>\n"
        "  <metadata>\n"
        "    <dc:title>Tom &amp; Jerry</dc:title>\n"
        "    <meta name=\"cover\" content=\"coverimg\"/>\n"
        "  </metadata>\n"
        "  <manifest>\n"
        "    <item id=\"coverimg\" href=\"images/c.png\" media-type=\"image/png\"/>\n"
        "    <item id=\"t1\" href=\"text1.html\" media-type=\"application/xhtml+xml\"/>\n"
        "  </manifest>\n"
        "  <spine><itemref idref=\"t1\"/></spine>\n"
        "</package>";
    OpfData d = parseOpf(opf);
    CHECK(d.title == "Tom & Jerry");       // entity-decoded
    REQUIRE(d.spineHrefs.size() == 1);
    CHECK(d.spineHrefs[0] == "text1.html");
    CHECK(d.coverHref == "images/c.png");  // resolved via meta name=cover
}

TEST_CASE("parseOpf: empty / structureless input yields empty data") {
    OpfData d = parseOpf("<package></package>");
    CHECK(d.title == "");
    CHECK(d.author == "");
    CHECK(d.spineHrefs.empty());
    CHECK(d.coverHref == "");
}
