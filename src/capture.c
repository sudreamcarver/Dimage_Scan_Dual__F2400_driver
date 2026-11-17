#include "internal.h"

int wait_frame_ready(int fd, unsigned attempts, unsigned delay_us) {
    unsigned char tur[6] = {0};
    for (unsigned i = 0; i < attempts; ++i) {
        if (command(fd, "FRAME WAIT READY", tur, NULL, 0) == 0)
            return 0;
        if (!mechanism_busy && !target_busy)
            return -1;
        if (i + 1 < attempts)
            usleep(delay_us);
    }
    return -1;
}

int cancel_frame(int fd) {
    /* Original 10013e57 -> 10019904, cancellation parameter ff. */
    unsigned char cdb[6] = {0x15, 0x10, 0, 0, 8, 0};
    unsigned char data[8] = {0, 0, 6, 0, 0x20, 2, 0xff, 0};
    return transfer(fd, "FRAME CANCEL", cdb, 6, data, 8, SG_DXFER_TO_DEV, 5000) == 8 ? 0 : -1;
}

int finish_frame(int fd) {
    if (wait_frame_ready(fd, 40, 500000) < 0)
        return -1;
    unsigned char cdb[10] = {0x31, 2, 0, 0, 0, 0, 0, 0, 0, 0};
    if (transfer(fd, "FRAME FINISH RELEASE", cdb, 10, NULL, 0, SG_DXFER_NONE, 5000) != 0)
        return -1;
    return wait_frame_ready(fd, 40, 500000);
}

int prepare35_samples(int fd, uint16_t dark[7008], const unsigned exposure[3], unsigned samples) {
    if (samples != 16 && samples != 64)
        return -1;
    for (unsigned c = 0; c < 3; ++c)
        if (exposure[c] < 8 || exposure[c] > 1024)
            return -1;
    /* Actual 77-command preparation replay matches original 1000806d +
     * 1000555f. Parameters belong to this scanner's recorded initialization. */
    unsigned char control[10] = {0x31, 2, 0, 0, 0, 0, 0, 0, 0, 0};
    if (transfer(fd, "PREPARE35 CONTROL", control, 10, NULL, 0, SG_DXFER_NONE, 5000) != 0 ||
        wait_frame_ready(fd, 40, 500000) < 0)
        return -1;
    unsigned char window[72] = {
        0,    0,    0,    0,    0, 0, 0, 0x40, 0, 0, 9, 0x60, 9, 0x60, 0,    0,    0,    0xf5,
        0xff, 0xff, 0xfd, 0x18, 0, 0, 9, 0x20, 0, 0, 0, 1,    0, 0,    0x80, 0xff, 0x10, 0,
        0,    0,    0,    0,    0, 0, 0, 0,    0, 0, 0, 0,    1, 1,    0,    14,   14,   14,
        20,   17,   63,   0,    0, 0, 3, 0xaa, 0, 0, 0, 0,    0, 36,   0,    20,   0,    25};
    unsigned char set[10] = {0x24, 0, 0, 0, 0, 0, 0, 0, 72, 0};
    for (unsigned c = 0; c < 3; ++c) {
        window[66 + c * 2] = exposure[c] >> 8;
        window[67 + c * 2] = exposure[c];
    }
    if (transfer(fd, "PREPARE35 DARK WINDOW", set, 10, window, 72, SG_DXFER_TO_DEV, 60000) != 72)
        return -1;
    uint32_t sums[7008] = {0};
    unsigned char data[14016];
    for (unsigned n = 0; n < samples; ++n) {
        unsigned char cdb[10] = {0x28, 0, 0, 0, 0, 0, 0, 0x36, 0xc0, 0};
        if (transfer(fd, "PREPARE35 DARK READ", cdb, 10, data, 14016, SG_DXFER_FROM_DEV, 60000) !=
            14016)
            return -1;
        for (unsigned i = 0; i < 7008; ++i) {
            if (data[i * 2] > 3)
                return -1;
            sums[i] += ((unsigned)data[i * 2] << 8) | data[i * 2 + 1];
        }
    }
    for (unsigned i = 0; i < 7008; ++i)
        dark[i] = sums[i] / samples;
    /* Original narrow film/light check: x1345, width100, y0, modefc. */
    window[16] = 5;
    window[17] = 0x41;
    memset(window + 18, 0, 4);
    window[24] = 0;
    window[25] = 100;
    window[33] = 0xfc;
    window[51] = window[52] = window[53] = 15;
    for (unsigned c = 0; c < 3; ++c)
        window[66 + c * 2] = 0;
    window[67] = 36;
    window[69] = 19;
    window[71] = 20;
    if (transfer(fd, "PREPARE35 NARROW WINDOW", set, 10, window, 72, SG_DXFER_TO_DEV, 60000) != 72)
        return -1;
    unsigned total = 0;
    for (unsigned n = 0; n < 10; ++n) {
        unsigned char cdb[10] = {0x28, 0, 0, 0, 0, 0, 0, 0, 200, 0};
        if (transfer(fd, "PREPARE35 NARROW READ", cdb, 10, data, 200, SG_DXFER_FROM_DEV, 60000) !=
            200)
            return -1;
        for (unsigned i = 0; i < 100; ++i) {
            if (data[i * 2] > 3)
                return -1;
            total += ((unsigned)data[i * 2] << 8) | data[i * 2 + 1];
        }
    }
    fprintf(wire_log ? wire_log : stdout, "PREPARE35 narrow_sum=%u original_upper_limit=760000\n",
            total);
    return total <= 760000 ? 0 : -1;
}

