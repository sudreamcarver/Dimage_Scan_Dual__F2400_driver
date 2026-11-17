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
    if (io->cmdp[0] == 0x24) {
        assert(io->dxfer_len == 72);
        return 0;
    }
    assert(io->cmdp[0] == 0x28 && io->dxfer_len == 14016);
    if (scenario == 1) {
        io->resid = 2;
        return 0;
    }
    memset(io->dxferp, 0, 14016);
    if (scenario == 2)
        ((unsigned char *)io->dxferp)[0] = 4;
    if (scenario == 3)
        stop_requested = 1;
    return 0;
}
int main(void) {
    for (scenario = 1; scenario <= 3; ++scenario) {
        calls = 0;
        stop_requested = 0;
        assert(calibration_field(-1, "/nonexistent/no-output", "dark", 0) < 0 && calls == 2);
    }
    calls = 0;
    stop_requested = 1;
    assert(calibration_field(-1, "/nonexistent/no-output", "white", 1) == -2 && calls == 0);
    puts("PASS calibration failures stop before next READ/export; preexisting cancellation sends "
         "no command");
    return 0;
}
