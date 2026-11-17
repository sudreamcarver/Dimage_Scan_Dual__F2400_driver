#include "internal.h"
#include <png.h>
#include <tiffio.h>
static f2400_image *image_new(unsigned w, unsigned h, unsigned channels) {
    if (!w || !h || (channels != 1 && channels != 3) || (uint64_t)w * h > 30000000) {
        fail("unsupported image geometry");
        return NULL;
    }
    f2400_image *im = calloc(1, sizeof(*im));
    if (!im) {
        fail("out of memory");
        return NULL;
    }
    size_t n = (size_t)w * h * channels;
    im->width = w;
    im->height = h;
    im->channels = channels;
    im->source_bits = 16;
    im->pixels = calloc(n, sizeof(uint16_t));
    im->preview = calloc(n, 1);
    if (!im->pixels || !im->preview) {
        f2400_image_free(im);
        fail("out of memory");
        return NULL;
    }
    return im;
}
void f2400_image_free(f2400_image *im) {
    if (im) {
        free(im->pixels);
        free(im->preview);
        free(im);
    }
}
const uint8_t *f2400_image_preview(const f2400_image *im) {
    return im ? im->preview : NULL;
}
unsigned f2400_image_width(const f2400_image *im) {
    return im ? im->width : 0;
}
unsigned f2400_image_height(const f2400_image *im) {
    return im ? im->height : 0;
}
unsigned f2400_image_channels(const f2400_image *im) {
    return im ? im->channels : 0;
}
unsigned f2400_image_depth(const f2400_image *im) {
    return im ? im->source_bits : 0;
}
int f2400_image_rotate(f2400_image *im, int clockwise) {
    if (!im)
        return fail("no image");
    f2400_image *turned = image_new(im->height, im->width, im->channels);
    if (!turned)
        return -1;
    for (unsigned y = 0; y < im->height; ++y)
        for (unsigned x = 0; x < im->width; ++x) {
            unsigned nx = clockwise ? im->height - 1 - y : y,
                     ny = clockwise ? x : im->width - 1 - x;
            for (unsigned c = 0; c < im->channels; ++c) {
                size_t a = ((size_t)y * im->width + x) * im->channels + c,
                       b = ((size_t)ny * turned->width + nx) * im->channels + c;
                turned->pixels[b] = im->pixels[a];
                turned->preview[b] = im->preview[a];
            }
        }
    turned->source_bits = im->source_bits;
    free(im->pixels);
    free(im->preview);
    *im = *turned;
    free(turned);
    return 0;
}
static int extension(const char *path, const char *ext) {
    const char *p = strrchr(path, '.');
    return p && !strcasecmp(p, ext);
}
int f2400_image_save(const f2400_image *im, const char *path) {
    if (!im || !path)
        return fail("no image/path");
    if (extension(path, ".png")) {
        FILE *file = fopen(path, "wb");
        if (!file)
            return fail("PNG open %s", path);
        png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        png_infop info = png ? png_create_info_struct(png) : NULL;
        if (!png || !info) {
            if (png)
                png_destroy_write_struct(&png, NULL);
            fclose(file);
            return fail("PNG allocation");
        }
        if (setjmp(png_jmpbuf(png))) {
            png_destroy_write_struct(&png, &info);
            fclose(file);
            return fail("PNG export failed");
        }
        png_init_io(png, file);
        png_set_IHDR(png, info, im->width, im->height, 8,
                     im->channels == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_GRAY,
                     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        /* No gamma, sRGB or ICC chunks: RAW values stay explicitly linear. */
        png_write_info(png, info);
        for (unsigned y = 0; y < im->height; ++y)
            png_write_row(png, im->preview + (size_t)y * im->width * im->channels);
        png_write_end(png, info);
        png_destroy_write_struct(&png, &info);
        if (fclose(file))
            return fail("PNG write failed");
        return 0;
    }
    if (!extension(path, ".tiff") && !extension(path, ".tif"))
        return fail("choose PNG or TIFF");
    TIFF *t = TIFFOpen(path, "wl");
    if (!t)
        return fail("TIFF open %s", path);
    TIFFSetField(t, TIFFTAG_IMAGEWIDTH, im->width);
    TIFFSetField(t, TIFFTAG_IMAGELENGTH, im->height);
    TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, im->channels);
    TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 16);
    TIFFSetField(t, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(t, TIFFTAG_PHOTOMETRIC,
                 im->channels == 3 ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK);
    TIFFSetField(t, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, im->height);
    for (unsigned y = 0; y < im->height; ++y)
        if (TIFFWriteScanline(t, im->pixels + (size_t)y * im->width * im->channels, y, 0) < 0) {
            TIFFClose(t);
            return fail("TIFF write failed");
        }
    TIFFClose(t);
    return 0;
}
static f2400_image *load_tiff(const char *path) {
    TIFF *t = TIFFOpen(path, "r");
    if (!t) {
        fail("TIFF open %s", path);
        return NULL;
    }
    uint32_t w = 0, h = 0;
    uint16_t channels = 0, bits = 0, planar = 0, photo = 0, orientation = ORIENTATION_TOPLEFT,
             format = SAMPLEFORMAT_UINT;
    TIFFGetField(t, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(t, TIFFTAG_IMAGELENGTH, &h);
    TIFFGetFieldDefaulted(t, TIFFTAG_SAMPLESPERPIXEL, &channels);
    TIFFGetFieldDefaulted(t, TIFFTAG_BITSPERSAMPLE, &bits);
    TIFFGetFieldDefaulted(t, TIFFTAG_PLANARCONFIG, &planar);
    TIFFGetFieldDefaulted(t, TIFFTAG_PHOTOMETRIC, &photo);
    TIFFGetFieldDefaulted(t, TIFFTAG_ORIENTATION, &orientation);
    TIFFGetFieldDefaulted(t, TIFFTAG_SAMPLEFORMAT, &format);
    if ((channels != 1 && channels != 3) || (bits != 8 && bits != 16) ||
        planar != PLANARCONFIG_CONTIG || orientation != ORIENTATION_TOPLEFT ||
        format != SAMPLEFORMAT_UINT ||
        photo != (channels == 3 ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK)) {
        TIFFClose(t);
        fail("unsupported TIFF layout");
        return NULL;
    }
    f2400_image *im = image_new(w, h, channels);
    if (!im) {
        TIFFClose(t);
        return NULL;
    }
    size_t row = (size_t)w * channels * (bits / 8);
    if (TIFFScanlineSize(t) != (tmsize_t)row) {
        TIFFClose(t);
        f2400_image_free(im);
        fail("TIFF scanline mismatch");
        return NULL;
    }
    uint8_t *buf = malloc(row);
    if (!buf) {
        TIFFClose(t);
        f2400_image_free(im);
        fail("out of memory");
        return NULL;
    }
    int ok = 1;
    for (unsigned y = 0; y < h; ++y) {
        if (TIFFReadScanline(t, buf, y, 0) < 0) {
            ok = 0;
            break;
        }
        for (unsigned i = 0; i < w * channels; ++i) {
            uint16_t v = bits == 16 ? ((uint16_t *)buf)[i] : (uint16_t)buf[i] << 8;
            size_t j = (size_t)y * w * channels + i;
            im->pixels[j] = v;
            im->preview[j] = v >> 8;
        }
    }
    im->source_bits = bits;
    free(buf);
    TIFFClose(t);
    if (!ok) {
        f2400_image_free(im);
        fail("TIFF read failed");
        return NULL;
    }
    return im;
}
f2400_image *f2400_image_load(const char *path) {
    if (!path) {
        fail("missing image path");
        return NULL;
    }
    if (extension(path, ".tif") || extension(path, ".tiff"))
        return load_tiff(path);
    if (!extension(path, ".png")) {
        fail("choose PNG or TIFF");
        return NULL;
    }
    png_image p = {0};
    p.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&p, path)) {
        fail("PNG input: %s", p.message);
        png_image_free(&p);
        return NULL;
    }
    unsigned channels = (p.format & PNG_FORMAT_FLAG_COLOR) ? 3 : 1;
    p.format = channels == 3 ? PNG_FORMAT_RGB : PNG_FORMAT_GRAY;
    f2400_image *im = image_new(p.width, p.height, channels);
    if (!im) {
        png_image_free(&p);
        return NULL;
    }
    if (!png_image_finish_read(&p, NULL, im->preview, 0, NULL)) {
        fail("PNG input: %s", p.message);
        png_image_free(&p);
        f2400_image_free(im);
        return NULL;
    }
    png_image_free(&p);
    im->source_bits = 8;
    for (size_t i = 0; i < (size_t)im->width * im->height * channels; ++i)
        im->pixels[i] = (uint16_t)im->preview[i] << 8;
    char sibling[PATH_MAX];
    size_t len = strlen(path);
    if (len + 2 < sizeof(sibling)) {
        memcpy(sibling, path, len + 1);
        char *dot = strrchr(sibling, '.');
        strcpy(dot, ".tiff");
        if (!access(sibling, R_OK)) {
            f2400_image *high = load_tiff(sibling);
            if (!high || high->width != im->width || high->height != im->height ||
                high->channels != im->channels) {
                f2400_image_free(high);
                f2400_image_free(im);
                fail("preview/TIFF geometry mismatch");
                return NULL;
            }
            free(im->pixels);
            im->source_bits = high->source_bits;
            im->pixels = high->pixels;
            high->pixels = NULL;
            f2400_image_free(high);
        }
    }
    return im;
}
int f2400_process(const uint8_t *raw, size_t size, const uint16_t dark[7008],
                  const uint8_t shade[7008], unsigned ratio, f2400_film film,
                  const unsigned levels[3], unsigned percentage, f2400_image **out) {
    if (!out)
        return fail("missing image output");
    *out = NULL;
    int gray = film == F2400_BW_NEGATIVE || film == F2400_RAW_GREEN,
        linear = film == F2400_RAW_RGB || film == F2400_RAW_GREEN;
    if (!raw || !dark || !shade || (ratio != 1 && ratio != 4) ||
        (film < F2400_POSITIVE || film > F2400_RAW_GREEN) || percentage < 100 || percentage > 1000)
        return fail("unsupported processing parameters");
    unsigned w = 2336 / ratio, h = 3504 / ratio, channels = gray ? 1 : 3, means[3] = {0};
    if (size != (size_t)w * h * channels * 2)
        return fail("incomplete frame");
    for (unsigned i = 0; i < 7008; ++i) {
        if (dark[i] > 1023 || !shade[i])
            return fail("invalid image calibration");
        means[i / 2336] += dark[i];
    }
    for (unsigned c = 0; c < 3; ++c)
        means[c] /= 2336;
    uint16_t lut[3][4096];
    unsigned coeff[3][2336];
    for (unsigned c = 0; c < 3; ++c) {
        for (unsigned x = 0; x < w; ++x)
            coeff[c][x] = (unsigned)(255.0 / shade[c * 2336 + x * ratio] * 1000.0);
        if (!linear && f2400_lut(film, 100, film == F2400_POSITIVE ? 0 : means[c],
                                 levels ? levels[c] : 256, lut[c]) < 0)
            return -1;
    }
    f2400_image *im = image_new(w, h, channels);
    if (!im)
        return -1;
    size_t offset = 0;
    uint32_t rng = 1;
    unsigned stagger = gray ? 0 : 16 / ratio;
    for (unsigned row = 0; row < h + stagger; ++row)
        for (unsigned ch = 0; ch < channels; ++ch) {
            unsigned c = gray ? 1 : ch, start = gray ? 0 : c * stagger / 2;
            if (row < start || row >= h + start)
                continue;
            unsigned y = row - start;
            for (unsigned x = 0; x < w; ++x) {
                unsigned sample = (unsigned)raw[offset] * 256 + raw[offset + 1];
                offset += 2;
                if (sample > 1023) {
                    f2400_image_free(im);
                    return fail("frame ADC exceeds 10 bits");
                }
                unsigned d = dark[c * 2336 + x * ratio];
                unsigned v = (sample > d ? sample - d : 0) * 4;
                v = v * coeff[c][x] / 1000;
                if (v > 4095 - means[c] * 4)
                    v = 4095 - means[c] * 4;
                if (!linear) {
                    if (gray) {
                        v = v * percentage / 100;
                        if (v > 4095)
                            v = 4095;
                    }
                    v = lut[c][v];
                }
                size_t index = ((size_t)y * w + x) * channels + ch;
                im->pixels[index] = (uint16_t)(v << 4);
                unsigned p = v >> 4;
                if (!gray && !linear) {
                    rng = 214013 * rng + 2531011;
                    if (p <= 25)
                        p += ((rng >> 16) & 32767) / 10922;
                }
                im->preview[index] = (uint8_t)p;
            }
        }
    if (offset != size) {
        f2400_image_free(im);
        return fail("frame reconstruction mismatch");
    }
    /* Factory wire orientation -> presentation orientation (90 degrees CCW). */
    if (f2400_image_rotate(im, 0) < 0) {
        f2400_image_free(im);
        return -1;
    }
    *out = im;
    return 0;
}

