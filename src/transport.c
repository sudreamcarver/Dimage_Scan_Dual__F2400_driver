#include "internal.h"
#include "f2400-errors.h"
volatile sig_atomic_t stop_requested, transport_stop;
int stage_complete, initialize_condition, mechanism_busy, target_busy, last_transfer_size;
int initializing;
FILE *wire_log;
const char *initialization_stage;
unsigned adaptive_a = 17, adaptive_b = 20, adaptive_mode = 0xfc;
unsigned peak_channel = 1, peak_block = 38;
unsigned calibration_exposures[3] = {22, 14, 16};
int calibration_adaptive, calibration_color, calibration_exposure, calibration_stability,
    calibration_stability_loop, calibration_exposure_final, calibration_shading;
unsigned calibration_stability_x = 1429, calibration_stability_channel = 1;
unsigned refresh_exposures[3] = {22, 14, 16};
int transfer(int fd, const char *name, unsigned char *cdb, unsigned cdb_len, unsigned char *data,
             unsigned len, int direction, unsigned timeout) {
    if (transport_stop || (initializing && stop_requested)) {
        puts("INITIALIZE stopped between commands; readiness unconfirmed");
        return -1;
    }
    unsigned char sense[64] = {0};
    sg_io_hdr_t io = {0};
    io.interface_id = 'S';
    io.cmd_len = cdb_len;
    io.cmdp = cdb;
    io.dxfer_direction = direction;
    io.dxferp = data;
    io.dxfer_len = len;
    io.sbp = sense;
    io.mx_sb_len = sizeof(sense);
    io.timeout = timeout;
    initialize_condition = 0;
    mechanism_busy = 0;
    target_busy = 0;
    last_transfer_size = -1;
    fprintf(wire_log ? wire_log : stdout, "%s CDB:", name);
    for (unsigned i = 0; i < cdb_len; ++i)
        fprintf(wire_log ? wire_log : stdout, " %02x", cdb[i]);
    fputc('\n', wire_log ? wire_log : stdout);
    if (ioctl(fd, SG_IO, &io) < 0) {
        int saved_errno = errno;
        fprintf(wire_log ? wire_log : stdout,
                "transport_errno=%d operation_state=unknown no_retry=1\n", saved_errno);
        errno = saved_errno;
        perror("SG_IO");
        return -1;
    }
    fprintf(wire_log ? wire_log : stdout,
            "status=%02x host=%04x driver=%04x residual=%d duration_ms=%u\n", io.status,
            io.host_status, io.driver_status, io.resid, io.duration);
    fprintf(wire_log ? wire_log : stdout, "sense:");
    for (unsigned i = 0; i < io.sb_len_wr; ++i)
        fprintf(wire_log ? wire_log : stdout, " %02x", sense[i]);
    fputc('\n', wire_log ? wire_log : stdout);
    if (io.sb_len_wr >= 14 && ((sense[0] & 0x7f) == 0x70 || (sense[0] & 0x7f) == 0x71))
        fprintf(wire_log ? wire_log : stdout,
                "fixed sense: key=%02x ASC=%02x ASCQ=%02x information_valid=%u\n", sense[2] & 15,
                sense[12], sense[13], !!(sense[0] & 0x80));
    static const unsigned char known_setup_sense[20] = {0x70, 0, 9, 0, 0, 0, 0, 12, 0,    0,
                                                        0,    0, 4, 2, 0, 0, 0, 0,  0x80, 0};
    initialize_condition = io.status == 2 && io.host_status == 0 && io.resid == 0 &&
                           io.driver_status == 8 && io.sb_len_wr == sizeof(known_setup_sense) &&
                           !memcmp(sense, known_setup_sense, sizeof(known_setup_sense));
    static const unsigned char busy_sense[20] = {0x70, 0, 9, 0, 0, 0, 0, 12, 0,    0,
                                                 0,    0, 0, 0, 0, 0, 0, 0,  0x10, 0};
    mechanism_busy = io.status == 2 && !io.host_status && io.driver_status == 8 && io.resid == 0 &&
                     io.sb_len_wr == sizeof(busy_sense) &&
                     !memcmp(sense, busy_sense, sizeof(busy_sense));
    target_busy =
        io.status == 8 && !io.host_status && !io.driver_status && !io.sb_len_wr && io.resid == 0;
    if (initialize_condition)
        puts("Original driver guidance for this extended state: Setting up now. Remove the film "
             "holder.\n"
             "Interpretation from original error handler/UI; readiness is still unconfirmed.");
    if (io.status && io.sb_len_wr >= 20 && (sense[0] & 0x7f) == 0x70 && (sense[2] & 15) == 9) {
        int code = f2400_vendor_error(sense[18], sense[19], 0, 0, 0);
        fprintf(
            wire_log ? wire_log : stdout,
            "MINOLTA ERROR diagnostic_page_flags=00 code=%d flags18=%02x flags19=%02x detail=%s\n",
            code, sense[18], sense[19], f2400_error_message(code));
    }
    if (io.resid < 0 || (unsigned)io.resid > len)
        return -1;
    unsigned actual = len - (unsigned)io.resid;
    last_transfer_size = (int)actual;
    if (len) {
        fprintf(wire_log ? wire_log : stdout, "data_length=%u\n", actual);
        if (io.status || io.host_status || io.driver_status)
            fputs("Command failed: buffer dump is unvalidated, not a successful transfer."
                  "\n",
                  wire_log ? wire_log : stdout);
        for (unsigned i = 0; i < actual; ++i) {
            if (i % 16 == 0)
                fprintf(wire_log ? wire_log : stdout, "%04x:", i);
            fprintf(wire_log ? wire_log : stdout, " %02x", data[i]);
            if (i % 16 == 15 || i + 1 == actual)
                fputc('\n', wire_log ? wire_log : stdout);
        }
    }
    if (io.status || io.host_status || io.driver_status)
        return -1;
    return (int)actual;
}

int command(int fd, const char *name, unsigned char *cdb, unsigned char *data, unsigned int len) {
    return transfer(fd, name, cdb, 6, data, len, len ? SG_DXFER_FROM_DEV : SG_DXFER_NONE, 5000);
}
