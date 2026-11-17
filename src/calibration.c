#include "internal.h"
int derive_calibration(const uint32_t dark[7008], const uint32_t white[7008],
                       const uint32_t correction[7008], const unsigned exposure[3],
                       uint16_t dark_out[7008], uint8_t shade[7008], uint8_t blocks[96],
                       unsigned positive[3], unsigned negative[3], unsigned means[3]) {
    const double fraction[3] = {.65, .22, .15};
    for (unsigned i = 0; i < 7008; ++i) {
        if (dark[i] > 32767 || white[i] > 64 * 1023 || correction[i] > 64 * 1023 ||
            white[i] < dark[i] || correction[i] < dark[i])
            return fail("invalid calibration sums");
        dark_out[i] = (uint16_t)(dark[i] / 64);
    }
    for (unsigned c = 0; c < 3; ++c) {
        if (exposure[c] < 1 || exposure[c] > 128)
            return fail("invalid calibration exposure");
        unsigned origin = c * 2336, peak = 0;
        means[c] = 0;
        for (unsigned x = 0; x < 2336; ++x) {
            unsigned v = white[origin + x] - dark[origin + x];
            if (v > peak)
                peak = v;
            means[c] += dark_out[origin + x];
        }
        if (!peak)
            return fail("no white-field signal");
        means[c] /= 2336;
        for (unsigned x = 0; x < 2336; ++x) {
            unsigned v = white[origin + x] - dark[origin + x];
            unsigned s = v ? v * 256 / peak : 255;
            shade[origin + x] = (uint8_t)(s < 128 ? 128 : s > 255 ? 255 : s);
        }
        uint64_t sum[32] = {0}, baseline[32] = {0}, maximum = 0;
        unsigned chosen = 0;
        for (unsigned b = 0; b < 32; ++b) {
            for (unsigned x = 16 + b * 72; x < 16 + (b + 1) * 72; ++x) {
                sum[b] += correction[origin + x] - dark[origin + x];
                baseline[b] += dark[origin + x];
            }
            if (sum[b] > maximum) {
                maximum = sum[b];
                chosen = b;
            }
        }
        if (!maximum)
            return fail("no correction-field signal");
        for (unsigned b = 0; b < 32; ++b)
            blocks[c * 32 + b] = (uint8_t)(sum[b] * 255 / maximum);
        uint64_t target = (uint64_t)exposure[c] * (72u * 1023 * 64 - baseline[chosen]);
        double factor = (double)target / (double)maximum;
        if (factor > 255)
            return fail("calibrated exposure exceeds original domain");
        positive[c] = (unsigned)factor;
        unsigned integral = (unsigned)(target / maximum);
        unsigned n = (unsigned)((long double)integral / (long double)fraction[c]);
        negative[c] = n > 136 ? 136 : n;
    }
    return 0;
}
int load_profile(const char *path, uint8_t shade[7008], unsigned base[3], unsigned negative[3],
                 json_object **metadata) {
    json_object *j = json_file(path);
    if (!j)
        return -1;
    json_object *v = NULL;
    int ok = 0;
    if (!json_object_object_get_ex(j, "schema", &v) || !json_object_is_type(v, json_type_int) ||
        json_object_get_int(v) != 1)
        goto done;
    if (!json_object_object_get_ex(j, "device", &v) || !json_object_is_type(v, json_type_string) ||
        strcmp(json_object_get_string(v), "Minolta #2882 1.10"))
        goto done;
    const char *flags[] = {"all_192_reads_good", "wire_sums_match", "final_ready"};
    for (unsigned i = 0; i < 3; ++i)
        if (!json_object_object_get_ex(j, flags[i], &v) ||
            !json_object_is_type(v, json_type_boolean) || !json_object_get_boolean(v))
            goto done;
    if (json_uints(j, "positive_exposures", base, 3, 8, 128) < 0 ||
        json_uints(j, "negative_exposures", negative, 3, 8, 136) < 0)
        goto done;
    char parent[PATH_MAX], filename[PATH_MAX], sha[65];
    if (strlen(path) >= sizeof(parent))
        goto done;
    strcpy(parent, path);
    char *slash = strrchr(parent, '/');
    if (slash)
        *slash = 0;
    else
        strcpy(parent, ".");
    if (path_join(filename, sizeof(filename), parent, "shading.bin") < 0)
        goto done;
    uint8_t *data = NULL;
    size_t n = 0;
    if (read_file(filename, &data, &n) < 0)
        goto done;
    if (n != 7008) {
        free(data);
        goto done;
    }
    for (size_t i = 0; i < n; ++i)
        if (data[i] < 128) {
            free(data);
            goto done;
        }
    hash_data(data, n, sha);
    json_object *files = NULL, *file = NULL, *bytes = NULL, *expected = NULL;
    if (!json_object_object_get_ex(j, "files", &files) ||
        !json_object_object_get_ex(files, "shading.bin", &file) ||
        !json_object_object_get_ex(file, "bytes", &bytes) ||
        !json_object_is_type(bytes, json_type_int) || json_object_get_int(bytes) != 7008 ||
        !json_object_object_get_ex(file, "sha256", &expected) ||
        !json_object_is_type(expected, json_type_string) ||
        strcmp(json_object_get_string(expected), sha)) {
        free(data);
        goto done;
    }
    memcpy(shade, data, 7008);
    free(data);
    ok = 1;
done:
    if (ok && metadata)
        *metadata = j;
    else
        json_object_put(j);
    return ok ? 0 : fail("invalid or modified calibration profile: %s", path);
}
int f2400_profile_info(const char *path, char *output, size_t capacity) {
    uint8_t shade[7008];
    unsigned base[3], negative[3];
    json_object *j = NULL;
    if (!output || !capacity || load_profile(path, shade, base, negative, &j) < 0)
        return -1;
    const char *s = json_object_to_json_string_ext(j, JSON_C_TO_STRING_PLAIN);
    if (strlen(s) >= capacity) {
        json_object_put(j);
        return fail("profile buffer too small");
    }
    strcpy(output, s);
    json_object_put(j);
    return 0;
}

