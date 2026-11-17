/* Factory 10003262 / 10002a0c / 10001c82; no device or image UI dependency. */
#include "internal.h"
static unsigned be16(const uint8_t *p) {
    return (unsigned)p[0] * 256 + p[1];
}
static unsigned corrected(const uint8_t *raw, size_t index, const uint16_t *dark,
                          const uint8_t *shade, unsigned c, unsigned x, unsigned mean) {
    unsigned sample = be16(raw + index * 2), offset = dark[c * 2336 + x * 8];
    unsigned v = sample > offset ? sample - offset : 0;
    unsigned coeff = (unsigned)(255.0 / shade[c * 2336 + x * 8] * 1000.0);
    v = v * coeff / 1000;
    return v > 1023 - mean ? 1023 - mean : v;
}
int normalize_bw(unsigned requested, unsigned *exposure, unsigned *percentage) {
    if (requested < 8 || requested > 32767)
        return fail("BW exposure outside supported range");
    *exposure = requested > 136 ? 136 : requested;
    double factor = requested > 136 ? requested / 136.0 : 1.0;
    *percentage = (unsigned)((long double)factor * 100);
    if (*percentage > 1000)
        return fail("BW compensation exceeds 1000 percent");
    return 0;
}
int f2400_meter(const uint8_t *raw, size_t size, const uint16_t dark[7008],
                const uint8_t shade[7008], const unsigned base[3], f2400_film film, f2400_ae *out) {
    int gray = film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN;
    if (!raw || !dark || !shade || !base || !out ||
        (film < F2400_POSITIVE || film > F2400_RAW_GREEN) ||
        size != (gray ? 292u * 438 * 2 : 292u * 440 * 6))
        return fail("invalid meter geometry");
    memset(out, 0, sizeof(*out));
    for (unsigned i = 0; i < 7008; ++i)
        if (dark[i] > 1023 || !shade[i] || (film == F2400_NEGATIVE && shade[i] < 128))
            return fail("invalid meter calibration");
    for (size_t i = 0; i < size; i += 2)
        if (be16(raw + i) > 1023)
            return fail("meter ADC exceeds 10 bits");
    for (unsigned c = 0; c < 3; ++c) {
        if (base[c] < 1 || base[c] > (film == F2400_NEGATIVE ? 136u : 128u))
            return fail("invalid meter base exposure");
        for (unsigned x = 0; x < 2336; ++x)
            out->means[c] += dark[c * 2336 + x];
        out->means[c] /= 2336;
    }
    unsigned sums[3][1344] = {{0}}, class_sums[3][1344] = {{0}};
    unsigned channels = gray ? 1 : 3;
    for (unsigned ch = 0; ch < channels; ++ch) {
        unsigned c = gray ? 1 : ch;
        out->minima[c] = gray ? 0x3800 : UINT_MAX;
        for (unsigned by = 0; by < 42; ++by)
            for (unsigned bx = 0; bx < 32; ++bx) {
                unsigned total = 0, cls = 0;
                for (unsigned dy = 0; dy < 10; ++dy)
                    for (unsigned dx = 0; dx < 9; ++dx) {
                        unsigned x = 2 + bx * 9 + dx;
                        size_t row = gray ? 9 + by * 10 + dy : 27 + 4 * c + 3 * (by * 10 + dy);
                        unsigned v =
                            corrected(raw, row * 292 + x, dark, shade, c, x, out->means[c]);
                        total += v;
                        cls += v / 4;
                        if (v > out->peaks[c])
                            out->peaks[c] = v;
                    }
                unsigned index = by * 32 + bx;
                sums[c][index] = total;
                class_sums[c][index] = cls;
                if (total > out->maxima[c]) {
                    out->maxima[c] = total;
                    out->block_x[c] = bx;
                    out->block_y[c] = by;
                }
                if (total < out->minima[c])
                    out->minima[c] = total;
            }
    }
    if (film == F2400_POSITIVE || film == F2400_RAW_RGB) {
        unsigned c = 0;
        for (unsigned i = 1; i < 3; ++i)
            if (out->maxima[i] > out->maxima[c])
                c = i;
        out->selected_channel = c;
        out->selected_average = out->maxima[c] / 90;
        if (!out->selected_average) {
            if (film != F2400_RAW_RGB)
                return fail("positive meter has no signal");
            out->no_signal = 1;
            out->limited = 1;
            for (unsigned i = 0; i < 3; ++i)
                out->exposures[i] = 128;
            return 0;
        }
        double factor = (1023 - out->means[c]) / (double)out->selected_average;
        for (unsigned i = 0; i < 3; ++i) {
            out->factors[i] = factor;
            out->exposures[i] = (unsigned)((long double)base[i] * factor);
            if (out->exposures[i] < 8 || out->exposures[i] > 128) {
                out->limited = 1;
                if (film == F2400_RAW_RGB)
                    out->exposures[i] = out->exposures[i] < 8 ? 8 : 128;
            }
        }
        return 0;
    }
    for (unsigned ch = 0; ch < channels; ++ch) {
        unsigned c = gray ? 1 : ch;
        if (!gray)
            out->maxima[c] = (out->maxima[c] + out->peaks[c] * 90) / 2;
        unsigned range = out->maxima[c] - out->minima[c];
        double low = (double)((long double)range * (long double).01 + out->minima[c]);
        double high = (double)((long double)range * (long double).5 + out->minima[c]);
        uint64_t sum = 0;
        unsigned count = 0;
        for (unsigned i = 0; i < 1344; ++i)
            if (sums[c][i] > low && sums[c][i] < high) {
                sum += sums[c][i];
                ++count;
            }
        unsigned peak = out->maxima[c] / 90;
        if (!peak)
            return fail("negative meter has no signal");
        double factor = (1023 - out->means[c]) / (double)peak;
        out->factors[c] = factor;
        unsigned average = (unsigned)(sum / (count ? count : 1) / 90);
        unsigned level = gray ? (unsigned)((long double)factor * average)
                              : (unsigned)((long double)sum * factor / (count ? count : 1) / 90);
        out->levels[c] = (unsigned)(int16_t)level;
        out->exposures[c] = (unsigned)((long double)factor * base[c]);
        if (gray) {
            out->band_count = count;
            out->band_average = average;
            out->selected_average = peak;
        }
    }
    if (gray) {
        for (unsigned c = 0; c < 3; ++c) {
            out->exposures[c] = out->exposures[1];
            out->levels[c] = out->levels[1];
        }
        return 0;
    }
    for (unsigned i = 0; i < 1344; ++i) {
        unsigned v[3], order[3] = {0, 1, 2};
        for (unsigned c = 0; c < 3; ++c)
            v[c] = (unsigned)((long double)class_sums[c][i] * out->factors[c]);
        for (unsigned j = 1; j < 3; ++j)
            for (unsigned k = j; k && v[order[k]] < v[order[k - 1]]; --k) {
                unsigned t = order[k];
                order[k] = order[k - 1];
                order[k - 1] = t;
            }
        unsigned lo = v[order[0]], mid = v[order[1]], hi = v[order[2]];
        if (hi < 2700 || (uint64_t)lo * 4 >= (uint64_t)hi * 3)
            ++out->counts[0];
        else if (hi - mid > mid - lo + 1)
            ++out->counts[4 + order[2]];
        else
            ++out->counts[1 + order[0]];
    }
    unsigned first = 1;
    for (unsigned i = 2; i < 7; ++i)
        if (out->counts[i] > out->counts[first])
            first = i;
    unsigned second = first == 1 ? 2 : 1;
    for (unsigned i = 1; i < 7; ++i)
        if (i != first && out->counts[i] > out->counts[second])
            second = i;
    unsigned usable = 1344 - out->counts[0];
    if (out->counts[0] <= 672 && (out->counts[first] > usable / 2 ||
                                  out->counts[first] + out->counts[second] > usable * 2 / 3))
        out->classifier = first;
    if (out->classifier) {
        unsigned a = out->levels[0], b = out->levels[1], c = out->levels[2], selected = 0;
        switch (out->classifier) {
        case 1:
            selected = a;
            break;
        case 2:
            selected = b;
            break;
        case 3:
            selected = c;
            break;
        case 4:
            selected = (b + c) / 2;
            break;
        case 5:
            selected = (a + c) / 2;
            break;
        case 6:
            selected = (a + b) / 2;
            break;
        }
        double mult[3] = {1.2, 1, 1};
        if (first == 3 || first == 4) {
            mult[0] = 1.1;
            mult[2] = .95;
        }
        for (unsigned i = 0; i < 3; ++i)
            out->levels[i] = (unsigned)((long double)selected * mult[i]);
    }
    for (unsigned i = 0; i < 3; ++i)
        if (out->levels[i] < 40 || out->levels[i] > 896) {
            for (unsigned c = 0; c < 3; ++c)
                out->levels[c] = 256;
            break;
        }
    return 0;
}

int f2400_normalize_color_exposure(const unsigned requested[3], f2400_normalization *out) {
    if (!requested || !out)
        return fail("missing normalization input");
    unsigned common = requested[0];
    for (unsigned c = 0; c < 3; ++c) {
        if (requested[c] < 8 || requested[c] > 32767)
            return fail("unsupported requested exposure");
        if (requested[c] < common)
            common = requested[c];
    }
    double cap = common > 136 ? common / 136.0 : 1.0;
    out->exposure = common > 136 ? 136 : common;
    for (unsigned c = 0; c < 3; ++c) {
        double ratio = requested[c] / (double)common;
        long double gain = (long double)log10(ratio) * (long double)42.8 + (long double)14.44;
        if (gain > 31) {
            double divisor = pow(10, 0.3869158878504673);
            out->gains[c] = 31;
            out->percentages[c] = (unsigned)((long double)ratio / divisor * cap * 100);
        } else {
            out->gains[c] = (unsigned)gain;
            out->percentages[c] = (unsigned)((long double)cap * 100);
        }
        if (!out->percentages[c] || out->percentages[c] > 1000)
            return fail("unsupported digital compensation");
    }
    return 0;
}
