#include "internal.h"
static int number(const char *s, unsigned *value) {
    if (!s || !*s || *s == '-')
        return -1;
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || *end || v > UINT_MAX)
        return -1;
    *value = (unsigned)v;
    return 0;
}
static int triple(const char *s, unsigned v[3]) {
    char tail;
    if (!s || sscanf(s, "%u,%u,%u%c", v, v + 1, v + 2, &tail) != 3)
        return -1;
    return 0;
}
static void usage(void) {
    puts("f2400 status | release\n"
         "f2400 scan --output NEW_DIR --calibration PROFILE --film TYPE [--ratio 1|4] "
         "[--preview-only --editable-preview]\n"
         "f2400 initialize|calibration --output NEW_DIR --holder-removed\n"
         "f2400 profile-info --calibration PROFILE\n"
         "f2400 process|meter --raw FILE --dark FILE --shading FILE --film TYPE [--base R,G,B] "
         "[--levels R,G,B] [--ratio 1|4] [--percentage N] [--output PREFIX]\n"
         "f2400 lut --film TYPE --output FILE [--scale 100 --dark-mean 0 --level 256]\n"
         "f2400 profile-build --output CAPTURE_DIR\n"
         "TYPE: color-positive, color-negative, bw-negative, raw-color, raw-bw");
}
int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "--help")) {
        usage();
        return argc < 2 ? 2 : 0;
    }
    const char *cmd = argv[1], *dir = NULL, *profile = NULL, *raw_path = NULL, *dark_path = NULL,
               *shade_path = NULL;
    f2400_film film = F2400_POSITIVE;
    unsigned ratio = 1, base[3] = {36, 20, 25}, levels[3] = {256, 256, 256}, percentage = 100,
             scale = 100, dark_mean = 0, level = 256;
    int holder = 0, preview = 0, editable = 0, metering = 0;
    for (int i = 2; i < argc; ++i) {
        const char *key = argv[i];
        if (!strcmp(key, "--metering")) {
            metering = 1;
            continue;
        }
        if (!strcmp(key, "--holder-removed")) {
            holder = 1;
            continue;
        }
        if (!strcmp(key, "--preview-only")) {
            preview = 1;
            continue;
        }
        if (!strcmp(key, "--editable-preview")) {
            editable = 1;
            continue;
        }
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        const char *v = argv[++i];
        if (!strcmp(key, "--output"))
            dir = v;
        else if (!strcmp(key, "--calibration"))
            profile = v;
        else if (!strcmp(key, "--raw"))
            raw_path = v;
        else if (!strcmp(key, "--dark"))
            dark_path = v;
        else if (!strcmp(key, "--shading"))
            shade_path = v;
        else if (!strcmp(key, "--film")) {
            if (!strcmp(v, "color-positive"))
                film = F2400_POSITIVE;
            else if (!strcmp(v, "color-negative"))
                film = F2400_NEGATIVE;
            else if (!strcmp(v, "bw-negative"))
                film = F2400_BW_NEGATIVE;
            else if (!strcmp(v, "raw-color") || !strcmp(v, "raw"))
                film = F2400_RAW_RGB;
            else if (!strcmp(v, "raw-bw"))
                film = F2400_RAW_GREEN;
            else {
                fprintf(stderr, "unsupported film\n");
                return 2;
            }
        } else if (!strcmp(key, "--ratio")) {
            if (number(v, &ratio) < 0 || (ratio != 1 && ratio != 4))
                return 2;
        } else if (!strcmp(key, "--base")) {
            if (triple(v, base) < 0)
                return 2;
        } else if (!strcmp(key, "--levels")) {
            if (triple(v, levels) < 0)
                return 2;
        } else if (!strcmp(key, "--percentage")) {
            if (number(v, &percentage) < 0)
                return 2;
        } else if (!strcmp(key, "--scale")) {
            if (number(v, &scale) < 0)
                return 2;
        } else if (!strcmp(key, "--dark-mean")) {
            if (number(v, &dark_mean) < 0)
                return 2;
        } else if (!strcmp(key, "--level")) {
            if (number(v, &level) < 0)
                return 2;
        } else {
            fprintf(stderr, "unknown option %s\n", key);
            return 2;
        }
    }
    f2400_install_signals();
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (!strcmp(cmd, "status") || !strcmp(cmd, "release"))
        return f2400_control(!strcmp(cmd, "release"));
    if (!strcmp(cmd, "scan")) {
        if (!dir || !profile)
            return 2;
        return f2400_scan(dir, profile, film, ratio, !preview || editable);
    }
    if (!strcmp(cmd, "initialize") || !strcmp(cmd, "calibration"))
        return f2400_calibrate(dir, !strcmp(cmd, "initialize"), holder);
    if (!strcmp(cmd, "profile-info")) {
        if (!profile)
            return 2;
        char json[65536];
        if (f2400_profile_info(profile, json, sizeof(json)) < 0)
            return 1;
        puts(json);
        return 0;
    }
    if (!strcmp(cmd, "audit")) {
        if (!dir)
            return 2;
        json_object *j = NULL;
        if (verify_capture(dir, metering ? 8 : ratio, base, metering,
                           film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN, &j) < 0)
            return 1;
        puts(json_object_to_json_string_ext(j, JSON_C_TO_STRING_PRETTY));
        json_object_put(j);
        return 0;
    }
    if (!strcmp(cmd, "profile-build"))
        return dir && export_profile(dir) == 0 ? 0 : 1;
    if (!strcmp(cmd, "lut")) {
        if (!dir)
            return 2;
        uint16_t lut[4096];
        uint8_t bytes[8192];
        if (f2400_lut(film, scale, dark_mean, level, lut) < 0)
            return 1;
        for (unsigned i = 0; i < 4096; ++i) {
            bytes[2 * i] = lut[i];
            bytes[2 * i + 1] = lut[i] >> 8;
        }
        return write_file(dir, bytes, sizeof(bytes)) < 0 ? 1 : 0;
    }
    if (strcmp(cmd, "process") && strcmp(cmd, "meter")) {
        usage();
        return 2;
    }
    if (!raw_path || !dark_path || !shade_path || (!strcmp(cmd, "process") && !dir))
        return 2;
    uint8_t *raw = NULL, *dark_bytes = NULL, *shade = NULL;
    size_t n = 0, dn = 0, sn = 0;
    int result = 1;
    f2400_image *im = NULL;
    if (read_file(raw_path, &raw, &n) < 0 || read_file(dark_path, &dark_bytes, &dn) < 0 ||
        read_file(shade_path, &shade, &sn) < 0)
        goto done;
    if (dn != 14016 || sn != 7008) {
        fail("calibration file sizes");
        goto done;
    }
    uint16_t dark[7008];
    for (unsigned i = 0; i < 7008; ++i)
        dark[i] = (uint16_t)((unsigned)dark_bytes[2 * i] + (unsigned)dark_bytes[2 * i + 1] * 256);
    if (!strcmp(cmd, "meter")) {
        f2400_ae ae;
        if (f2400_meter(raw, n, dark, shade, base, film, &ae) < 0)
            goto done;
        json_object *j = json_object_new_object();
        json_object_object_add(j, "exposures", uint_array(ae.exposures, 3));
        json_object_object_add(j, "curve_levels", uint_array(ae.levels, 3));
        json_object_object_add(j, "classifier", json_object_new_int(ae.classifier));
        json_object_object_add(j, "classifier_counts", uint_array(ae.counts, 7));
        json_object_object_add(j, "maximum_blocks", uint_array(ae.maxima, 3));
        json_object_object_add(j, "minimum_blocks", uint_array(ae.minima, 3));
        json_object_object_add(j, "single_peaks", uint_array(ae.peaks, 3));
        json_object_object_add(j, "dark_means", uint_array(ae.means, 3));
        json_object_object_add(j, "exposure_limited", json_object_new_boolean(ae.limited));
        puts(json_object_to_json_string_ext(j, JSON_C_TO_STRING_PRETTY));
        json_object_put(j);
        result = 0;
        goto done;
    }
    if (f2400_process(raw, n, dark, shade, ratio, film, levels, percentage, &im) < 0)
        goto done;
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s.png", dir) >= (int)sizeof(path) ||
        f2400_image_save(im, path) < 0)
        goto done;
    if (!preview || editable) {
        if (snprintf(path, sizeof(path), "%s.tiff", dir) >= (int)sizeof(path) ||
            f2400_image_save(im, path) < 0)
            goto done;
    }
    char sha[65];
    hash_data(im->pixels, (size_t)im->width * im->height * im->channels * 2, sha);
    printf("{\"width\":%u,\"height\":%u,\"channels\":%u,\"rgb16_sha256\":\"%s\"}\n", im->width,
           im->height, im->channels, sha);
    result = 0;
done:
    free(raw);
    free(dark_bytes);
    free(shade);
    f2400_image_free(im);
    return result;
}
