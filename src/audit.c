/* Independent validation of actual wire logs, saved sums and raw bytes.
 * Malformed/short/uncompleted commands never become successful transfers. */
#include "audit.h"
#include <openssl/evp.h>
static int hex_bytes(const char *s, uint8_t *bytes, unsigned cap, unsigned *n) {
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
            ++s;
        if (!*s)
            break;
        if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1]) ||
            (s[2] && !isspace((unsigned char)s[2])) || *n >= cap)
            return -1;
        char b[3] = {s[0], s[1], 0};
        bytes[(*n)++] = (uint8_t)strtoul(b, NULL, 16);
        s += 2;
    }
    return 0;
}
int audit_records(const char *path, record_callback callback, void *data) {
    FILE *f = fopen(path, "r");
    if (!f)
        return fail("audit log unavailable: %s", path);
    char *line = NULL;
    size_t capacity = 0;
    wire_record record = {0};
    int active = 0, result = 0;
    while (getline(&line, &capacity, f) >= 0) {
        char *cdb = strstr(line, " CDB:");
        if (cdb) {
            if (active && (result = callback(&record, data)) < 0)
                break;
            memset(&record, 0, sizeof(record));
            active = 1;
            if (hex_bytes(cdb + 5, record.cdb, sizeof(record.cdb), &record.cdb_len) < 0 ||
                (record.cdb_len != 6 && record.cdb_len != 10)) {
                result = fail("malformed CDB log");
                break;
            }
        } else if (active && !strncmp(line, "status=", 7)) {
            if (sscanf(line, "status=%x host=%x driver=%x residual=%d", &record.status,
                       &record.host, &record.driver, &record.residual) != 4) {
                result = -1;
                break;
            }
            record.has_status = 1;
        } else if (active && !strncmp(line, "sense:", 6)) {
            if (hex_bytes(line + 6, record.sense, sizeof(record.sense), &record.sense_len) < 0) {
                result = -1;
                break;
            }
        } else if (active && strlen(line) >= 6 && line[4] == ':' &&
                   isxdigit((unsigned char)line[0]) && isxdigit((unsigned char)line[1]) &&
                   isxdigit((unsigned char)line[2]) && isxdigit((unsigned char)line[3])) {
            unsigned offset = 0;
            sscanf(line, "%x", &offset);
            if (offset != record.data_len ||
                hex_bytes(line + 5, record.data, sizeof(record.data), &record.data_len) < 0) {
                result = fail("malformed payload dump");
                break;
            }
        }
    }
    if (!result && active)
        result = callback(&record, data);
    if (ferror(f))
        result = -1;
    free(line);
    fclose(f);
    return result;
}
int audit_good(const wire_record *r) {
    return r->has_status && !r->status && !r->host && !r->driver && !r->residual && !r->sense_len;
}
static int busy(const wire_record *r) {
    static const uint8_t known[20] = {0x70, 0, 9, 0, 0, 0, 0, 12, 0,    0,
                                      0,    0, 0, 0, 0, 0, 0, 0,  0x10, 0};
    return r->has_status && !r->host && !r->residual &&
           ((r->status == 8 && !r->driver && !r->sense_len) ||
            (r->status == 2 && r->driver == 8 && r->sense_len == 20 &&
             !memcmp(r->sense, known, 20)));
}
typedef struct {
    unsigned ratio, exp[3], meter, gray, window, rows, dark_reads, narrow_reads, releases, identity,
        statuses, last_opcode;
    uint32_t dark[7008];
    unsigned narrow_sum;
    EVP_MD_CTX *hash;
    int final_good;
} capture_audit;
static int capture_record(const wire_record *r, void *context) {
    capture_audit *a = context;
    unsigned op = r->cdb[0];
    a->final_good = audit_good(r);
    a->last_opcode = op;
    if (op == 0)
        return (audit_good(r) || busy(r)) && r->cdb_len == 6 && !memcmp(r->cdb, "\0\0\0\0\0\0", 6)
                   ? 0
                   : fail("unexpected TUR during audit");
    if (!audit_good(r))
        return fail("failed command in completed capture");
    if (op == 0x12) {
        if (a->identity++ || r->cdb_len != 6 || memcmp(r->cdb, "\x12\0\0\0\x24\0", 6) ||
            r->data_len != 36 || (r->data[0] & 31) != 6 ||
            memcmp(r->data + 8, "Minolta #2882           1.10", 28))
            return fail("audit identity mismatch");
    } else if (op == 0x31) {
        if (r->cdb_len != 10 || memcmp(r->cdb, "\x31\x02\0\0\0\0\0\0\0\0", 10) || r->data_len ||
            a->releases >= 2 || (!a->releases && a->window) ||
            (a->releases == 1 && a->rows != 3504 / a->ratio + (a->gray ? 0 : 16 / a->ratio)))
            return fail("audit release mismatch");
        ++a->releases;
    } else if (op == 0x24) {
        static const uint8_t canonical[72] = {
            0,   0,   0,   0,  0, 0, 0, 64,  0, 0, 9, 96, 9, 96, 0,   0,   0,  245,
            255, 255, 253, 24, 0, 0, 9, 32,  0, 0, 0, 1,  0, 0,  128, 255, 16, 0,
            0,   0,   0,   0,  0, 0, 0, 0,   0, 0, 0, 0,  1, 1,  0,   14,  14, 14,
            20,  17,  63,  0,  0, 0, 3, 170, 0, 0, 0, 0,  0, 36, 0,   20,  0,  25};
        uint8_t w[72];
        memcpy(w, canonical, 72);
        if (a->window >= 3 || r->cdb_len != 10 || memcmp(r->cdb, "\x24\0\0\0\0\0\0\0\x48\0", 10) ||
            r->data_len != 72)
            return fail("audit window count/layout");
        if (a->window != 1)
            for (unsigned c = 0; c < 3; ++c) {
                w[66 + 2 * c] = a->exp[c] >> 8;
                w[67 + 2 * c] = a->exp[c];
            }
        if (a->window == 1) {
            w[16] = 5;
            w[17] = 65;
            memset(w + 18, 0, 4);
            w[24] = 0;
            w[25] = 100;
            w[33] = 252;
            w[51] = w[52] = w[53] = 15;
            w[67] = 36;
            w[69] = 19;
            w[71] = 20;
        }
        if (a->window == 2) {
            memset(w + 18, 0, 4);
            unsigned dpi = a->meter ? 300 : 2438 / a->ratio;
            w[10] = dpi >> 8;
            w[11] = dpi;
            if (!a->meter) {
                w[12] = dpi >> 8;
                w[13] = dpi;
            }
            w[48] = w[49] = a->ratio;
            w[33] = a->gray ? 250 : 255;
            w[51] = w[52] = w[53] = a->gray && !a->meter ? 14 : 15;
            w[57] = a->meter ? 25 : a->ratio == 1 ? 34 : 29;
            w[60] = a->meter ? 3 : a->ratio == 1 ? 6 : 4;
            w[61] = a->meter ? 170 : a->ratio == 1 ? 27 : 31;
            w[64] = 12;
            w[65] = 53;
        }
        if (memcmp(w, r->data, 72))
            return fail("audit window %u differs from factory fields", a->window);
        ++a->window;
    } else if (op == 0x28) {
        unsigned expected = (unsigned)r->cdb[6] * 65536 + (unsigned)r->cdb[7] * 256 + r->cdb[8];
        if (r->cdb_len != 10 || r->data_len != expected)
            return fail("short READ in audit");
        if (a->window == 1) {
            if (memcmp(r->cdb, "\x28\0\0\0\0\0\0\x36\xc0\0", 10) ||
                ++a->dark_reads > (a->meter ? 16u : 64u))
                return fail("audit dark READ mismatch");
            for (unsigned i = 0; i < 7008; ++i) {
                unsigned v = (unsigned)r->data[2 * i] * 256 + r->data[2 * i + 1];
                if (v > 1023)
                    return fail("dark ADC");
                a->dark[i] += v;
            }
        } else if (a->window == 2) {
            if (memcmp(r->cdb, "\x28\0\0\0\0\0\0\0\xc8\0", 10) || ++a->narrow_reads > 10)
                return fail("audit narrow READ mismatch");
            for (unsigned i = 0; i < 200; i += 2) {
                if (r->data[i] > 3)
                    return fail("narrow ADC");
                a->narrow_sum += (unsigned)r->data[i] * 256 + r->data[i + 1];
            }
        } else if (a->window == 3) {
            unsigned h = 3504 / a->ratio, stagger = 16 / a->ratio, row = a->rows;
            unsigned mask = a->meter                ? 7
                            : row < stagger / 2     ? 4
                            : row < stagger         ? 6
                            : row < h               ? 7
                            : row < h + stagger / 2 ? 3
                                                    : 1;
            unsigned len = 2336 / a->ratio * 2 * (a->gray ? 1 : (unsigned)__builtin_popcount(mask));
            uint8_t want[10] = {0x28,     0, 0,        0,   a->gray ? 250 : 248 | mask,
                                a->ratio, 0, len >> 8, len, 0};
            if (memcmp(want, r->cdb, 10) || a->rows >= h + (a->gray ? 0 : stagger))
                return fail("audit image READ mask/order mismatch");
            if (!EVP_DigestUpdate(a->hash, r->data, r->data_len))
                return fail("hash update");
            ++a->rows;
        } else
            return fail("READ before window");
    } else if (op == 0x1a) {
        if (r->cdb_len != 6 || memcmp(r->cdb, "\x1a\x10\x20\0\x08\0", 6) || r->data_len != 8 ||
            a->releases != 2 || ++a->statuses != 1)
            return fail("audit status mismatch");
    } else
        return fail("unexpected opcode %02x in audit", op);
    return 0;
}
int verify_capture(const char *prefix, unsigned ratio, const unsigned exposures[3], int meter,
                   int gray, json_object **audit) {
    capture_audit a = {0};
    a.ratio = ratio;
    a.meter = meter;
    a.gray = gray;
    memcpy(a.exp, exposures, sizeof(a.exp));
    a.hash = EVP_MD_CTX_new();
    if (!a.hash || !EVP_DigestInit_ex(a.hash, EVP_sha256(), NULL)) {
        EVP_MD_CTX_free(a.hash);
        return fail("hash init");
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s.log", prefix);
    int good = audit_records(path, capture_record, &a) == 0;
    good = good && a.identity == 1 && a.window == 3 && a.releases == 2 && a.statuses == 1 &&
           a.last_opcode == 0x1a && a.final_good && a.dark_reads == (meter ? 16u : 64u) &&
           a.narrow_reads == 10 && a.narrow_sum <= 760000 &&
           a.rows == 3504 / ratio + (gray ? 0 : 16 / ratio);
    uint8_t *bytes = NULL;
    size_t n = 0;
    snprintf(path, sizeof(path), "%s-dark.bin", prefix);
    if (good && read_file(path, &bytes, &n) == 0) {
        good = n == 14016;
        for (unsigned i = 0; good && i < 7008; ++i)
            good = (unsigned)bytes[2 * i] + (unsigned)bytes[2 * i + 1] * 256 ==
                   a.dark[i] / a.dark_reads;
        free(bytes);
        bytes = NULL;
    } else
        good = 0;
    unsigned char digest[32];
    unsigned length = 0;
    EVP_DigestFinal_ex(a.hash, digest, &length);
    EVP_MD_CTX_free(a.hash);
    char wire_sha[65], file_sha[65];
    for (unsigned i = 0; i < 32; ++i)
        snprintf(wire_sha + 2 * i, 3, "%02x", digest[i]);
    snprintf(path, sizeof(path), "%s-raw.bin", prefix);
    if (good && read_file(path, &bytes, &n) == 0) {
        hash_data(bytes, n, file_sha);
        free(bytes);
        good = n == (size_t)(2336 / ratio) * (3504 / ratio + (meter && !gray ? 2 : 0)) *
                        (gray ? 2 : 6) &&
               !strcmp(wire_sha, file_sha);
    } else
        good = 0;
    if (!good)
        return fail("capture failed independent wire/raw/dark audit");
    json_object *j = json_object_new_object();
    json_object_object_add(j, "frame_reads", json_object_new_int(a.rows));
    json_object_object_add(j, "dark_reads", json_object_new_int(a.dark_reads));
    json_object_object_add(j, "raw_sha256", json_object_new_string(wire_sha));
    json_object_object_add(j, "original_windows_match", json_object_new_boolean(1));
    json_object_object_add(j, "dark_average_matches", json_object_new_boolean(1));
    json_object_object_add(j, "raw_matches_wire", json_object_new_boolean(1));
    json_object_object_add(j, "final_ready", json_object_new_boolean(1));
    if (audit)
        *audit = j;
    else
        json_object_put(j);
    return 0;
}
