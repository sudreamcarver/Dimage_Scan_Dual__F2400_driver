#include "internal.h"

void initialize_window(unsigned char window[72], unsigned channel) {
    /* Captured from DS_Dual.dll 100052d3 -> 10018c36, original defaults.
     * This is an initialization request, not an arbitrary image scan window.
     */
    static const unsigned char first[72] = {
        0,    0,    0,    0, 0, 0, 0, 0x40, 0, 0, 9, 0x60, 9, 0x60, 0,    0,    0,    0xf5,
        0,    0,    0,    0, 0, 0, 0, 0x64, 0, 0, 0, 1,    0, 0,    0x80, 0xfa, 0x10, 0,
        0,    0,    0,    0, 0, 0, 0, 0,    0, 0, 0, 0,    1, 1,    0,    15,   15,   15,
        0x12, 0x10, 0x3f, 0, 0, 0, 3, 0xaa, 0, 0, 0, 0,    0, 0x19, 0,    0x13, 0,    0x14};
    memcpy(window, first, 72);
    unsigned x = 245 + 1000 * channel;
    window[16] = x >> 8;
    window[17] = x & 255;
}

uint32_t sample_sum(const unsigned char samples[200]) {
    uint32_t sum = 0;
    for (unsigned i = 0; i < 200; i += 2) {
        int value = (int16_t)(((unsigned)samples[i] << 8) | samples[i + 1]);
        if (value > 0)
            sum += (unsigned)value;
    }
    return sum;
}

int light_check(int fd) {
    /* Original 100052d3h, stopping BEFORE the subsequent calibration function.
     * Only known setup responses may provide provisional diagnostic samples.
     */
    for (unsigned position = 0; position < 3; ++position) {
        unsigned char window[72];
        initialize_window(window, position);
        unsigned char set[10] = {0x24, 0, 0, 0, 0, 0, 0, 0, 0x48, 0};
        fprintf(wire_log ? wire_log : stdout, "LIGHT CHECK position=%u x=%u\n", position + 1,
                245 + 1000 * position);
        int result =
            transfer(fd, "LIGHT CHECK SET WINDOW", set, 10, window, 72, SG_DXFER_TO_DEV, 60000);
        if (result != 72 && !(initialize_condition && last_transfer_size == 72))
            return -1;
        uint32_t total = 0;
        unsigned provisional = 0;
        for (unsigned iteration = 0; iteration < 10; ++iteration) {
            unsigned char samples[200] = {0};
            unsigned char get[10] = {0x28, 0, 0, 0, 0, 0, 0, 0, 0xc8, 0};
            result =
                transfer(fd, "LIGHT CHECK READ", get, 10, samples, 200, SG_DXFER_FROM_DEV, 60000);
            if (result != 200) {
                if (!(initialize_condition && last_transfer_size == 200))
                    return -1;
                ++provisional;
            }
            uint32_t sum = sample_sum(samples);
            total += sum;
            fprintf(wire_log ? wire_log : stdout,
                    "LIGHT SAMPLE position=%u iteration=%u sum=%u provisional=%u\n", position + 1,
                    iteration + 1, sum, result != 200);
        }
        fprintf(wire_log ? wire_log : stdout,
                "LIGHT RESULT position=%u total=%u original_threshold=300000 comparison=%s "
                "provisional_reads=%u\n",
                position + 1, total, total >= 300000 ? "PASS" : "FAIL", provisional);
        if (total < 300000)
            return -1;
    }
    stage_complete = 1;
    puts("LIGHT CHECK STOP: completed only the original sample-sum stage;\n"
         "full calibration has not run; CHECK CONDITION data remains provisional.");
    unsigned char ready[6] = {0};
    return command(fd, "TEST UNIT READY AFTER LIGHT CHECK", ready, NULL, 0);
}

