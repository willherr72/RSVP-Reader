#include "rsvp/battery.hpp"
namespace rsvp {

int batteryPercent(int mv) {
    static const struct { int mv; int pct; } pts[] = {
        {3300, 0}, {3500, 8}, {3700, 25}, {3800, 40},
        {3900, 58}, {4000, 76}, {4100, 90}, {4200, 100},
    };
    const int n = (int)(sizeof(pts) / sizeof(pts[0]));
    if (mv <= pts[0].mv)     return 0;
    if (mv >= pts[n-1].mv)   return 100;
    for (int i = 1; i < n; i++) {
        if (mv < pts[i].mv) {
            const int lo_mv = pts[i-1].mv, hi_mv = pts[i].mv;
            const int lo_p  = pts[i-1].pct, hi_p = pts[i].pct;
            return lo_p + (mv - lo_mv) * (hi_p - lo_p) / (hi_mv - lo_mv);
        }
    }
    return 100;
}

} // namespace rsvp
