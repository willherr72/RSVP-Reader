#include "rsvp/zipreader.hpp"
// Include via the "miniz/" subpath so this resolves to our vendored
// third_party/miniz (which has the ZIP archive API) rather than ESP-IDF's
// inflate-only esp_rom/include/miniz.h, which also matches a bare "miniz.h".
#include "miniz/miniz.h"

namespace rsvp {

bool readZipEntry(const std::vector<std::uint8_t>& zip, const std::string& name, std::string& out) {
    out.clear();
    if (zip.empty()) return false;
    mz_zip_archive za;
    mz_zip_zero_struct(&za);
    if (!mz_zip_reader_init_mem(&za, zip.data(), zip.size(), 0)) return false;
    bool ok = false;
    const int idx = mz_zip_reader_locate_file(&za, name.c_str(), nullptr, 0);
    if (idx >= 0) {
        std::size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&za, static_cast<mz_uint>(idx), &sz, 0);
        if (p) {
            out.assign(static_cast<const char*>(p), sz);
            mz_free(p);
            ok = true;
        }
    }
    mz_zip_reader_end(&za);
    return ok;
}

} // namespace rsvp