int calibration_wide(int fd) {
    /* 10005e78h first FC window captured after exact recorded-response replay.
     * Parameters 14/11 are the replay-verified result of this device session.
     */
    unsigned char window[72];
    initialize_window(window, 0);
    window[18] = 0xff;
    window[19] = 0xff;
    window[20] = 0xfd;
    window[21] = 0x18;
    window[24] = 9;
    window[25] = 0x20;
    window[33] = calibration_color ? 0xff : 0xfc;
    window[51] = window[52] = window[53] = 0x1f;
    window[54] = 20;
    window[55] = 17;
    if (calibration_exposure) {
        memset(window + 18, 0, 4);
        window[51] = window[52] = window[53] = 15;
        window[66] = window[68] = window[70] = 0;
        window[67] = window[69] = window[71] = 12;
    }
    if (calibration_stability) {
        window[16] = calibration_stability_x >> 8;
        window[17] = calibration_stability_x & 255;
        window[24] = 0;
        window[25] = 32;
        window[67] = window[69] = window[71] = 13;
    }
    if (calibration_exposure_final)
        window[67] = window[69] = window[71] = 13;
    if (calibration_shading) {
        window[18] = 0xff;
        window[19] = 0xff;
        window[20] = 0xfd;
        window[21] = 0x18;
        window[51] = window[52] = window[53] = 14;
        window[67] = calibration_exposures[0];
        window[69] = calibration_exposures[1];
        window[71] = calibration_exposures[2];
    }
    unsigned round = 0, history_count = 0;
    uint32_t history[9] = {0};
    uint32_t exposure_sums[7008] = {0};
    int16_t block_levels[3][73] = {{0}};
next_stability_window:
    if (calibration_stability)
        usleep(500000);
    unsigned char set[10] = {0x24, 0, 0, 0, 0, 0, 0, 0, 0x48, 0};
    int result = transfer(fd, "WIDE SET WINDOW", set, 10, window, 72, SG_DXFER_TO_DEV, 60000);
    if (result != 72 && !(initialize_condition && last_transfer_size == 72))
        return -1;
    uint32_t sums[7008] = {0};
    if (calibration_exposure_final)
        memcpy(sums, exposure_sums, sizeof(sums));
    unsigned count = calibration_stability ? 96 : (calibration_color ? 7008 : 2336);
    unsigned iterations = calibration_stability ? 128 : 64;
    for (unsigned iteration = 0; iteration < iterations; ++iteration) {
        unsigned char samples[14016] = {0};
        unsigned char get[10] = {0x28, 0, 0, 0, 0, 0, 0, 0x12, 0x40, 0};
        if (calibration_color) {
            get[7] = 0x36;
            get[8] = 0xc0;
        }
        if (calibration_stability) {
            get[7] = 0;
            get[8] = 0xc0;
        }
        result = transfer(fd, "WIDE READ", get, 10, samples, 2 * count, SG_DXFER_FROM_DEV, 60000);
        if (result != (int)(2 * count) &&
            !(initialize_condition && last_transfer_size == (int)(2 * count)))
            return -1;
        for (unsigned i = 0; i < count; ++i) {
            if (samples[2 * i] > 3) {
                fprintf(stderr, "WIDE invalid 10-bit sample index=%u\n", i);
                return -1;
            }
            sums[i] += ((unsigned)samples[2 * i] << 8) | samples[2 * i + 1];
        }
        if (calibration_exposure && !calibration_stability && !calibration_exposure_final &&
            !calibration_shading) {
            for (unsigned b = 0; b < 73; ++b)
                for (unsigned x = 0; x < 32; ++x)
                    for (unsigned c = 0; c < 3; ++c) {
                        unsigned i = c * 2336 + b * 32 + x;
                        block_levels[c][b] =
                            (int16_t)(block_levels[c][b] +
                                      (((unsigned)samples[2 * i] << 8) | samples[2 * i + 1])) /
                            2;
                    }
        }
        if (initialization_stage)
            fprintf(wire_log ? wire_log : stdout, "INITIALIZE stage=%s sample=%u\n",
                    initialization_stage, iteration + 1);
        fprintf(wire_log ? wire_log : stdout, "WIDE SAMPLE iteration=%u provisional=%u\n",
                iteration + 1, result != (int)(2 * count));
    }
    unsigned low = UINT32_MAX, high = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (sums[i] < low)
            low = sums[i];
        if (sums[i] > high)
            high = sums[i];
    }
    fprintf(wire_log ? wire_log : stdout,
            "WIDE RESULT mode=%02x pixel_sum_min=%u pixel_sum_max=%u; bounded stage complete\n",
            window[33], low, high);
    if (calibration_exposure && !calibration_stability && !calibration_exposure_final &&
        !calibration_shading) {
        int best = 0;
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned b = 0; b < 73; ++b)
                if (block_levels[c][b] > best) {
                    best = block_levels[c][b];
                    peak_channel = c;
                    peak_block = b;
                }
        if (!best)
            return -1;
        fprintf(wire_log ? wire_log : stdout, "INITIALIZE peak_channel=%u peak_block=%u peak=%d\n",
                peak_channel, peak_block, best);
    }
    if (calibration_exposure_final) {
        uint32_t maxima[3] = {0};
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned i = 0; i < 2336; ++i)
                if (sums[c * 2336 + i] > maxima[c])
                    maxima[c] = sums[c * 2336 + i];
        uint32_t peak = maxima[0];
        if (maxima[1] > peak)
            peak = maxima[1];
        if (maxima[2] > peak)
            peak = maxima[2];
        fprintf(wire_log ? wire_log : stdout,
                "EXPOSURE RESULT round=%u setting=%u maxima=%u,%u,%u peak=%u\n", ++round,
                window[67], maxima[0], maxima[1], maxima[2], peak);
        if (peak < 0xf280) {
            if (round >= 40 || window[67] >= 254) {
                fputs("Exposure bound reached\n", stderr);
                return -1;
            }
            ++window[67];
            ++window[69];
            ++window[71];
            memcpy(exposure_sums, sums, sizeof(sums));
            goto next_stability_window;
        }
        if (!maxima[0] || !maxima[1] || !maxima[2])
            return -1;
        for (unsigned c = 0; c < 3; ++c) {
            calibration_exposures[c] = (unsigned)((double)peak / maxima[c] * window[67 + 2 * c]);
            if (calibration_exposures[c] < 1 || calibration_exposures[c] > 128)
                return -1;
        }
        fprintf(wire_log ? wire_log : stdout,
                "EXPOSURE CANDIDATE values=%u,%u,%u provisional=1; stopped before subsequent "
                "calibration\n",
                (unsigned)((double)peak / maxima[0] * window[67]),
                (unsigned)((double)peak / maxima[1] * window[69]),
                (unsigned)((double)peak / maxima[2] * window[71]));
    }
    if (calibration_stability_loop) {
        uint32_t total = 0;
        /* Peak channel=1 recovered at original 10006b39 from recorded wide samples. */
        for (unsigned i = 32 * calibration_stability_channel;
             i < 32 * (calibration_stability_channel + 1); ++i)
            total += sums[i];
        fprintf(wire_log ? wire_log : stdout,
                "STABILITY RESULT round=%u exposure=%u channel=1 total=%u\n", ++round, window[67],
                total);
        if (total > 0x398000) {
            if (window[67] == 0) {
                fputs("Exposure would underflow; stopped\n", stderr);
                return -1;
            }
            --window[67];
            --window[69];
            --window[71];
            history_count = 0;
        } else if (history_count == 0) {
            history[0] = total;
            history_count = 1;
        } else {
            uint32_t previous = history[history_count - 1];
            uint32_t delta = total > previous ? total - previous : previous - total;
            history[history_count] = total;
            if (delta >= 0x4000)
                history_count = 0;
            else if (history_count < 8)
                ++history_count;
            else {
                uint32_t drift = total > history[0] ? total - history[0] : history[0] - total;
                if (drift < 0x8000) {
                    puts("STABILITY STOP: original consecutive-round criterion met; stopping "
                         "before full-width exposure adjustment.");
                    goto stability_done;
                }
            }
        }
        if (round >= 40) {
            fputs("Stability round limit reached\n", stderr);
            return -1;
        }
        goto next_stability_window;
    }