#include "audit.h"
typedef struct {
    unsigned field, count[3], identity, releases;
    uint32_t sums[3][7008];
    uint8_t windows[3][72];
    int last_ready;
} calibration_audit;
static int calibration_record(const wire_record *r, void *context) {
    calibration_audit *a = context;
    unsigned op = r->cdb[0];
    a->last_ready = op == 0 && audit_good(r);
    if (op == 0) {
        static const uint8_t known[20] = {0x70, 0, 9, 0, 0, 0, 0, 12, 0,    0,
                                          0,    0, 0, 0, 0, 0, 0, 0,  0x10, 0};
        return audit_good(r) || (r->has_status && !r->host && !r->residual &&
                                 ((r->status == 8 && !r->driver && !r->sense_len) ||
                                  (r->status == 2 && r->driver == 8 && r->sense_len == 20 &&
                                   !memcmp(r->sense, known, 20))))
                   ? 0
                   : fail("calibration TUR error");
    }
    if (!audit_good(r))
        return fail("calibration command failed");
    if (op == 0x12) {
        if (a->identity++ || r->data_len != 36 || memcmp(r->cdb, "\x12\0\0\0\x24\0", 6) ||
            (r->data[0] & 31) != 6 || memcmp(r->data + 8, "Minolta #2882           1.10", 28))
            return fail("calibration identity");
    } else if (op == 0x31) {
        if (r->cdb_len != 10 || memcmp(r->cdb, "\x31\x02\0\0\0\0\0\0\0\0", 10))
            return fail("calibration terminal");
        ++a->releases;
    } else if (op == 0x24) {
        if (a->field >= 3 || r->data_len != 72 || r->cdb_len != 10 ||
            memcmp(r->cdb, "\x24\0\0\0\0\0\0\0\x48\0", 10) ||
            (a->field && a->count[a->field - 1] != 64))
            return fail("calibration windows");
        memcpy(a->windows[a->field++], r->data, 72);
    } else if (op == 0x28) {
        if (!a->field || a->count[a->field - 1] >= 64 || r->data_len != 14016 || r->cdb_len != 10)
            return fail("calibration READ length/order");
        uint8_t want[10] = {0x28, 0, 0, 0, a->field == 1 ? 0 : 1, 0, 0, 54, 192, 0};
        if (memcmp(r->cdb, want, 10))
            return fail("calibration READ CDB");
        for (unsigned i = 0; i < 7008; ++i) {
            unsigned v = (unsigned)r->data[2 * i] * 256 + r->data[2 * i + 1];
            if (v > 1023)
                return fail("calibration ADC");
            a->sums[a->field - 1][i] += v;
        }
        ++a->count[a->field - 1];
    } else
        return fail("unexpected calibration opcode");
    return 0;
}
int export_profile(const char *dir) {
    char path[PATH_MAX];
    if (path_join(path, sizeof(path), dir, "capture.log") < 0)
        return -1;
    calibration_audit *a = calloc(1, sizeof(*a));
    if (!a)
        return fail("out of memory");
    if (audit_records(path, calibration_record, a) < 0 || a->field != 3 || a->count[2] != 64 ||
        a->identity != 1 || a->releases != 2 || !a->last_ready) {
        free(a);
        return fail("calibration wire audit failed");
    }
    uint8_t canonical[72] = {0,  0,  0,  0, 0, 0, 0, 64,  0, 0, 9, 96, 9, 96, 0,   0,   0,  245,
                             0,  0,  0,  0, 0, 0, 9, 32,  0, 0, 0, 1,  0, 0,  128, 255, 16, 0,
                             0,  0,  0,  0, 0, 0, 0, 0,   0, 0, 0, 0,  1, 1,  0,   15,  15, 15,
                             20, 17, 63, 0, 0, 0, 3, 170, 0, 0, 0, 0,  0, 22, 0,   14,  0,  16};
    unsigned exposures[3];
    for (unsigned c = 0; c < 3; ++c) {
        exposures[c] = (unsigned)a->windows[1][66 + 2 * c] * 256 + a->windows[1][67 + 2 * c];
        if (!exposures[c] || exposures[c] > 128) {
            free(a);
            return fail("calibration exposure");
        }
        canonical[66 + 2 * c] = exposures[c] >> 8;
        canonical[67 + 2 * c] = exposures[c];
    }
    for (unsigned f = 0; f < 3; ++f) {
        uint8_t w[72];
        memcpy(w, canonical, 72);
        if (!f) {
            memcpy(w + 18, "\xff\xff\xfd\x18", 4);
            w[51] = w[52] = w[53] = 14;
        }
        if (memcmp(w, a->windows[f], 72)) {
            free(a);
            return fail("calibration factory window mismatch");
        }
        const char *names[] = {"capture-dark-sums.bin", "capture-white-sums.bin",
                               "capture-correction-sums.bin"};
        path_join(path, sizeof(path), dir, names[f]);
        uint8_t *data = NULL;
        size_t n = 0;
        if (read_file(path, &data, &n) < 0) {
            free(a);
            return -1;
        }
        int match = n == 28032;
        for (unsigned i = 0; match && i < 7008; ++i)
            match = ((uint32_t)data[4 * i] | (uint32_t)data[4 * i + 1] << 8 |
                     (uint32_t)data[4 * i + 2] << 16 | (uint32_t)data[4 * i + 3] << 24) ==
                    a->sums[f][i];
        free(data);
        if (!match) {
            free(a);
            return fail("calibration sums differ from wire");
        }
    }
    uint16_t dark[7008];
    uint8_t shade[7008], blocks[96], dark_bytes[14016];
    unsigned positive[3], negative[3], means[3];
    if (derive_calibration(a->sums[0], a->sums[1], a->sums[2], exposures, dark, shade, blocks,
                           positive, negative, means) < 0) {
        free(a);
        return -1;
    }
    free(a);
    for (unsigned i = 0; i < 7008; ++i) {
        dark_bytes[2 * i] = dark[i];
        dark_bytes[2 * i + 1] = dark[i] >> 8;
    }
    json_object *j = json_object_new_object(), *files = json_object_new_object();
    json_object_object_add(j, "schema", json_object_new_int(1));
    json_object_object_add(j, "device", json_object_new_string("Minolta #2882 1.10"));
    json_object_object_add(
        j, "scope",
        json_object_new_string("35mm native C calibration; supported offsets 20/17/63"));
    char timestamp[32];
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm);
    json_object_object_add(j, "created_utc", json_object_new_string(timestamp));
    json_object_object_add(j, "positive_exposures", uint_array(positive, 3));
    json_object_object_add(j, "negative_exposures", uint_array(negative, 3));
    json_object_object_add(j, "dark_means", uint_array(means, 3));
    json_object_object_add(j, "calibration_exposures", uint_array(exposures, 3));
    json_object_object_add(j, "all_192_reads_good", json_object_new_boolean(1));
    json_object_object_add(j, "wire_sums_match", json_object_new_boolean(1));
    json_object_object_add(j, "final_ready", json_object_new_boolean(1));
    json_object_object_add(j, "reads_per_field", json_object_new_int(64));
    const char *names[] = {"shading.bin", "dark.bin", "block-table.bin"};
    const uint8_t *buffers[] = {shade, dark_bytes, blocks};
    size_t lengths[] = {7008, 14016, 96};
    int result = 0;
    for (unsigned i = 0; i < 3; ++i) {
        path_join(path, sizeof(path), dir, names[i]);
        if (write_file(path, buffers[i], lengths[i]) < 0) {
            result = -1;
            break;
        }
        char sha[65];
        hash_data(buffers[i], lengths[i], sha);
        json_object *meta = json_object_new_object();
        json_object_object_add(meta, "bytes", json_object_new_int64(lengths[i]));
        json_object_object_add(meta, "sha256", json_object_new_string(sha));
        json_object_object_add(files, names[i], meta);
    }
    json_object_object_add(j, "files", files);
    if (!result) {
        path_join(path, sizeof(path), dir, "profile.json");
        result = write_json(path, j);
    }
    json_object_put(j);
    return result;
}
