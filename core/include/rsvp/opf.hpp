#pragma once
#include <string>
#include <vector>

namespace rsvp {

// Extract attribute `name`'s value from a tag's inner text (name="v" or name='v').
// Returns "" if absent. Entities are NOT decoded. Matches whole attribute names only.
std::string attrValue(const std::string& tagInner, const std::string& name);

// From an EPUB META-INF/container.xml, return the OPF package's full-path ("" if none).
std::string parseContainerOpfPath(const std::string& containerXml);

// Parsed OPF package document.
struct OpfData {
    std::string              title;
    std::string              author;
    std::vector<std::string> spineHrefs;  // reading order (manifest hrefs, OPF-relative)
    std::string              coverHref;    // "" if none
};

OpfData parseOpf(const std::string& opfXml);

} // namespace rsvp