stability_done:
    stage_complete = 1;
    unsigned char ready[6] = {0};
    return command(fd, "TEST UNIT READY AFTER WIDE", ready, NULL, 0);
}

int calibration_baseline(int fd) {
    /* Original 100057d2h, only the initial FC baseline (64 x 100).
     * Stop before the adaptive FB loop. Decoder 1000bf0e accepts 10-bit data.
     */
    unsigned char window[72];
    initialize_window(window, 0);
    window[16] = 0x05;
    window[17] = 0x53;
    window[18] = 0xff;
    window[19] = 0xff;
    window[20] = 0xfd;
    window[21] = 0x18;
    window[33] = 0xfc;
    window[51] = window[52] = window[53] = 0x1f;
    window[54] = window[55] = 0x10;
    unsigned char set[10] = {0x24, 0, 0, 0, 0, 0, 0, 0, 0x48, 0};
    fputs("CALIBRATION BASELINE payload:"
          "\n",
          wire_log ? wire_log : stdout);
    for (unsigned i = 0; i < 72; ++i)
        fprintf(wire_log ? wire_log : stdout, "%02x%s", window[i], i == 71 ? "\n" : " ");
    int result = transfer(fd, "BASELINE SET WINDOW", set, 10, window, 72, SG_DXFER_TO_DEV, 60000);
    if (result != 72 && !(initialize_condition && last_transfer_size == 72))
        return -1;
    uint32_t total = 0;
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        unsigned char samples[200] = {0};
        unsigned char get[10] = {0x28, 0, 0, 0, 0, 0, 0, 0, 0xc8, 0};
        result = transfer(fd, "BASELINE READ", get, 10, samples, 200, SG_DXFER_FROM_DEV, 60000);
        if (result != 200 && !(initialize_condition && last_transfer_size == 200))
            return -1;
        for (unsigned i = 0; i < 200; i += 2) {
            if (samples[i] > 3) {
                fprintf(stderr, "BASELINE invalid 10-bit sample iteration=%u index=%u\n",
                        iteration + 1, i / 2);
                return -1;
            }
        }
        uint32_t sum = sample_sum(samples);
        total += sum;
        fprintf(wire_log ? wire_log : stdout,
                "BASELINE SAMPLE iteration=%u sum=%u provisional=%u\n", iteration + 1, sum,
                result != 200);
    }
    fprintf(wire_log ? wire_log : stdout, "BASELINE RESULT total=%u\n", total);
    if (calibration_adaptive) {
        unsigned a = 16, b = 16;
        for (unsigned round = 0; round < 64; ++round) {
            window[33] = 0xfb;
            window[54] = b;
            window[55] = a;
            result =
                transfer(fd, "ADAPTIVE SET WINDOW", set, 10, window, 72, SG_DXFER_TO_DEV, 60000);
            if (result != 72 && !(initialize_condition && last_transfer_size == 72))
                return -1;
            uint32_t sums[2] = {0, 0};
            for (unsigned iteration = 0; iteration < 64; ++iteration) {
                unsigned char samples[400] = {0};
                unsigned char get[10] = {0x28, 0, 0, 0, 0, 0, 0, 1, 0x90, 0};
                result =
                    transfer(fd, "ADAPTIVE READ", get, 10, samples, 400, SG_DXFER_FROM_DEV, 60000);
                if (result != 400 && !(initialize_condition && last_transfer_size == 400))
                    return -1;
                for (unsigned i = 0; i < 400; i += 2) {
                    if (samples[i] > 3) {
                        fprintf(stderr,
                                "ADAPTIVE invalid 10-bit sample round=%u iteration=%u index=%u\n",
                                round + 1, iteration + 1, i / 2);
                        return -1;
                    }
                }
                sums[0] += sample_sum(samples);
                sums[1] += sample_sum(samples + 200);
            }
            unsigned selected = sums[0] < sums[1] && sums[0] < total
                                    ? 0xfa
                                    : (sums[1] < sums[0] && sums[1] < total ? 0xf9 : 0xfc);
            fprintf(wire_log ? wire_log : stdout,
                    "ADAPTIVE RESULT round=%u a=%u b=%u baseline=%u first=%u second=%u "
                    "selected=%02x provisional=1\n",
                    round + 1, a, b, total, sums[0], sums[1], selected);
            unsigned next_a = a, next_b = b;
            if (sums[0] > total && sums[0] - total > 128000 && a < 31)
                ++next_a;
            if (sums[0] <= total && total - sums[0] > 128000 && a > 0)
                --next_a;
            if (sums[1] > total && sums[1] - total > 128000 && b < 31)
                ++next_b;
            if (sums[1] <= total && total - sums[1] > 128000 && b > 0)
                --next_b;
            if (a == next_a && b == next_b) {
                adaptive_a = a;
                adaptive_b = b;
                adaptive_mode = selected;
                puts("ADAPTIVE STOP: original comparison converged; no subsequent calibration "
                     "executed.");
                break;
            }
            a = next_a;
            b = next_b;
            if (round == 63) {
                fputs("ADAPTIVE round limit reached\n", stderr);
                return -1;
            }
        }
    }
    unsigned char ready[6] = {0};
    stage_complete = 1;
    return command(fd, "TEST UNIT READY AFTER BASELINE", ready, NULL, 0);
}

