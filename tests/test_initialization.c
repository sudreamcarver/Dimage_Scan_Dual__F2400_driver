#include <zlib.h>
#include "internal.h"
#include <assert.h>
#include <stdarg.h>
static gzFile fixture;
static unsigned consumed, fail_at, mode;
static int ready_state;
int __wrap_ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    (void)request;
    va_list ap;
    va_start(ap, request);
    sg_io_hdr_t *io = va_arg(ap, sg_io_hdr_t *);
    va_end(ap);
    unsigned char setup[20] = {0x70, 0, 9, 0, 0, 0, 0, 12, 0, 0, 0, 0, 4, 2, 0, 0, 0, 0, 0x80, 0};
    if (io->cmdp[0] == 0) {
        if (!ready_state) {
            io->status = 2;
            io->driver_status = 8;
            io->sb_len_wr = 20;
            memcpy(io->sbp, setup, 20);
        }
        return 0;
    }
    unsigned char cdb[10], head[8], sense[20], payload[14016];
    assert(gzread(fixture, cdb, 10) == 10);
    assert(!memcmp(cdb, io->cmdp, 10));
    assert(gzread(fixture, head, 8) == 8);
    unsigned len = head[4] | (head[5] << 8);
    assert(len == io->dxfer_len);
    assert(gzread(fixture, sense, 20) == 20);
    assert(gzread(fixture, payload, len) == (int)len);
    if (cdb[0] == 0x24)
        assert(!memcmp(io->dxferp, payload, len));
    else if (len)
        memcpy(io->dxferp, payload, len);
    io->status = head[0];
    io->driver_status = head[1];
    io->sb_len_wr = head[2];
    memcpy(io->sbp, sense, 20);
    ++consumed;
    if (consumed == fail_at) {
        if (mode == 1) {
            io->status = 2;
            io->driver_status = 8;
            io->sb_len_wr = 20;
            setup[18] = 0x40;
            memcpy(io->sbp, setup, 20);
        }
        if (mode == 2)
            io->resid = 1;
        if (mode == 3)
            transport_stop = 1;
    }
    if (cdb[0] == 0x28 && cdb[4] == 1 && !io->status)
        ready_state = 1;
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 4 || argc == 5);
    fixture = gzopen(argv[1], "rb");
    assert(fixture);
    fail_at = atoi(argv[2]);
    mode = atoi(argv[3]);
    wire_log = fopen("/dev/null", "w");
    assert(wire_log);
    int r = cold_initialize(99);
    if (fail_at) {
        assert(r < 0 && consumed == fail_at);
    } else {
        assert(r == 0 && consumed == 2105 && gzgetc(fixture) == -1);
        unsigned expected_block = argc == 5 ? atoi(argv[4]) : 39;
        assert(peak_block == expected_block && peak_channel == 1);
        assert(calibration_stability_x == 213 + 32 * expected_block);
        assert(calibration_exposures[0] == 22 && calibration_exposures[1] == 14 &&
               calibration_exposures[2] == 16);
    }
    gzclose(fixture);
    fprintf(stderr, "PASS initialization replay consumed=%u injected=%u mode=%u\n", consumed,
            fail_at, mode);
    return 0;
}

int __wrap_usleep(useconds_t delay) {
    (void)delay;
    return 0;
}
