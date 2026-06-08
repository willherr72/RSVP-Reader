#include "doctest.h"
#include "rsvp/zipreader.hpp"
#include "miniz.h"
#include <string>
#include <utility>
#include <vector>
using namespace rsvp;

// Build an in-memory ZIP from {name, content} entries (miniz writer).
static std::vector<std::uint8_t> makeZip(std::initializer_list<std::pair<std::string, std::string>> entries) {
    mz_zip_archive z;
    mz_zip_zero_struct(&z);
    mz_zip_writer_init_heap(&z, 0, 0);
    for (const auto& e : entries)
        mz_zip_writer_add_mem(&z, e.first.c_str(), e.second.data(), e.second.size(), MZ_BEST_COMPRESSION);
    void* buf = nullptr;
    size_t sz = 0;
    mz_zip_writer_finalize_heap_archive(&z, &buf, &sz);
    std::vector<std::uint8_t> out(static_cast<std::uint8_t*>(buf), static_cast<std::uint8_t*>(buf) + sz);
    mz_zip_writer_end(&z);
    mz_free(buf);
    return out;
}

TEST_CASE("readZipEntry: reads named entries from an in-memory ZIP") {
    auto zip = makeZip({{"a.txt", "alpha"}, {"dir/b.xml", "<x>beta</x>"}});
    std::string out;
    REQUIRE(readZipEntry(zip, "a.txt", out));
    CHECK(out == "alpha");
    REQUIRE(readZipEntry(zip, "dir/b.xml", out));
    CHECK(out == "<x>beta</x>");
}

TEST_CASE("readZipEntry: missing entry or invalid zip returns false and clears out") {
    auto zip = makeZip({{"a.txt", "alpha"}});
    std::string out = "untouched";
    CHECK_FALSE(readZipEntry(zip, "missing.txt", out));
    CHECK(out == "");
    CHECK_FALSE(readZipEntry(std::vector<std::uint8_t>{1, 2, 3, 4}, "a.txt", out)); // not a zip
}
