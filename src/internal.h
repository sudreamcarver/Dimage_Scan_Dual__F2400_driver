#ifndef F2400_INTERNAL_H
#define F2400_INTERNAL_H
#include "f2400/f2400.h"
#include <errno.h>
#include <fcntl.h>
#include <scsi/sg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <json-c/json.h>
#include "f2400-device.h"
extern volatile sig_atomic_t stop_requested, transport_stop;
extern int initializing, stage_complete, initialize_condition, mechanism_busy, target_busy,
    last_transfer_size;
extern FILE *wire_log;
extern const char *initialization_stage;
extern unsigned adaptive_a, adaptive_b, adaptive_mode, peak_channel, peak_block,
    calibration_exposures[3];
extern int calibration_adaptive, calibration_color, calibration_exposure, calibration_stability,
    calibration_stability_loop, calibration_exposure_final, calibration_shading;
extern unsigned calibration_stability_x, calibration_stability_channel, refresh_exposures[3];
static inline void f2400_log_puts(const char *s) {
    fprintf(wire_log ? wire_log : stdout, "%s\n", s);
}
#define puts(s) f2400_log_puts(s)
int fail(const char *format, ...) __attribute__((format(printf, 1, 2)));
int read_file(const char *path, uint8_t **data, size_t *size);
int write_file(const char *path, const void *data, size_t size);
int hash_data(const void *data, size_t size, char out[65]);
int path_join(char *out, size_t size, const char *dir, const char *name);
int write_json(const char *path, json_object *value);
json_object *json_file(const char *path);
json_object *uint_array(const unsigned *values, unsigned count);
int json_uints(json_object *j, const char *key, unsigned *out, unsigned n, unsigned low,
               unsigned high);
json_object *image_statistics(const uint8_t *raw, const uint16_t dark[7008], unsigned ratio,
                              int gray);
int f2400_device_open(void);
int open_locked(void);
int capture_frame(int fd, const char *prefix, const unsigned exposures[3], unsigned ratio,
                  int meter, int gray);
int load_profile(const char *path, uint8_t shade[7008], unsigned base[3], unsigned negative[3],
                 json_object **metadata);
int derive_calibration(const uint32_t dark[7008], const uint32_t white[7008],
                       const uint32_t correction[7008], const unsigned exposure[3],
                       uint16_t dark_out[7008], uint8_t shade[7008], uint8_t blocks[96],
                       unsigned positive[3], unsigned negative[3], unsigned means[3]);
int export_profile(const char *dir);
int normalize_bw(unsigned requested, unsigned *exposure, unsigned *percentage);
int verify_capture(const char *prefix, unsigned ratio, const unsigned exposures[3], int meter,
                   int gray, json_object **audit);
int transfer(int fd, const char *name, unsigned char *cdb, unsigned cdb_len, unsigned char *data,
             unsigned len, int direction, unsigned timeout);
int command(int fd, const char *name, unsigned char *cdb, unsigned char *data, unsigned int len);
void initialize_window(unsigned char window[72], unsigned channel);
uint32_t sample_sum(const unsigned char samples[200]);
int light_check(int fd);
int calibration_wide(int fd);
int calibration_baseline(int fd);
int completed_stage(int fd, int (*stage)(int), const char *name);
void set_modes(int color, int exposure, int stability, int final, int shading);
int bright_field(int fd);
int cold_initialize(int fd);
int wait_frame_ready(int fd, unsigned attempts, unsigned delay_us);
int cancel_frame(int fd);
int finish_frame(int fd);
int prepare35_samples(int fd, uint16_t dark[7008], const unsigned exposure[3], unsigned samples);
int save_bytes(const char *prefix, const char *suffix, const void *data, size_t length);
int read_factory_lines(int fd, unsigned char *data, unsigned capacity, unsigned *completed,
                       int meter, unsigned ratio, unsigned cancel_after);
int read_factory_gray(int fd, unsigned char *data, unsigned capacity, unsigned *completed,
                      unsigned ratio, unsigned cancel_after);
int calibration_field(int fd, const char *prefix, const char *name, int white);

#endif
