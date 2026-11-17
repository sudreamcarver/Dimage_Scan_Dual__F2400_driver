#include "internal.h"
static const char *film_name(f2400_film film) {
    const char *names[] = {"35mm color positive", "35mm color negative", "35mm bw negative",
                           "35mm color RAW", "35mm BW RAW"};
    return (film >= F2400_POSITIVE && film <= F2400_RAW_GREEN) ? names[film] : "unsupported";
}
static void setstr(json_object *j, const char *key, const char *value) {
    json_object_object_add(j, key, json_object_new_string(value));
}
static int save_report(const char *dir, json_object *report, const char *stage) {
    if (stage) {
        json_object *a = NULL;
        json_object_object_get_ex(report, "transitions", &a);
        json_object_array_add(a, json_object_new_string(stage));
    }
    char path[PATH_MAX];
    if (path_join(path, sizeof(path), dir, "result.json") < 0)
        return -1;
    return write_json(path, report);
}
static int new_directory(const char *dir) {
    if (mkdir(dir, 0750) < 0)
        return fail("new output directory required (%s): %s", dir, strerror(errno));
    return 0;
}
static int identity(int fd) {
    uint8_t cdb[6] = {0x12, 0, 0, 0, 36, 0}, id[36];
    return command(fd, "IDENTITY", cdb, id, 36) == 36 && (id[0] & 31) == 6 &&
                   !memcmp(id + 8, "Minolta #2882           1.10", 28)
               ? 0
               : fail("scanner identity mismatch");
}
static int stage_log(const char *dir, const char *name) {
    if (wire_log) {
        int error = ferror(wire_log);
        if (fclose(wire_log))
            error = 1;
        wire_log = NULL;
        if (error)
            return fail("wire log write failed");
    }
    char path[PATH_MAX];
    if (path_join(path, sizeof(path), dir, name) < 0)
        return -1;
    wire_log = fopen(path, "w");
    if (!wire_log)
        return fail("open wire log: %s", strerror(errno));
    setvbuf(wire_log, NULL, _IOLBF, 0);
    return 0;
}
static int close_log(void) {
    if (!wire_log)
        return 0;
    int bad = ferror(wire_log);
    if (fclose(wire_log))
        bad = 1;
    wire_log = NULL;
    return bad ? fail("wire log write failed") : 0;
}
static int load_capture(const char *prefix, uint8_t **raw, size_t *size, uint16_t dark[7008]) {
    char path[PATH_MAX];
    uint8_t *data = NULL;
    size_t n = 0;
    if (snprintf(path, sizeof(path), "%s-dark.bin", prefix) >= (int)sizeof(path) ||
        read_file(path, &data, &n) < 0)
        return -1;
    if (n != 14016) {
        free(data);
        return fail("dark table length");
    }
    for (unsigned i = 0; i < 7008; ++i)
        dark[i] = (uint16_t)((unsigned)data[2 * i] + (unsigned)data[2 * i + 1] * 256);
    free(data);
    if (snprintf(path, sizeof(path), "%s-raw.bin", prefix) >= (int)sizeof(path))
        return fail("path too long");
    return read_file(path, raw, size);
}
static json_object *ae_json(const f2400_ae *ae) {
    json_object *j = json_object_new_object();
    json_object_object_add(j, "exposures", uint_array(ae->exposures, 3));
    json_object_object_add(j, "curve_levels", uint_array(ae->levels, 3));
    json_object_object_add(j, "dark_means", uint_array(ae->means, 3));
    json_object_object_add(j, "block_sums", uint_array(ae->maxima, 3));
    json_object_object_add(j, "minimum_blocks", uint_array(ae->minima, 3));
    json_object_object_add(j, "single_peaks", uint_array(ae->peaks, 3));
    json_object_object_add(j, "classifier", json_object_new_int(ae->classifier));
    json_object_object_add(j, "classifier_counts", uint_array(ae->counts, 7));
    json_object_object_add(j, "exposure_limited", json_object_new_boolean(ae->limited));
    json_object_object_add(j, "meter_signal", json_object_new_boolean(!ae->no_signal));
    json_object_object_add(j, "converged", json_object_new_boolean(!ae->limited && !ae->no_signal));
    return j;
}
int f2400_scan(const char *dir, const char *profile, f2400_film film, unsigned ratio,
               int save_tiff) {
    if (!dir || !profile || (film < F2400_POSITIVE || film > F2400_RAW_GREEN) ||
        (ratio != 1 && ratio != 4))
        return 2;
    uint8_t shade[7008];
    unsigned base[3], negative[3];
    json_object *meta = NULL;
    if (load_profile(profile, shade, base, negative, &meta) < 0)
        return 2;
    if (new_directory(dir) < 0) {
        json_object_put(meta);
        return 2;
    }
    json_object *report = json_object_new_object(), *transitions = json_object_new_array(),
                *audits = json_object_new_array();
    setstr(report, "status", "running");
    setstr(report, "film", film_name(film));
    json_object_object_add(report, "ratio", json_object_new_int(ratio));
    json_object_object_add(
        report, "raw_mode",
        json_object_new_boolean(film == F2400_RAW_RGB || film == F2400_RAW_GREEN));
    json_object_object_add(report, "raw_channels",
                           json_object_new_int(film == F2400_RAW_GREEN ? 1 : 3));
    json_object_object_add(report, "transitions", transitions);
    json_object_object_add(report, "calibration_profile", meta);
    json_object_object_add(report, "device_ready", NULL);
    int result = 1, fd = -1;
    uint8_t *raw = NULL;
    size_t raw_size = 0;
    uint16_t dark[7008];
    f2400_image *image = NULL;
    char path[PATH_MAX], prefix[PATH_MAX];
    unsigned exposure[3], percentage = 100;
    f2400_ae ae;
    if (path_join(path, sizeof(path), dir, "shading.bin") < 0 ||
        write_file(path, shade, 7008) < 0 || save_report(dir, report, "metering") < 0 ||
        stage_log(dir, "meter.log") < 0)
        goto done;
    fd = open_locked();
    if (fd < 0) {
        result = 2;
        goto done;
    }
    if (stop_requested) {
        result = 130;
        goto done;
    }
    path_join(prefix, sizeof(prefix), dir, "meter");
    if (film == F2400_NEGATIVE)
        memcpy(base, negative, sizeof(base));
    result =
        capture_frame(fd, prefix, base, 8, 1, film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN);
    if (result)
        goto done;
    if (close_log() < 0) {
        result = 1;
        goto done;
    }
    json_object_object_add(report, "device_ready", json_object_new_boolean(1));
    json_object *audit = NULL;
    if (verify_capture(prefix, 8, base, 1, film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN,
                       &audit) < 0) {
        result = 1;
        goto done;
    }
    json_object_array_add(audits, audit);
    if (load_capture(prefix, &raw, &raw_size, dark) < 0 ||
        f2400_meter(raw, raw_size, dark, shade, base, film, &ae) < 0) {
        result = 1;
        goto done;
    }
    free(raw);
    raw = NULL;
    memcpy(exposure, ae.exposures, sizeof(exposure));
    json_object_object_add(report, "meter", ae_json(&ae));
    json_object_object_add(report, "native_gains", uint_array((unsigned[3]){15, 15, 15}, 3));
    if (film == F2400_NEGATIVE) {
        f2400_normalization norm;
        if (f2400_normalize_color_exposure(exposure, &norm) < 0) {
            result = 1;
            goto done;
        }
        json_object *j = json_object_new_object();
        json_object_object_add(j, "exposure", json_object_new_int(norm.exposure));
        json_object_object_add(j, "gains", uint_array(norm.gains, 3));
        json_object_object_add(j, "percentages", uint_array(norm.percentages, 3));
        json_object_object_add(report, "preview_normalization", j);
    }
    if (film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN) {
        unsigned e;
        if (normalize_bw(exposure[1], &e, &percentage) < 0) {
            result = 1;
            goto done;
        }
        for (unsigned c = 0; c < 3; ++c)
            exposure[c] = e;
        json_object *norm = json_object_new_object();
        json_object_object_add(norm, "exposure", json_object_new_int(e));
        json_object_object_add(norm, "percentages",
                               uint_array((unsigned[3]){percentage, percentage, percentage}, 3));
        json_object_object_add(norm, "gains", uint_array((unsigned[3]){14, 14, 14}, 3));
        json_object_object_add(report, "normalization", norm);
        json_object_object_add(report, "native_gains", uint_array((unsigned[3]){14, 14, 14}, 3));
    }
    for (unsigned c = 0; c < 3; ++c)
        if (exposure[c] < 8 ||
            exposure[c] > (film == F2400_NEGATIVE                                 ? 1024u
                           : film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN ? 136u
                                                                                  : 128u)) {
            fail("formal exposure exceeds supported range");
            result = 1;
            goto done;
        }
    if (save_report(dir, report, "meter-complete-release-ready") < 0) {
        result = 1;
        goto done;
    }
    if (stop_requested) {
        result = 130;
        goto done;
    }
    json_object_object_add(report, "device_ready", NULL);
    if (save_report(dir, report, "exposure-selected") < 0 || stage_log(dir, "full.log") < 0 ||
        identity(fd) < 0) {
        result = 1;
        goto done;
    }
    path_join(prefix, sizeof(prefix), dir, "full");
    result = capture_frame(fd, prefix, exposure, ratio, 0,
                           film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN);
    if (result)
        goto done;
    if (close_log() < 0) {
        result = 1;
        goto done;
    }
    json_object_object_add(report, "device_ready", json_object_new_boolean(1));
    if (save_report(dir, report, "scan-complete-release-ready") < 0) {
        result = 1;
        goto done;
    }
    if (stop_requested) {
        result = 130;
        goto done;
    }
    if (verify_capture(prefix, ratio, exposure, 0,
                       film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN, &audit) < 0) {
        result = 1;
        goto done;
    }
    json_object_array_add(audits, audit);
    if (load_capture(prefix, &raw, &raw_size, dark) < 0 ||
        f2400_process(raw, raw_size, dark, shade, ratio, film, ae.levels, percentage, &image) < 0) {
        result = 1;
        goto done;
    }
    json_object_object_add(
        report, "stats",
        image_statistics(raw, dark, ratio, film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN));
    unsigned means[3] = {0};
    uint8_t dark_bytes[14016];
    for (unsigned i = 0; i < 7008; ++i) {
        means[i / 2336] += dark[i];
        dark_bytes[2 * i] = dark[i];
        dark_bytes[2 * i + 1] = dark[i] >> 8;
    }
    for (unsigned c = 0; c < 3; ++c)
        means[c] /= 2336;
    json_object_object_add(report, "dark_means", uint_array(means, 3));
    char sha[65];
    hash_data(dark_bytes, sizeof(dark_bytes), sha);
    setstr(report, "dark_sha256", sha);
    hash_data(raw, raw_size, sha);
    setstr(report, "raw_sha256", sha);
    free(raw);
    raw = NULL;
    if (stop_requested) {
        result = 130;
        goto done;
    }
    const char *stem =
        film == F2400_RAW_RGB || film == F2400_RAW_GREEN ? "image-raw" : "image-factory";
    char filename[64];
    snprintf(filename, sizeof(filename), "%s.png", stem);
    path_join(path, sizeof(path), dir, filename);
    if (f2400_image_save(image, path) < 0) {
        result = 1;
        goto done;
    }
    if (save_tiff) {
        snprintf(filename, sizeof(filename), "%s.tiff", stem);
        path_join(path, sizeof(path), dir, filename);
        if (f2400_image_save(image, path) < 0) {
            result = 1;
            goto done;
        }
    }
    json_object_object_add(report, "width", json_object_new_int(2336 / ratio));
    json_object_object_add(report, "height", json_object_new_int(3504 / ratio));
    json_object_object_add(report, "native_exposures", uint_array(exposure, 3));
    json_object *output = json_object_new_object();
    json_object_object_add(output, "png_size",
                           uint_array((unsigned[2]){image->width, image->height}, 2));
    json_object_object_add(output, "tiff_saved", json_object_new_boolean(save_tiff));
    json_object_object_add(output, "tiff_container_bits", json_object_new_int(16));
    json_object_object_add(
        output, "calibrated_linear",
        json_object_new_boolean(film == F2400_RAW_RGB || film == F2400_RAW_GREEN));
    json_object_object_add(report, "output", output);
    if (save_report(dir, report, "verifying") < 0) {
        result = 1;
        goto done;
    }
    path_join(path, sizeof(path), dir, "wire-audit.json");
    if (write_json(path, audits) < 0) {
        result = 1;
        goto done;
    }
    result = stop_requested ? 130 : 0;
done:
    free(raw);
    f2400_image_free(image);
    if (fd >= 0)
        close(fd);
    if (close_log() < 0)
        result = 1;
    setstr(report, "status", result == 0 ? "complete" : result == 130 ? "cancelled" : "failed");
    if (result)
        setstr(report, "error",
               f2400_error()[0] ? f2400_error()
               : result == 130  ? "cancelled; see mechanical readiness evidence"
                                : "device operation failed; inspect wire log");
    if (save_report(dir, report, result == 0 ? "idle" : NULL) < 0)
        result = 1;
    json_object_put(report);
    json_object_put(audits);
    return result;
}
int f2400_calibrate(const char *dir, int cold_start, int holder_removed) {
    if (!holder_removed || !dir) {
        fail("remove holder and confirm --holder-removed before calibration");
        return 2;
    }
    if (new_directory(dir) < 0)
        return 2;
    int fd = -1, result = 1;
    if (stage_log(dir, cold_start ? "initialize.log" : "capture.log") < 0)
        goto done;
    fd = open_locked();
    if (fd < 0) {
        result = 2;
        goto done;
    }
    if (cold_start) {
        initializing = 1;
        int r = cold_initialize(fd);
        initializing = 0;
        if (r < 0) {
            result = stop_requested ? 130 : 1;
            goto done;
        }
        if (stop_requested) {
            result = 130;
            goto done;
        }
        memcpy(refresh_exposures, calibration_exposures, sizeof(refresh_exposures));
        if (stage_log(dir, "capture.log") < 0 || identity(fd) < 0)
            goto done;
    }
    if (wait_frame_ready(fd, 40, 500000) < 0 || finish_frame(fd) < 0)
        goto done;
    char prefix[PATH_MAX];
    path_join(prefix, sizeof(prefix), dir, "capture");
    int captured = calibration_field(fd, prefix, "dark-sums", 0) == 0 &&
                   calibration_field(fd, prefix, "white-sums", 1) == 0 &&
                   calibration_field(fd, prefix, "correction-sums", 1) == 0;
    if (stop_requested) {
        int ack = cancel_frame(fd);
        fprintf(wire_log, "CALIBRATION cancel_ack_good=%u\n", ack == 0);
    }
    int ready = finish_frame(fd);
    fprintf(wire_log, "CALIBRATION complete=%u release_ready=%u\n", captured, ready == 0);
    if (close_log() < 0 || ready < 0)
        goto done;
    if (stop_requested) {
        result = 130;
        goto done;
    }
    if (!captured || export_profile(dir) < 0)
        goto done;
    uint8_t shade[7008];
    unsigned base[3], negative[3];
    char path[PATH_MAX];
    path_join(path, sizeof(path), dir, "profile.json");
    if (load_profile(path, shade, base, negative, NULL) < 0)
        goto done;
    if (cold_start) {
        json_object *j = json_object_new_object();
        setstr(j, "status", "complete");
        json_object_object_add(j, "final_ready", json_object_new_boolean(1));
        setstr(j, "profile", "profile.json");
        path_join(path, sizeof(path), dir, "initialization.json");
        if (write_json(path, j) < 0) {
            json_object_put(j);
            goto done;
        }
        json_object_put(j);
    }
    result = 0;
done:
    initializing = 0;
    if (fd >= 0)
        close(fd);
    if (close_log() < 0)
        result = 1;
    return result;
}
int f2400_control(int release) {
    int fd = open_locked();
    if (fd < 0)
        return 2;
    int result = release ? finish_frame(fd) : wait_frame_ready(fd, 40, 500000);
    uint8_t mode[6] = {0x1a, 0x10, 0x20, 0, 8, 0}, status[8];
    int page = command(fd, "STATUS", mode, status, 8);
    close(fd);
    return result == 0 && page == 8 ? 0 : 1;
}
