/* Discover only the supported scanner through sysfs before opening an sg node.
 * Never probe disks/DVDs to find the scanner. Existing INQUIRY checks remain
 * mandatory after opening/locking. No fixed node number or disk fallback. */
#ifndef F2400_DEVICE_H
#define F2400_DEVICE_H
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
#ifndef F2400_SYS_SG_ROOT
#define F2400_SYS_SG_ROOT "/sys/class/scsi_generic"
#endif
#ifndef F2400_DEV_ROOT
#define F2400_DEV_ROOT "/dev"
#endif
static inline int f2400_attribute(const char *root, const char *node, const char *attribute,
                                  char *value, size_t size) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s/%s", root, node, attribute) >= (int)sizeof(path))
        return -1;
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int ok = fgets(value, (int)size, f) != NULL;
    fclose(f);
    if (!ok)
        return -1;
    size_t n = strlen(value);
    while (n && isspace((unsigned char)value[n - 1]))
        value[--n] = 0;
    return 0;
}
static inline int f2400_sysfs_match(const char *root, const char *node) {
    char value[128];
    return f2400_attribute(root, node, "device/type", value, sizeof(value)) == 0 &&
           !strcmp(value, "6") &&
           f2400_attribute(root, node, "device/vendor", value, sizeof(value)) == 0 &&
           !strcmp(value, "Minolta") &&
           f2400_attribute(root, node, "device/model", value, sizeof(value)) == 0 &&
           !strcmp(value, "#2882") &&
           f2400_attribute(root, node, "device/rev", value, sizeof(value)) == 0 &&
           !strcmp(value, "1.10");
}
static inline int f2400_sg_name(const char *name) {
    if (strncmp(name, "sg", 2) || !name[2])
        return 0;
    for (const char *p = name + 2; *p; ++p)
        if (!isdigit((unsigned char)*p))
            return 0;
    return 1;
}
static inline int f2400_find_scanner(const char *root, char *node, size_t capacity) {
    DIR *dir = opendir(root);
    if (!dir) {
        perror("scanner sysfs");
        return -1;
    }
    struct dirent *entry;
    unsigned matches = 0;
    while ((entry = readdir(dir))) {
        if (!f2400_sg_name(entry->d_name) || !f2400_sysfs_match(root, entry->d_name))
            continue;
        if (strlen(entry->d_name) >= capacity) {
            closedir(dir);
            errno = ENAMETOOLONG;
            return -1;
        }
        strcpy(node, entry->d_name);
        ++matches;
    }
    closedir(dir);
    if (matches != 1) {
        fprintf(stderr, "SCANNER DEVICE: %s; no device opened\n",
                matches ? "multiple Minolta #2882 1.10 scanners found"
                        : "Minolta #2882 1.10 scanner not found (SCSI type 6 required)");
        errno = matches ? ENOTUNIQ : ENODEV;
        return -1;
    }
    return 0;
}
static inline int f2400_open_scanner(void) {
    char node[64], path[PATH_MAX], value[128];
    if (f2400_find_scanner(F2400_SYS_SG_ROOT, node, sizeof(node)) < 0)
        return -1;
    if (f2400_attribute(F2400_SYS_SG_ROOT, node, "device/state", value, sizeof(value)) < 0 ||
        strcmp(value, "running")) {
        fputs("SCANNER DEVICE: scanner is offline or unavailable\n", stderr);
        errno = ENODEV;
        return -1;
    }
    unsigned maj, min;
    char extra;
    if (f2400_attribute(F2400_SYS_SG_ROOT, node, "dev", value, sizeof(value)) < 0 ||
        sscanf(value, "%u:%u%c", &maj, &min, &extra) != 2) {
        errno = ENODEV;
        return -1;
    }
    if (snprintf(path, sizeof(path), "%s/%s", F2400_DEV_ROOT, node) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        fprintf(stderr, "SCANNER DEVICE: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISCHR(st.st_mode) || st.st_rdev != makedev(maj, min) ||
        !f2400_sysfs_match(F2400_SYS_SG_ROOT, node)) {
        close(fd);
        errno = ENODEV;
        fputs("SCANNER DEVICE: device changed during discovery; stopped\n", stderr);
        return -1;
    }
    fprintf(stderr, "SCANNER DEVICE: %s (Minolta #2882 1.10)\n", path);
    return fd;
}
#endif
