#include "doctest.h"
#include "miniz.h"
#include <string>

// Sanity check that the vendored miniz compiles into our build and its in-memory
// ZIP write/read APIs work — the foundation the EPUB ZIP reader builds on.
TEST_CASE("miniz: in-memory ZIP write then read round-trip") {
    mz_zip_archive zw;
    mz_zip_zero_struct(&zw);
    REQUIRE(mz_zip_writer_init_heap(&zw, 0, 0));
    const char* content = "hello miniz";
    REQUIRE(mz_zip_writer_add_mem(&zw, "greeting.txt", content, 11, MZ_BEST_COMPRESSION));
    void*  zbuf  = nullptr;
    size_t zsize = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&zw, &zbuf, &zsize));
    mz_zip_writer_end(&zw);

    mz_zip_archive zr;
    mz_zip_zero_struct(&zr);
    REQUIRE(mz_zip_reader_init_mem(&zr, zbuf, zsize, 0));
    const int idx = mz_zip_reader_locate_file(&zr, "greeting.txt", nullptr, 0);
    REQUIRE(idx >= 0);
    size_t outSize = 0;
    void* p = mz_zip_reader_extract_to_heap(&zr, static_cast<mz_uint>(idx), &outSize, 0);
    REQUIRE(p != nullptr);
    std::string got(static_cast<char*>(p), outSize);
    CHECK(got == "hello miniz");
    mz_free(p);
    mz_zip_reader_end(&zr);
    mz_free(zbuf);
}
