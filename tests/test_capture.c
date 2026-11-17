#include "internal.h"
#include <assert.h>
#include <stdarg.h>
static unsigned calls;
static int scenario;
int __wrap_ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    (void)request;
    va_list ap;
    va_start(ap, request);
    sg_io_hdr_t *io = va_arg(ap, sg_io_hdr_t *);
    va_end(ap);
    ++calls;
    if (scenario >= 7 && scenario <= 9) {
        const unsigned char gray[10] = {0x28, 0, 0, 0, 0xfa, 8, 0, 2, 0x48, 0};
        assert(io->cmd_len == 10 && !memcmp(io->cmdp, gray, 10) && io->dxfer_len == 584);
        memset(io->dxferp, 0, 584);
        if (scenario == 8)
            io->resid = 2;
        if (scenario == 9) {
            io->status = 2;
            io->driver_status = 8;
        }
        return 0;
    }
    if (scenario <= 2 || scenario == 6) {
        const unsigned char cdb[10] = {0x28, 0, 0, 0, 0xff, 8, 0, 6, 0xd8, 0};
        assert(io->cmd_len == 10 && !memcmp(io->cmdp, cdb, 10) && io->dxfer_len == 1752);
        if (scenario == 6) {
            stop_requested = 1;
            errno = EINTR;
            return -1;
        }
        memset(io->dxferp, 0, 1752);
        if (scenario == 1)
            io->resid = 2;
        if (scenario == 2) {
            io->status = 2;
            io->driver_status = 8;
        }
        return 0;
    }
    if (scenario == 3) {
        assert(io->cmd_len == 6 && io->cmdp[0] == 0x15);
        const unsigned char busy[20] = {0x70, 0, 9, 0, 0, 0, 0, 12, 0,    0,
                                        0,    0, 0, 0, 0, 0, 0, 0,  0x10, 0};
        memcpy(io->sbp, busy, 20);
        io->sb_len_wr = 20;
        io->status = 2;
        io->driver_status = 8;
        return 0;
    }
    if (scenario == 4 || scenario == 5) {
        assert(io->cmdp[0] == 0);
        io->status = 2;
        io->driver_status = 8;
        unsigned char bad[20] = {0x70, 0, 9, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0};
        if (scenario == 5) {
            bad[18] = 0x10;
            io->resid = 1;
        }
        memcpy(io->sbp, bad, 20);
        io->sb_len_wr = 20;
        return 0;
    }
    return -1;
}
int main(void) {
    unsigned char *data = calloc(770880, 1);
    assert(data);
    unsigned done;
    for (scenario = 0; scenario <= 2; ++scenario) {
        calls = 0;
        int rc = read_factory_lines(-1, data, 770880, &done, 1, 8, 0);
        if (scenario == 0)
            assert(rc == 0 && calls == 440 && done == 770880);
        else
            assert(rc < 0 && calls == 1 && done == 0);
    }
    calls = 0;
    assert(read_factory_lines(-1, data, 770879, &done, 1, 8, 0) < 0 && calls == 0);
    scenario = 0;
    calls = 0;
    stop_requested = 1;
    assert(read_factory_lines(-1, data, 770880, &done, 1, 8, 0) == -2 && calls == 0);
    stop_requested = 0;
    calls = 0;
    assert(read_factory_lines(-1, data, 770880, &done, 1, 8, 16) == -2 && calls == 16 &&
           done == 28032);
    scenario = 3;
    calls = 0;
    assert(cancel_frame(-1) < 0 && mechanism_busy && calls == 1);
    scenario = 4;
    calls = 0;
    assert(finish_frame(-1) < 0 && calls == 1); /* unknown sense must not send release */
    scenario = 6;
    calls = 0;
    assert(read_factory_lines(-1, data, 770880, &done, 1, 8, 0) == -1 && calls == 1 && done == 0 &&
           stop_requested);
    stop_requested = 0;
    scenario = 5;
    calls = 0;
    assert(finish_frame(-1) < 0 && calls == 1);
    stop_requested = 0;
    for (scenario = 7; scenario <= 9; ++scenario) {
        calls = 0;
        int rc = read_factory_gray(-1, data, 770880, &done, 8, 0);
        if (scenario == 7)
            assert(rc == 0 && calls == 438 && done == 255792);
        else
            assert(rc < 0 && calls == 1 && done == 0);
    }
    calls = 0;
    assert(read_factory_gray(-1, data, 255791, &done, 8, 0) < 0 && calls == 0);
    scenario = 7;
    calls = 0;
    assert(read_factory_gray(-1, data, 770880, &done, 8, 16) == -2 && calls == 16 && done == 9344);
    free(data);
    puts("PASS: exact 440-line factory meter; no READ retry on short/error; signal/boundary "
         "cancellation; busy cancel remains ambiguous; unknown sense blocks mechanical mutation.");
    return 0;
}
