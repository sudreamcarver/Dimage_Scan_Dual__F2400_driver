/* Synthetic sysfs, mocked device open: never opens real disks or scanners. */
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <assert.h>
static char fixture_root[PATH_MAX];
static unsigned opens, closes;
static int deny_open, wrong_node, wrong_type;
static char opened[PATH_MAX];
static int fake_open(const char *path, int flags, ...) {
    assert(flags & O_NOFOLLOW);
    ++opens;
    snprintf(opened, sizeof(opened), "%s", path);
    if (deny_open) {
        errno = EACCES;
        return -1;
    }
    return 42;
}
static int fake_fstat(int fd, struct stat *s) {
    assert(fd == 42);
    memset(s, 0, sizeof(*s));
    s->st_mode = wrong_type ? S_IFBLK : S_IFCHR;
    s->st_rdev = makedev(21, wrong_node ? 2 : 3);
    return 0;
}
static int fake_close(int fd) {
    assert(fd == 42);
    ++closes;
    return 0;
}
#define F2400_SYS_SG_ROOT fixture_root
#define F2400_DEV_ROOT "/synthetic-dev"
#define open fake_open
#define fstat fake_fstat
#define close fake_close
#include "f2400-device.h"
#undef open
#undef fstat
#undef close
static void attr(const char *node, const char *key, const char *value) {
    char path[PATH_MAX];
    assert(snprintf(path, sizeof(path), "%s/%s/%s", fixture_root, node, key) < (int)sizeof(path));
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "%s\n", value);
    assert(!fclose(f));
}
static void device(const char *name, const char *type, const char *vendor, const char *model,
                   const char *rev) {
    char path[PATH_MAX];
    assert(snprintf(path, sizeof(path), "%s/%s", fixture_root, name) < (int)sizeof(path));
    assert(!mkdir(path, 0700));
    size_t n = strlen(path);
    assert(n + 8 < sizeof(path));
    strcpy(path + n, "/device");
    assert(!mkdir(path, 0700));
    attr(name, "device/type", type);
    attr(name, "device/vendor", vendor);
    attr(name, "device/model", model);
    attr(name, "device/rev", rev);
    attr(name, "device/state", "running");
    attr(name, "dev", "21:3");
}
int main(void) {
    strcpy(fixture_root, "/tmp/f2400-device-tests-XXXXXX");
    assert(mkdtemp(fixture_root));
    device("sg0", "0", "ATA", "WDC", "1");
    device("sg1", "5", "TSSTcorp", "DVD", "1");
    device("sg2", "0", "ATA", "ZHITAI", "1");
    device("sg3", "6", "Minolta ", "#2882           ", "1.10");
    assert(f2400_open_scanner() == 42 && opens == 1 && !strcmp(opened, "/synthetic-dev/sg3"));
    attr("sg3", "device/type", "0");
    assert(f2400_open_scanner() < 0 && opens == 1);
    attr("sg3", "device/type", "6");
    attr("sg3", "device/model", "other");
    assert(f2400_open_scanner() < 0 && opens == 1);
    attr("sg3", "device/model", "#2882");
    attr("sg3", "device/rev", "9.99");
    assert(f2400_open_scanner() < 0 && opens == 1);
    attr("sg3", "device/rev", "1.10");
    attr("sg3", "device/state", "offline");
    assert(f2400_open_scanner() < 0 && opens == 1);
    attr("sg3", "device/state", "running");
    deny_open = 1;
    assert(f2400_open_scanner() < 0 && errno == EACCES && opens == 2);
    deny_open = 0;
    wrong_node = 1;
    assert(f2400_open_scanner() < 0 && closes == 1);
    wrong_node = 0;
    wrong_type = 1;
    assert(f2400_open_scanner() < 0 && closes == 2);
    wrong_type = 0;
    device("sg4", "6", "Minolta", "#2882", "1.10");
    unsigned before = opens;
    assert(f2400_open_scanner() < 0 && errno == ENOTUNIQ && opens == before);
    assert(!f2400_sg_name("sg") && !f2400_sg_name("sg3/../sda") && f2400_sg_name("sg123"));
    /* Remove only files created in this temporary fixture. */
    for (unsigned i = 0; i <= 4; ++i) {
        char name[8], path[PATH_MAX];
        snprintf(name, sizeof(name), "sg%u", i);
        const char *keys[] = {"device/type", "device/vendor", "device/model",
                              "device/rev",  "device/state",  "dev"};
        for (unsigned k = 0; k < 6; ++k) {
            assert(snprintf(path, sizeof(path), "%s/%s/%s", fixture_root, name, keys[k]) <
                   (int)sizeof(path));
            assert(!unlink(path));
        }
        assert(snprintf(path, sizeof(path), "%s/%s/device", fixture_root, name) <
               (int)sizeof(path));
        assert(!rmdir(path));
        assert(snprintf(path, sizeof(path), "%s/%s", fixture_root, name) < (int)sizeof(path));
        assert(!rmdir(path));
    }
    assert(!rmdir(fixture_root));
    puts("PASS: scanner selection; disks/DVD excluded; ambiguous/offline/changed nodes and "
         "permission errors stop.");
    return 0;
}
