#include "rsvp/cachepath.hpp"

namespace rsvp {

std::string cacheIndexPath(const std::string& bookPath) {
    const auto slash = bookPath.find_last_of('/');
    const std::string dir  = (slash == std::string::npos) ? std::string() : bookPath.substr(0, slash);
    const std::string name = (slash == std::string::npos) ? bookPath : bookPath.substr(slash + 1);
    return dir + "/.rsvp/" + name + ".idx";
}

} // namespace rsvp
