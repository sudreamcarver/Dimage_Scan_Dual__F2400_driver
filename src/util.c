#include "internal.h"
#include <openssl/evp.h>
static _Thread_local char error_message[512];
const char *f2400_error(void) {
    return error_message;
}
int fail(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error_message, sizeof(error_message), format, args);
    va_end(args);
    fprintf(stderr, "F2400 ERROR: %s\n", error_message);
    return -1;
}
int path_join(char *out, size_t size, const char *dir, const char *name) {
    if (snprintf(out, size, "%s/%s", dir, name) >= (int)size)
        return fail("path is too long");
    return 0;
}
int read_file(const char *path, uint8_t **data, size_t *size) {
    *data = NULL;
    *size = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return fail("open %s: %s", path, strerror(errno));
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0) {
        fclose(f);
        return fail("file size: %s", path);
    }
    long n = ftell(f);
    rewind(f);
    if (n > 512 * 1024 * 1024) {
        fclose(f);
        return fail("input too large");
    }
    uint8_t *p = malloc(n ? (size_t)n : 1);
    if (!p) {
        fclose(f);
        return fail("out of memory");
    }
    int ok = fread(p, 1, (size_t)n, f) == (size_t)n;
    int closed = fclose(f);
    if (!ok || closed) {
        free(p);
        return fail("read %s", path);
    }
    *data = p;
    *size = (size_t)n;
    return 0;
}
int write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return fail("open %s: %s", path, strerror(errno));
    int ok = fwrite(data, 1, size, f) == size;
    int closed = fclose(f);
    return ok && !closed ? 0 : fail("write %s", path);
}
int hash_data(const void *data, size_t size, char out[65]) {
    unsigned char digest[32];
    unsigned n = 0;
    if (!EVP_Digest(data, size, digest, &n, EVP_sha256(), NULL) || n != 32)
        return fail("SHA256 failed");
    for (unsigned i = 0; i < 32; ++i)
        snprintf(out + 2 * i, 3, "%02x", digest[i]);
    return 0;
}
int write_json(const char *path, json_object *value) {
    char temp[PATH_MAX];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp))
        return fail("path too long");
    const char *s = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PRETTY);
    if (write_file(temp, s, strlen(s)) < 0 || rename(temp, path))
        return fail("write report %s", path);
    return 0;
}
json_object *json_file(const char *path) {
    json_object *j = json_object_from_file(path);
    if (!j)
        fail("invalid JSON %s", path);
    return j;
}
json_object *uint_array(const unsigned *values, unsigned count) {
    json_object *a = json_object_new_array();
    for (unsigned i = 0; i < count; ++i)
        json_object_array_add(a, json_object_new_int64(values[i]));
    return a;
}
int json_uints(json_object *j, const char *key, unsigned *out, unsigned n, unsigned low,
               unsigned high) {
    json_object *a = NULL;
    if (!json_object_object_get_ex(j, key, &a) || !json_object_is_type(a, json_type_array) ||
        json_object_array_length(a) != n)
        return fail("invalid %s", key);
    for (unsigned i = 0; i < n; ++i) {
        json_object *v = json_object_array_get_idx(a, i);
        int64_t x = json_object_get_int64(v);
        if (!json_object_is_type(v, json_type_int) || x < low || x > high)
            return fail("%s outside supported range", key);
        out[i] = (unsigned)x;
    }
    return 0;
}
static void request_stop(int signo) {
    (void)signo;
    stop_requested = 1;
}
void f2400_install_signals(void) {
    struct sigaction sa = {0};
    sa.sa_handler = request_stop;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}
int open_locked(void) {
    int fd = f2400_device_open();
    if (fd < 0)
        return fail("scanner unavailable: %s", strerror(errno));
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        return fail("scanner is in use");
    }
    uint8_t cdb[6] = {0x12, 0, 0, 0, 36, 0}, id[36] = {0};
    if (command(fd, "IDENTITY", cdb, id, 36) != 36 || (id[0] & 31) != 6 ||
        memcmp(id + 8, "Minolta ", 8) || memcmp(id + 16, "#2882           1.10", 20)) {
        close(fd);
        return fail("scanner identity mismatch");
    }
    return fd;
}
