/* Independently reconstructed factory film stage. QWORD stores are explicit
 * double casts; x87 intermediates use long double where the oracle requires it.
 * Do not enable fast-math or replace this with a generic spline library. */
#include "internal.h"
#if LDBL_MANT_DIG != 64
#error "Factory curve reproduction requires >=64-bit long-double mantissa"
#endif
int f2400_lut(f2400_film film, unsigned scale, unsigned dark, unsigned level, uint16_t out[4096]) {
    if (!out || scale < 1 || scale > 1000 || dark > 1023)
        return fail("invalid LUT parameters");
    double x[19], y[19], h[19], slope[19], b[19], c[18], d[18];
    unsigned n = 5;
    int inverted = film == F2400_NEGATIVE || film == F2400_BW_NEGATIVE;
    int extended = film != F2400_BW_NEGATIVE;
    double factor = scale / 100.0;
    if (film == F2400_POSITIVE) {
        const double px[] = {1,  2,   3,   4,   5,   6,    7,    10,   20,  30,
                             40, 220, 330, 410, 820, 2180, 2870, 4095, 4095};
        const double py[] = {6,   12,  18,  24,   30,   36,   42,   60,   120, 180,
                             240, 730, 980, 1150, 1650, 2970, 3570, 4095, 4095};
        if (dark > 306)
            return fail("unsupported positive dark scalar");
        n = 19;
        for (unsigned i = 0; i < n; ++i) {
            x[i] = (px[i] - (i == 17 ? dark * 4 : 0)) / factor;
            y[i] = py[i];
        }
    } else if (film == F2400_NEGATIVE) {
        if (level < 40 || level > 896 || level * 4 >= 4095 - dark * 4)
            return fail("unordered negative LUT knots");
        unsigned a = level * 4, black = dark * 4;
        x[0] = 0;
        x[1] = (double)((long double)(a / 3) / (long double)factor);
        x[2] = (double)((long double)a / (long double)factor);
        x[3] = (double)(((long double)a + ((long double)4095 - black - a) / 3 * 2) /
                        (long double)factor);
        x[4] = (double)(((long double)4095 - black) / (long double)factor);
        const double py[] = {0, 540, 2048, 3800, 4095};
        memcpy(y, py, sizeof(py));
    } else if (film == F2400_BW_NEGATIVE) {
        double end = (4095 - dark * 4) / factor;
        x[0] = 0;
        x[1] = end * .1;
        x[2] = end * .5;
        x[3] = end * .8;
        x[4] = end;
        const double py[] = {0, 2000, 3080, 3600, 4095};
        memcpy(y, py, sizeof(py));
        if (end <= 0)
            return fail("unordered BW LUT knots");
    } else
        return fail("RAW has no film LUT");
    for (unsigned i = 1; i < n; ++i) {
        h[i] = film == F2400_NEGATIVE ? (double)((long double)x[i] - x[i - 1]) : x[i] - x[i - 1];
        if (h[i] == 0)
            h[i] = 1e-7;
        if (h[i] < 0)
            return fail("unordered LUT knots");
        slope[i] = film == F2400_NEGATIVE ? (double)(((long double)y[i] - y[i - 1]) / h[i])
                                          : (y[i] - y[i - 1]) / h[i];
    }
    b[0] = slope[1];
    b[n - 1] = slope[n - 1];
    for (unsigned i = 1; i + 1 < n; ++i)
        b[i] =
            extended
                ? (double)(((long double)y[i + 1] - y[i - 1]) / ((long double)x[i + 1] - x[i - 1]))
                : (y[i + 1] - y[i - 1]) / (x[i + 1] - x[i - 1]);
    for (unsigned i = 0; i + 1 < n; ++i) {
        double delta = extended ? (double)((long double)b[i] + b[i + 1] - 2.0L * slope[i + 1])
                                : b[i] + b[i + 1] - 2.0 * slope[i + 1];
        c[i] = extended ? (double)(((long double)slope[i + 1] - b[i] - delta) / h[i + 1])
                        : (slope[i + 1] - b[i] - delta) / h[i + 1];
        d[i] = extended ? (double)((long double)delta / ((long double)h[i + 1] * h[i + 1]))
                        : delta / (h[i + 1] * h[i + 1]);
    }
    for (unsigned v = 0; v < 4096; ++v) {
        double z;
        if (!v) {
            out[v] = inverted ? 4095 : 0;
            continue;
        }
        if (film == F2400_POSITIVE && v <= x[0])
            z = y[0];
        else if (v >= x[n - 1])
            z = 4095;
        else {
            unsigned i = 0;
            while (i + 1 < n && x[i + 1] <= v)
                ++i;
            double dx = v - x[i];
            z = extended ? (double)((long double)d[i] * dx * dx * dx + (long double)c[i] * dx * dx +
                                    (long double)b[i] * dx + y[i])
                         : d[i] * dx * dx * dx + c[i] * dx * dx + b[i] * dx + y[i];
        }
        if (z < 0)
            z = 0;
        if (z > 4095)
            z = 4095;
        out[v] = (uint16_t)(inverted ? 4095 - (unsigned)z : (unsigned)z);
    }
    return 0;
}
