/* Entire native job pipeline with virtual SG_IO. No real device opened.
 * Exercises output audits and cleanup, not just individual arithmetic calls. */
#include "internal.h"
#include <assert.h>
#include <dirent.h>
static unsigned opens, closes, reads, image_reads, releases;
static int scenario;
static uint8_t window[72];
int __wrap_f2400_device_open(void) {
    ++opens;
    return 100;
}
int __wrap_flock(int fd, int operation) {
    assert(fd == 100 && operation == (LOCK_EX | LOCK_NB));
    return 0;
}
int __real_close(int fd);
int __wrap_close(int fd) {
    if (fd == 100) {
        ++closes;
        return 0;
    }
    return __real_close(fd);
}
int __wrap_ioctl(int fd, unsigned long request, ...) {
    assert(fd == 100 && request == SG_IO);
    va_list ap;
    va_start(ap, request);
    sg_io_hdr_t *io = va_arg(ap, sg_io_hdr_t *);
    va_end(ap);
    unsigned op = io->cmdp[0];
    if (op == 0x12) {
        assert(io->dxfer_len == 36);
        uint8_t id[36] = {6, 0, 2, 2, 31, 0, 0, 1};
        memcpy(id + 8, "Minolta #2882           1.10", 28);
        if (scenario == 3)
            id[0] = 0;
        memcpy(io->dxferp, id, 36);
    } else if (op == 0 || op == 0x31) {
        assert(!io->dxfer_len);
        if (op == 0x31)
            ++releases;
    } else if (op == 0x1a) {
        assert(io->dxfer_len == 8);
        memcpy(io->dxferp, "\x03\0\x06\0\x20\x02\0\0", 8);
    } else if (op == 0x24) {
        assert(io->dxfer_len == 72);
        memcpy(window, io->dxferp, 72);
    } else if (op == 0x15) {
        assert(io->dxfer_len == 8 && ((uint8_t *)io->dxferp)[6] == 255);
        if (scenario == 2) {
            const uint8_t sense[20] = {0x70, 0, 9, 0, 0, 0, 0, 12, 0,    0,
                                       0,    0, 0, 0, 0, 0, 0, 0,  0x10, 0};
            io->status = 2;
            io->driver_status = 8;
            io->sb_len_wr = 20;
            memcpy(io->sbp, sense, 20);
        }
    } else if (op == 0x28) {
        ++reads;
        int is_dark = window[18] == 255;
        int image = window[48] == 4 || window[48] == 8;
        if (image)
            ++image_reads;
        uint8_t *p = io->dxferp;
        for (unsigned i = 0; i < io->dxfer_len; i += 2) {
            unsigned v = is_dark ? 120 : image ? 300 + ((i / 2 * 7 + image_reads * 13) % 400) : 400;
            p[i] = v >> 8;
            p[i + 1] = v;
        }
        if (scenario == 1 && reads == 1)
            io->resid = 2;
        if (scenario == 2 && image_reads == 17)
            stop_requested = 1;
    } else
        assert(!"unexpected command");
    return 0;
}
static void copy_fixture(const char *fixtures, const char *name, const char *output) {
    char path[PATH_MAX];
    uint8_t *p = NULL;
    size_t n = 0;
    assert(path_join(path, sizeof(path), fixtures, name) == 0);
    assert(read_file(path, &p, &n) == 0);
    assert(write_file(output, p, n) == 0);
    free(p);
}
static void remove_tree(const char *path) {
    DIR *dir = opendir(path);
    assert(dir);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char child[PATH_MAX];
        assert(path_join(child, sizeof(child), path, entry->d_name) == 0);
        struct stat s;
        assert(!lstat(child, &s));
        if (S_ISDIR(s.st_mode))
            remove_tree(child);
        else
            assert(!unlink(child));
    }
    closedir(dir);
    assert(!rmdir(path));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    char root[] = "/tmp/f2400-jobs-XXXXXX";
    assert(mkdtemp(root));
    char profile[PATH_MAX], path[PATH_MAX], dir[PATH_MAX];
    path_join(profile, sizeof(profile), root, "profile.json");
    copy_fixture(argv[1], "calibration.json", profile);
    path_join(path, sizeof(path), root, "shading.bin");
    copy_fixture(argv[1], "calibration-shading.bin", path);
    for (unsigned film = 0; film < 5; ++film) {
        scenario = 0;
        opens = closes = reads = image_reads = releases = 0;
        stop_requested = 0;
        char name[32];
        snprintf(name, sizeof(name), "film-%u", film);
        path_join(dir, sizeof(dir), root, name);
        assert(f2400_scan(dir, profile, film, 4, 1) == 0);
        assert(opens == 1 && closes == 1 && releases == 4);
        path_join(path, sizeof(path), dir, "result.json");
        json_object *j = json_file(path), *state = NULL;
        assert(j && json_object_object_get_ex(j, "status", &state));
        assert(!strcmp(json_object_get_string(state), "complete"));
        json_object_put(j);
        path_join(path, sizeof(path), dir, film >= 3 ? "image-raw.tiff" : "image-factory.tiff");
        f2400_image *im = f2400_image_load(path);
        assert(im && im->width == 876 && im->height == 584);
        f2400_image_free(im);
    }
    for (scenario = 1; scenario <= 3; ++scenario) {
        opens = closes = reads = image_reads = releases = 0;
        stop_requested = 0;
        char name[32];
        snprintf(name, sizeof(name), "failure-%d", scenario);
        path_join(dir, sizeof(dir), root, name);
        int status = f2400_scan(dir, profile, F2400_POSITIVE, 4, 1);
        assert(status == (scenario == 2 ? 130 : scenario == 3 ? 2 : 1));
        if (scenario == 1)
            assert(reads == 1);
        if (scenario == 2)
            assert(image_reads == 17);
        if (scenario == 3)
            assert(reads == 0 && releases == 0);
        path_join(path, sizeof(path), dir, "image-factory.png");
        assert(access(path, F_OK));
        assert(opens == 1 && closes == 1);
    }
    stop_requested = 0;
    scenario = 0;
    path_join(dir, sizeof(dir), root, "initialize");
    assert(f2400_calibrate(dir, 1, 1) == 0);
    path_join(path, sizeof(path), dir, "profile.json");
    uint8_t shade[7008];
    unsigned pos[3], neg[3];
    assert(load_profile(path, shade, pos, neg, NULL) == 0);
    path_join(dir, sizeof(dir), root, "unconfirmed");
    assert(f2400_calibrate(dir, 1, 0) == 2 && access(dir, F_OK));
    remove_tree(root);
    puts("PASS complete native jobs: 5 modes, audits, fresh calibration, stop-on-short-read, "
         "cancellation and wrong identity");
    return 0;
}