static unsigned histogram_percentile(const unsigned hist[1024], unsigned count, double fraction) {
    unsigned rank = (unsigned)((count - 1) * fraction), cumulative = 0;
    for (unsigned i = 0; i < 1024; ++i) {
        cumulative += hist[i];
        if (cumulative > rank)
            return i;
    }
    return 1023;
}
json_object *image_statistics(const uint8_t *raw, const uint16_t dark[7008], unsigned ratio,
                              int gray) {
    unsigned w = 2336 / ratio, h = 3504 / ratio, stagger = gray ? 0 : 16 / ratio;
    unsigned hist[3][1024] = {{0}}, count[3] = {0}, clipped[3] = {0}, low[3] = {1023, 1023, 1023},
             high[3] = {0};
    size_t offset = 0;
    unsigned margin_x = w / 20, margin_y = h / 20;
    for (unsigned row = 0; row < h + stagger; ++row)
        for (unsigned ch = 0; ch < (gray ? 1u : 3u); ++ch) {
            unsigned c = gray ? 1 : ch, start = gray ? 0 : c * stagger / 2;
            if (row < start || row >= h + start)
                continue;
            unsigned y = row - start;
            for (unsigned x = 0; x < w; ++x) {
                unsigned v = (unsigned)raw[offset] * 256 + raw[offset + 1];
                offset += 2;
                if (v < low[ch])
                    low[ch] = v;
                if (v > high[ch])
                    high[ch] = v;
                if (x < margin_x || x >= w - margin_x || y < margin_y || y >= h - margin_y)
                    continue;
                unsigned d = dark[c * 2336 + x * ratio];
                ++hist[ch][v > d ? v - d : 0];
                ++count[ch];
                clipped[ch] += v >= 1020;
            }
        }
    json_object *array = json_object_new_array();
    for (unsigned ch = 0; ch < (gray ? 1u : 3u); ++ch) {
        json_object *j = json_object_new_object();
        json_object_object_add(j, "channel", json_object_new_int(gray ? 1 : ch));
        json_object_object_add(
            j, "low", json_object_new_int(histogram_percentile(hist[ch], count[ch], .002)));
        json_object_object_add(j, "median",
                               json_object_new_int(histogram_percentile(hist[ch], count[ch], .5)));
        json_object_object_add(
            j, "peak", json_object_new_int(histogram_percentile(hist[ch], count[ch], .998)));
        json_object_object_add(j, "clipped_fraction",
                               json_object_new_double(clipped[ch] / (double)count[ch]));
        json_object_object_add(j, "sample_range", uint_array((unsigned[2]){low[ch], high[ch]}, 2));
        json_object_object_add(j, "count", json_object_new_int(count[ch]));
        json_object_object_add(
            j, "roi", uint_array((unsigned[4]){margin_x, margin_y, w - margin_x, h - margin_y}, 4));
        json_object_array_add(array, j);
    }
    if (gray) {
        json_object *first = json_object_get(json_object_array_get_idx(array, 0));
        json_object_put(array);
        return first;
    }
    return array;
}