int save_bytes(const char *prefix, const char *suffix, const void *data, size_t length) {
    char path[512];
    if (snprintf(path, sizeof(path), "%s-%s.bin", prefix, suffix) >= (int)sizeof(path))
        return -1;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int good = fwrite(data, 1, length, f) == length;
    int closed = fclose(f);
    return good && !closed ? 0 : -1;
}

int read_factory_lines(int fd, unsigned char *data, unsigned capacity, unsigned *completed,
                       int meter, unsigned ratio, unsigned cancel_after) {
    *completed = 0;
    if ((meter && ratio != 8) || (!meter && ratio != 1 && ratio != 4))
        return -1;
    unsigned w = 2336 / ratio, h = 3504 / ratio, stagger = 16 / ratio;
    unsigned expected = meter ? w * (h + 2) * 6 : w * h * 6;
    if (capacity < expected)
        return -1;
    for (unsigned row = 0; row < h + stagger; ++row) {
        if (stop_requested || (cancel_after && row == cancel_after))
            return -2;
        unsigned mask = meter                   ? 7
                        : row < stagger / 2     ? 4
                        : row < stagger         ? 6
                        : row < h               ? 7
                        : row < h + stagger / 2 ? 3
                                                : 1;
        unsigned channels = (!!(mask & 4)) + (!!(mask & 2)) + (!!(mask & 1));
        unsigned length = w * channels * 2;
        unsigned char cdb[10] = {0x28,         0, 0, 0, 0xf8 | mask, ratio, 0, length >> 8,
                                 length & 255, 0};
        if (transfer(fd, "FACTORY FRAME READ", cdb, 10, data + *completed, length,
                     SG_DXFER_FROM_DEV, 60000) != (int)length)
            return -1;
        *completed += length;
        fprintf(wire_log ? wire_log : stdout, "FACTORY LINE row=%u bytes=%u\n", row + 1,
                *completed);
    }
    return 0;
}