int completed_stage(int fd, int (*stage)(int), const char *name) {
    initialization_stage = name;
    fprintf(wire_log ? wire_log : stdout, "INITIALIZE stage=%s\n", name);
    stage_complete = 0;
    int result = stage(fd);
    return !transport_stop && stage_complete && (result >= 0 || initialize_condition) ? 0 : -1;
}

void set_modes(int color, int exposure, int stability, int final, int shading) {
    calibration_color = color;
    calibration_exposure = exposure;
    calibration_stability = stability;
    calibration_stability_loop = stability;
    calibration_exposure_final = final;
    calibration_shading = shading;
}

int bright_field(int fd) {
    unsigned char w[72], data[14016], set[10] = {0x24, 0, 0, 0, 0, 0, 0, 0, 72, 0};
    initialize_window(w, 0);
    w[24] = 9;
    w[25] = 32;
    w[33] = 0xff;
    w[54] = 20;
    w[55] = 17;
    w[56] = 63;
    for (unsigned c = 0; c < 3; ++c)
        w[67 + 2 * c] = calibration_exposures[c];
    int r = transfer(fd, "INITIALIZE BRIGHT WINDOW", set, 10, w, 72, SG_DXFER_TO_DEV, 60000);
    if (r != 72 && !(initialize_condition && last_transfer_size == 72))
        return -1;
    for (unsigned n = 0; n < 64; ++n) {
        unsigned char read[10] = {0x28, 0, 0, 0, 1, 0, 0, 0x36, 0xc0, 0};
        r = transfer(fd, "INITIALIZE BRIGHT READ", read, 10, data, sizeof(data), SG_DXFER_FROM_DEV,
                     60000);
        if (r != (int)sizeof(data) &&
            !(initialize_condition && last_transfer_size == (int)sizeof(data)))
            return -1;
        for (unsigned i = 0; i < sizeof(data); i += 2)
            if (data[i] > 3)
                return -1;
        fprintf(wire_log ? wire_log : stdout, "INITIALIZE stage=%s sample=%u\n",
                initialization_stage, n + 1);
        fprintf(wire_log ? wire_log : stdout, "INITIALIZE bright_sample=%u provisional=%u\n", n + 1,
                r != (int)sizeof(data));
    }
    return 0;
}

