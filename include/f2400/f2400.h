#ifndef F2400_H
#define F2400_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define F2400_API __attribute__((visibility("default")))
#define F2400_COLUMNS 2336
#define F2400_HEIGHT 3504
#define F2400_CAL_SIZE 7008
/* All calls are synchronous. Hardware jobs belong in a worker thread/process.
 * A process may run one job at a time; device flock also excludes other jobs.
 * Return codes: 0 complete, 1 failed, 2 configuration/device, 130 cancelled.
 * Never use a cancelled/failed job's image as a complete result. */
typedef enum {
    F2400_POSITIVE,
    F2400_NEGATIVE,
    F2400_BW_NEGATIVE,
    F2400_RAW_RGB,
    F2400_RAW_GREEN
} f2400_film;
typedef struct {
    unsigned exposures[3], levels[3], means[3], maxima[3], minima[3], peaks[3];
    unsigned classifier, counts[7], band_count, band_average, selected_channel, selected_average;
    unsigned block_x[3], block_y[3];
    double factors[3];
    int limited, no_signal;
} f2400_ae;
typedef struct {
    unsigned exposure, gains[3], percentages[3];
} f2400_normalization;
F2400_API int f2400_normalize_color_exposure(const unsigned requested[3], f2400_normalization *out);
typedef struct {
    unsigned width, height, channels, source_bits;
    uint16_t *pixels;
    uint8_t *preview;
} f2400_image;
F2400_API const char *f2400_error(void);
F2400_API int f2400_lut(f2400_film film, unsigned scale, unsigned dark, unsigned level,
                        uint16_t out[4096]);
F2400_API int f2400_meter(const uint8_t *raw, size_t size, const uint16_t dark[7008],
                          const uint8_t shade[7008], const unsigned base[3], f2400_film film,
                          f2400_ae *out);
F2400_API int f2400_process(const uint8_t *raw, size_t size, const uint16_t dark[7008],
                            const uint8_t shade[7008], unsigned ratio, f2400_film film,
                            const unsigned levels[3], unsigned percentage, f2400_image **out);
F2400_API void f2400_image_free(f2400_image *image);
F2400_API int f2400_image_rotate(f2400_image *image, int clockwise);
F2400_API int f2400_image_save(const f2400_image *image, const char *path);
F2400_API f2400_image *f2400_image_load(const char *path);
F2400_API const uint8_t *f2400_image_preview(const f2400_image *image);
F2400_API unsigned f2400_image_width(const f2400_image *image);
F2400_API unsigned f2400_image_height(const f2400_image *image);
F2400_API unsigned f2400_image_channels(const f2400_image *image);
F2400_API unsigned f2400_image_depth(const f2400_image *image);
F2400_API int f2400_profile_info(const char *path, char *json, size_t capacity);
F2400_API int f2400_scan(const char *directory, const char *profile, f2400_film film,
                         unsigned ratio, int save_tiff);
F2400_API int f2400_calibrate(const char *directory, int cold_start, int holder_removed);
F2400_API int f2400_control(int release);
F2400_API void f2400_install_signals(void);
#ifdef __cplusplus
}
#endif
#endif