int read_factory_gray(int fd, unsigned char *data, unsigned capacity, unsigned *completed,
                      unsigned ratio, unsigned cancel_after) {
    *completed = 0;
    if (ratio != 1 && ratio != 4 && ratio != 8)
        return -1;
    unsigned width = 2336 / ratio, rows = 3504 / ratio, length = width * 2;
    if (capacity < rows * length)
        return -1;
    for (unsigned row = 0; row < rows; ++row) {
        if (stop_requested || (cancel_after && row == cancel_after))
            return -2;
        unsigned char cdb[10] = {0x28, 0, 0, 0, 0xfa, ratio, 0, length >> 8, length & 255, 0};
        if (transfer(fd, "FACTORY GRAY READ", cdb, 10, data + *completed, length, SG_DXFER_FROM_DEV,
                     60000) != (int)length)
            return -1;
        *completed += length;
        fprintf(wire_log ? wire_log : stdout, "FACTORY LINE row=%u bytes=%u\n", row + 1,
                *completed);
    }
    return 0;
}

int calibration_field(int fd, const char *prefix, const char *name, int white) {
    unsigned char window[72] = {
        0,  0,  0,  0, 0, 0, 0, 0x40, 0, 0, 9, 0x60, 9, 0x60, 0,    0,    0,    0xf5,
        0,  0,  0,  0, 0, 0, 9, 0x20, 0, 0, 0, 1,    0, 0,    0x80, 0xff, 0x10, 0,
        0,  0,  0,  0, 0, 0, 0, 0,    0, 0, 0, 0,    1, 1,    0,    15,   15,   15,
        20, 17, 63, 0, 0, 0, 3, 0xaa, 0, 0, 0, 0,    0, 22,   0,    14,   0,    16};
    for (unsigned c = 0; c < 3; ++c)
        window[67 + 2 * c] = refresh_exposures[c];
    if (!white) {
        window[18] = window[19] = 0xff;
        window[20] = 0xfd;
        window[21] = 0x18;
        window[51] = window[52] = window[53] = 14;
    }
    unsigned char set[10] = {0x24, 0, 0, 0, 0, 0, 0, 0, 72, 0};
    if (stop_requested)
        return -2;
    if (transfer(fd, "CALIBRATION WINDOW", set, 10, window, 72, SG_DXFER_TO_DEV, 60000) != 72)
        return -1;
    uint32_t sums[7008] = {0};
    unsigned char data[14016];
    for (unsigned n = 0; n < 64; ++n) {
        if (stop_requested)
            return -2;
        unsigned char cdb[10] = {0x28, 0, 0, 0, white ? 1 : 0, 0, 0, 0x36, 0xc0, 0};
        if (transfer(fd, "CALIBRATION READ", cdb, 10, data, sizeof(data), SG_DXFER_FROM_DEV,
                     60000) != (int)sizeof(data))
            return -1;
        for (unsigned i = 0; i < 7008; ++i) {
            if (data[i * 2] > 3)
                return -1;
            sums[i] += ((unsigned)data[i * 2] << 8) | data[i * 2 + 1];
        }
        fprintf(wire_log ? wire_log : stdout, "CALIBRATION field=%s sample=%u\n", name, n + 1);
    }
    unsigned char encoded[28032];
    for (unsigned i = 0; i < 7008; ++i)
        for (unsigned b = 0; b < 4; ++b)
            encoded[i * 4 + b] = sums[i] >> (b * 8);
    return save_bytes(prefix, name, encoded, sizeof(encoded));
}