int cold_initialize(int fd) {
    unsigned char tur[6] = {0};
    int ready = command(fd, "INITIALIZE READY CHECK", tur, NULL, 0);
    if (ready == 0) {
        puts("INITIALIZE already_ready=1; cold acquisition skipped");
        return 0;
    }
    if (!initialize_condition)
        return -1;
    if (completed_stage(fd, light_check, "light") < 0)
        return -1;
    calibration_adaptive = 1;
    if (completed_stage(fd, calibration_baseline, "offsets") < 0)
        return -1;
    /* Scan acquisition still implements this verified offset/mode branch only.
     * Never substitute another sensor mode with these offsets. */
    if (adaptive_a != 17 || adaptive_b != 20 || adaptive_mode != 0xfc) {
        fputs("Unsupported cold-start offsets/mode; no fixed fallback applied\n", stderr);
        return -1;
    }
    set_modes(0, 0, 0, 0, 0);
    if (completed_stage(fd, calibration_wide, "dark-wide") < 0)
        return -1;
    set_modes(1, 0, 0, 0, 0);
    if (completed_stage(fd, calibration_wide, "dark-color") < 0)
        return -1;
    set_modes(1, 1, 0, 0, 0);
    if (completed_stage(fd, calibration_wide, "peak") < 0)
        return -1;
    calibration_stability_x = 213 + 32 * peak_block;
    calibration_stability_channel = peak_channel;
    set_modes(1, 1, 1, 0, 0);
    if (completed_stage(fd, calibration_wide, "stability") < 0)
        return -1;
    set_modes(1, 1, 0, 1, 0);
    if (completed_stage(fd, calibration_wide, "exposure") < 0)
        return -1;
    set_modes(1, 1, 0, 0, 1);
    if (completed_stage(fd, calibration_wide, "dark-field") < 0)
        return -1;
    initialization_stage = "white-first";
    fputs("INITIALIZE stage=white-first"
          "\n",
          wire_log ? wire_log : stdout);
    if (bright_field(fd) < 0)
        return -1;
    initialization_stage = "white-second";
    fputs("INITIALIZE stage=white-second"
          "\n",
          wire_log ? wire_log : stdout);
    if (bright_field(fd) < 0)
        return -1;
    if (command(fd, "INITIALIZE BEFORE TERMINAL", tur, NULL, 0) != 0)
        return -1;
    unsigned char terminal[10] = {0x31, 2, 0, 0, 0, 0, 0, 0, 0, 0};
    if (transfer(fd, "INITIALIZE TERMINAL", terminal, 10, NULL, 0, SG_DXFER_NONE, 60000) != 0)
        return -1;
    for (unsigned n = 0; n < 40; ++n) {
        if (command(fd, "INITIALIZE FINAL READY", tur, NULL, 0) == 0) {
            fprintf(wire_log ? wire_log : stdout, "INITIALIZE complete=1 exposures=%u,%u,%u\n",
                    calibration_exposures[0], calibration_exposures[1], calibration_exposures[2]);
            return 0;
        }
        if (!mechanism_busy && !target_busy)
            return -1;
        usleep(500000);
    }
    return -1;
}
