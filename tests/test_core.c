/* Independent original/legacy goldens. No hardware, DLL or Python is used. */
#include "internal.h"
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #expr, f2400_error());    \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static const char *fixtures;
static json_object *fixture_json(const char *name) {
    char p[PATH_MAX];
    CHECK(path_join(p, sizeof(p), fixtures, name) == 0);
    json_object *j = json_file(p);
    CHECK(j);
    return j;
}
static uint8_t *fixture_data(const char *name, size_t expected) {
    char p[PATH_MAX];
    uint8_t *bytes = NULL;
    size_t n = 0;
    CHECK(path_join(p, sizeof(p), fixtures, name) == 0);
    CHECK(read_file(p, &bytes, &n) == 0 && n == expected);
    return bytes;
}
static unsigned integer(json_object *j, const char *key) {
    json_object *v = NULL;
    CHECK(json_object_object_get_ex(j, key, &v));
    return (unsigned)json_object_get_int64(v);
}
static const char *string(json_object *j, const char *key) {
    json_object *v = NULL;
    CHECK(json_object_object_get_ex(j, key, &v));
    return json_object_get_string(v);
}
static void test_curves(void) {
    json_object *cases = fixture_json("curves.json");
    unsigned n = json_object_array_length(cases);
    for (unsigned i = 0; i < n; ++i) {
        json_object *j = json_object_array_get_idx(cases, i);
        uint16_t lut[4096];
        uint8_t bytes[8192];
        char sha[65];
        CHECK(f2400_lut(integer(j, "film"), integer(j, "scale"), integer(j, "dark"),
                        integer(j, "level"), lut) == 0);
        for (unsigned x = 0; x < 4096; ++x) {
            bytes[2 * x] = lut[x];
            bytes[2 * x + 1] = lut[x] >> 8;
        }
        CHECK(hash_data(bytes, sizeof(bytes), sha) == 0 && !strcmp(sha, string(j, "sha256")));
    }
    uint16_t values[4096];
    CHECK(f2400_lut(F2400_NEGATIVE, 100, 1023, 896, values) < 0);
    CHECK(f2400_lut(F2400_POSITIVE, 0, 0, 0, values) < 0);
    json_object_put(cases);
    printf("PASS %u original LUT cases (%u entries)\n", n, n * 4096);
}
static void test_exposure(void) {
    uint16_t darks[3][7008];
    uint8_t *shades[3];
    const char *labels[] = {"ae", "cn", "pos"};
    for (unsigned c = 0; c < 3; ++c) {
        char name[64];
        snprintf(name, sizeof(name), "%s-dark.bin", labels[c]);
        uint8_t *p = fixture_data(name, 14016);
        for (unsigned i = 0; i < 7008; ++i)
            darks[c][i] = (uint16_t)((unsigned)p[2 * i] + (unsigned)p[2 * i + 1] * 256);
        free(p);
        snprintf(name, sizeof(name), "%s-shade.bin", labels[c]);
        shades[c] = fixture_data(name, 7008);
    }
    json_object *cases = fixture_json("meters.json");
    unsigned n = json_object_array_length(cases);
    for (unsigned i = 0; i < n; ++i) {
        json_object *j = json_object_array_get_idx(cases, i);
        unsigned film = integer(j, "film"), gray = film == 2, base[3], expected[3];
        const char *name = string(j, "kind");
        unsigned rows = gray ? 438 : 440, channels = gray ? 1 : 3;
        size_t size = (size_t)rows * 292 * channels * 2;
        uint8_t *raw = malloc(size);
        CHECK(raw);
        for (unsigned y = 0; y < rows; ++y)
            for (unsigned c = 0; c < channels; ++c)
                for (unsigned x = 0; x < 292; ++x) {
                    unsigned v = 0, index = 0;
                    char kind[8];
                    if (!strncmp(name, "synthetic-flat-", 15))
                        v = (unsigned)atoi(name + 15);
                    else if (!strncmp(name, "flat-", 5))
                        v = (unsigned)atoi(name + 5);
                    else if (sscanf(name, "synthetic-%u", &index) == 1)
                        v = 150 + (x * (c + 1) * 7 + y * (index + 1) * (c + 3)) % 700;
                    else if (sscanf(name, "classifier-%7[^-]-%u", kind, &index) == 2) {
                        v = ((!strcmp(kind, "min")) == (c == index)) ? 250 : 650;
                        if (y >= 80 && y < 100 && x >= 80 && x < 100)
                            v = 900;
                    } else if (!gray) {
                        size_t k = (size_t)y * channels * 292 + c * 292 + x;
                        v = 150 + (k * 17 + k / 292 * 11) % 700;
                    } else
                        v = 150 + (x * 17 + y * 11) % 700;
                    size_t pos = ((size_t)y * channels * 292 + c * 292 + x) * 2;
                    raw[pos] = v >> 8;
                    raw[pos + 1] = v;
                }
        CHECK(json_uints(j, "base", base, 3, 1, 136) == 0);
        f2400_ae ae;
        CHECK(f2400_meter(raw, size,
                          darks[film == 1   ? 1
                                : film == 0 ? 2
                                            : 0],
                          shades[film == 1   ? 1
                                 : film == 0 ? 2
                                             : 0],
                          base, film, &ae) == 0);
        CHECK(json_uints(j, "exposures", expected, 3, 0, 65535) == 0);
        if (memcmp(expected, ae.exposures, sizeof(expected)))
            fprintf(stderr, "case %s film%u expected %u/%u/%u got %u/%u/%u\n", name, film,
                    expected[0], expected[1], expected[2], ae.exposures[0], ae.exposures[1],
                    ae.exposures[2]);
        CHECK(!memcmp(expected, ae.exposures, sizeof(expected)));
        if (film != 0) {
            CHECK(json_uints(j, "levels", expected, 3, 0, 65535) == 0 &&
                  !memcmp(expected, ae.levels, sizeof(expected)));
        }
        if (film == 1) {
            CHECK(ae.classifier == integer(j, "classifier"));
            unsigned counts[7];
            CHECK(json_uints(j, "counts", counts, 7, 0, 1344) == 0 &&
                  !memcmp(counts, ae.counts, sizeof(counts)));
        }
        free(raw);
    }
    unsigned base[3] = {36, 20, 25};
    uint8_t *blank = calloc(292 * 440 * 6, 1);
    CHECK(blank);
    f2400_ae ae;
    CHECK(f2400_meter(blank, 292 * 440 * 6, darks[0], shades[0], base, F2400_POSITIVE, &ae) < 0);
    CHECK(f2400_meter(blank, 292 * 440 * 6, darks[0], shades[0], base, F2400_RAW_RGB, &ae) == 0 &&
          ae.no_signal && ae.exposures[0] == 128 && ae.limited);
    CHECK(f2400_meter(blank, 2, darks[0], shades[0], base, F2400_RAW_RGB, &ae) < 0);
    unsigned e, p;
    CHECK(normalize_bw(205, &e, &p) == 0 && e == 136 && p == 150);
    CHECK(normalize_bw(32767, &e, &p) < 0);
    free(blank);
    free(shades[0]);
    free(shades[1]);
    free(shades[2]);
    json_object_put(cases);
    printf("PASS %u original AE cases, all classifier branches, RAW/no-signal/bounds\n", n);
}
static void test_normalization(void) {
    json_object *cases = fixture_json("normalization.json");
    for (unsigned i = 0; i < json_object_array_length(cases); ++i) {
        json_object *j = json_object_array_get_idx(cases, i), *o = NULL;
        unsigned values[3], expected[3];
        f2400_normalization norm;
        CHECK(json_uints(j, "requested", values, 3, 8, 32767) == 0 &&
              json_object_object_get_ex(j, "original", &o));
        CHECK(f2400_normalize_color_exposure(values, &norm) == 0 &&
              norm.exposure == integer(o, "exposure"));
        CHECK(json_uints(o, "gains", expected, 3, 0, 31) == 0 &&
              !memcmp(expected, norm.gains, sizeof(expected)));
        CHECK(json_uints(o, "percentages", expected, 3, 1, 1000) == 0 &&
              !memcmp(expected, norm.percentages, sizeof(expected)));
    }
    json_object_put(cases);
    puts("PASS 7 original preview exposure normalization cases");
}
static void test_calibration(void) {
    uint32_t sums[3][7008];
    const char *fields[] = {"dark", "white", "correction"};
    for (unsigned f = 0; f < 3; ++f) {
        char name[64];
        snprintf(name, sizeof(name), "calibration-%s.bin", fields[f]);
        uint8_t *p = fixture_data(name, 28032);
        for (unsigned i = 0; i < 7008; ++i)
            sums[f][i] = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 |
                         (uint32_t)p[4 * i + 2] << 16 | (uint32_t)p[4 * i + 3] << 24;
        free(p);
    }
    uint16_t dark[7008];
    uint8_t shade[7008], blocks[96];
    unsigned positive[3], negative[3], means[3];
    CHECK(derive_calibration(sums[0], sums[1], sums[2], (unsigned[3]){22, 14, 16}, dark, shade,
                             blocks, positive, negative, means) == 0);
    uint8_t *s = fixture_data("calibration-shading.bin", 7008),
            *b = fixture_data("calibration-blocks.bin", 96),
            *d = fixture_data("calibration-dark-mean.bin", 14016);
    CHECK(!memcmp(shade, s, 7008) && !memcmp(blocks, b, 96));
    for (unsigned i = 0; i < 7008; ++i)
        CHECK(dark[i] == (unsigned)d[2 * i] + (unsigned)d[2 * i + 1] * 256);
    CHECK(!memcmp(positive, (unsigned[3]){36, 20, 25}, sizeof(positive)) &&
          !memcmp(negative, (unsigned[3]){55, 90, 136}, sizeof(negative)));
    sums[1][0] = 0;
    CHECK(derive_calibration(sums[0], sums[1], sums[2], (unsigned[3]){22, 14, 16}, dark, shade,
                             blocks, positive, negative, means) < 0);
    free(s);
    free(b);
    free(d);
    puts("PASS original dark/flat/block/exposure calibration and invalid-field rejection");
}
static void image_hash(const f2400_image *im, const char *pixels, const char *preview) {
    char sha[65];
    size_t n = (size_t)im->width * im->height * im->channels;
    CHECK(hash_data(im->pixels, n * 2, sha) == 0 && !strcmp(sha, pixels));
    CHECK(hash_data(im->preview, n, sha) == 0 && !strcmp(sha, preview));
}
static void test_images(const char *output) {
    uint16_t dark[7008];
    uint8_t shade[7008];
    for (unsigned i = 0; i < 7008; ++i) {
        dark[i] = 119 + (i * 7) % 12;
        shade[i] = 128 + (i * 13) % 128;
    }
    json_object *cases = fixture_json("images.json");
    for (unsigned i = 0; i < json_object_array_length(cases); ++i) {
        json_object *j = json_object_array_get_idx(cases, i);
        unsigned film = integer(j, "film"), gray = film == 2 || film == 4, channels = gray ? 1 : 3,
                 ratio = 4, w = 584, h = 876;
        size_t count = (size_t)w * h * channels;
        uint8_t *raw = malloc(count * 2);
        CHECK(raw);
        for (size_t k = 0; k < count; ++k) {
            unsigned v = 120 + (k * 17 + k / 292 * 11) % 850;
            raw[2 * k] = v >> 8;
            raw[2 * k + 1] = v;
        }
        f2400_image *im = NULL;
        CHECK(f2400_process(raw, count * 2, dark, shade, ratio, film, (unsigned[3]){215, 196, 186},
                            150, &im) == 0);
        CHECK(im->width == 876 && im->height == 584 && im->channels == channels);
        const char *high = string(j, "pixels_sha256"), *low = string(j, "preview_sha256");
        image_hash(im, high, low);
        for (unsigned r = 0; r < 4; ++r)
            CHECK(f2400_image_rotate(im, 1) == 0);
        image_hash(im, high, low);
        char png[PATH_MAX], tiff[PATH_MAX], name[64];
        snprintf(name, sizeof(name), "roundtrip-%u.png", film);
        CHECK(path_join(png, sizeof(png), output, name) == 0);
        snprintf(name, sizeof(name), "roundtrip-%u.tiff", film);
        CHECK(path_join(tiff, sizeof(tiff), output, name) == 0);
        CHECK(f2400_image_save(im, png) == 0 && f2400_image_save(im, tiff) == 0);
        f2400_image *loaded = f2400_image_load(png);
        CHECK(loaded);
        image_hash(loaded, high, low);
        CHECK(!unlink(png) && !unlink(tiff));
        CHECK(f2400_image_rotate(loaded, 0) == 0 && f2400_image_rotate(loaded, 1) == 0);
        image_hash(loaded, high, low);
        f2400_image_free(loaded);
        f2400_image_free(im);
        raw[0] = 4;
        CHECK(f2400_process(raw, count * 2, dark, shade, 4, film, (unsigned[3]){215, 196, 186}, 150,
                            &im) < 0);
        free(raw);
    }
    json_object_put(cases);
    puts("PASS 5 film/RAW image modes: full-depth pixels, dither, rotation, PNG/TIFF roundtrip and "
         "memory-only save");
}
int main(int argc, char **argv) {
    CHECK(argc == 3);
    fixtures = argv[1];
    test_curves();
    test_exposure();
    test_normalization();
    test_calibration();
    test_images(argv[2]);
    return 0;
}