int capture_frame(int fd, const char *prefix, const unsigned exposures[3], unsigned ratio,
                  int meter, int gray) {
    if ((meter && ratio != 8) || (!meter && ratio != 1 && ratio != 4))
        return fail("invalid capture ratio");
    for (unsigned c = 0; c < 3; ++c)
        if (exposures[c] < 8 || exposures[c] > 1024)
            return fail("invalid capture exposure");
    if (gray && !meter && (exposures[0] != exposures[1] || exposures[1] != exposures[2]))
        return fail("gray exposure mismatch");
    uint16_t dark[7008];
    uint8_t encoded[14016];
    if (wait_frame_ready(fd, 40, 500000) < 0)
        return 1;
    int result = 1, cancelled = 0;
    unsigned completed = 0;
    uint8_t *data = NULL;
    if (stop_requested)
        return 130;
    if (prepare35_samples(fd, dark, exposures, meter ? 16 : 64) < 0)
        goto cleanup;
    for (unsigned i = 0; i < 7008; ++i) {
        encoded[2 * i] = dark[i];
        encoded[2 * i + 1] = dark[i] >> 8;
    }
    if (save_bytes(prefix, "dark", encoded, sizeof(encoded)) < 0)
        goto cleanup;
    uint8_t window[72] = {0,  0,  0,  0,  0, 0, 0, 64,  0, 0, 1,  44, 9, 96, 0,   0,   0,  245,
                          0,  0,  0,  0,  0, 0, 9, 32,  0, 0, 0,  1,  0, 0,  128, 255, 16, 0,
                          0,  0,  0,  0,  0, 0, 0, 0,   0, 0, 0,  0,  8, 8,  0,   15,  15, 15,
                          20, 17, 63, 25, 0, 0, 3, 170, 0, 0, 12, 53, 0, 36, 0,   20,  0,  25};
    if (gray)
        window[33] = 250;
    if (gray && !meter)
        window[51] = window[52] = window[53] = 14;
    if (!meter) {
        unsigned dpi = 2438 / ratio;
        window[10] = window[12] = dpi >> 8;
        window[11] = window[13] = dpi;
        window[48] = window[49] = ratio;
        window[57] = ratio == 1 ? 34 : 29;
        window[60] = ratio == 1 ? 6 : 4;
        window[61] = ratio == 1 ? 27 : 31;
    }
    for (unsigned c = 0; c < 3; ++c) {
        window[66 + 2 * c] = exposures[c] >> 8;
        window[67 + 2 * c] = exposures[c];
    }
    uint8_t set[10] = {0x24, 0, 0, 0, 0, 0, 0, 0, 72, 0};
    if (stop_requested)
        goto cancel;
    if (transfer(fd, "FACTORY FRAME WINDOW", set, 10, window, 72, SG_DXFER_TO_DEV, 60000) != 72)
        goto cleanup;
    unsigned w = 2336 / ratio, h = 3504 / ratio,
             capacity = gray    ? w * h * 2
                        : meter ? w * (h + 2) * 6
                                : w * h * 6;
    data = calloc(capacity, 1);
    if (!data)
        goto cleanup;
    int captured = gray ? read_factory_gray(fd, data, capacity, &completed, ratio, 0)
                        : read_factory_lines(fd, data, capacity, &completed, meter, ratio, 0);
    if (captured == -2)
        goto cancel;
    if (captured < 0) {
        if (stop_requested)
            goto cancel;
        goto cleanup;
    }
    result = 0;
    goto cleanup;
cancel:
    cancelled = 1;
    {
        int ack = cancel_frame(fd);
        fprintf(wire_log, "CANCEL acknowledgement_good=%u mechanism_busy=%u ambiguous=%u\n",
                ack == 0, mechanism_busy, ack != 0);
    }
cleanup:
    if (stop_requested && !cancelled) {
        cancelled = 1;
        int ack = cancel_frame(fd);
        fprintf(wire_log, "CANCEL acknowledgement_good=%u\n", ack == 0);
    }
    if (data && completed &&
        save_bytes(prefix, result == 0 ? "raw" : "partial", data, completed) < 0)
        result = 1;
    free(data);
    int ready = finish_frame(fd);
    fprintf(wire_log, "MECHANICAL release_ready=%u cancelled=%u image_complete=%u\n", ready == 0,
            cancelled, result == 0);
    uint8_t mode[6] = {0x1a, 0x10, 0x20, 0, 8, 0}, state[8];
    int status = command(fd, "FINAL STATUS", mode, state, 8);
    if (ready < 0 || status != 8)
        return 1;
    return cancelled ? 130 : result;
}
