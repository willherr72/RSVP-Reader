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
