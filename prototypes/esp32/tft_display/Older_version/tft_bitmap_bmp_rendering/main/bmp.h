#ifndef BMP_H
#define BMP_H

#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* --------------------------------------------------------------------------
     * BMP Constants
     * -------------------------------------------------------------------------- */

#define BMP_SIGNATURE 0x4D42  /* 'BM' */
#define BMP_COMPRESSION_RGB 0 /* BI_RGB */
#define BMP_BPP_24 24         /* 24-bit BMP */

    /* --------------------------------------------------------------------------
     * BMP File Header
     * -------------------------------------------------------------------------- */

    typedef struct
    {
        uint16_t signature;         /* Must be 'BM' */
        uint32_t file_size;         /* Total file size */
        uint16_t reserved1;         /* Reserved */
        uint16_t reserved2;         /* Reserved */
        uint32_t pixel_data_offset; /* Offset to pixel data */

    } bmp_file_header_t;

    /* --------------------------------------------------------------------------
     * BMP Information Header
     * -------------------------------------------------------------------------- */

    typedef struct
    {
        uint32_t header_size;    /* Must be 40 */
        int32_t width;           /* Image width */
        int32_t height;          /* Image height */
        uint16_t planes;         /* Must be 1 */
        uint16_t bits_per_pixel; /* Supported: 24 */
        uint32_t compression;    /* Supported: BI_RGB */
        uint32_t image_size;     /* Image data size */
        int32_t x_pixels_per_meter;
        int32_t y_pixels_per_meter;
        uint32_t colors_used;
        uint32_t important_colors;

    } bmp_info_header_t;

    /* --------------------------------------------------------------------------
     * BMP Image Information
     * -------------------------------------------------------------------------- */

    typedef struct
    {
        uint32_t width;
        uint32_t height;
        uint16_t bits_per_pixel;

    } bmp_image_info_t;

    /* --------------------------------------------------------------------------
     * File Operations
     * -------------------------------------------------------------------------- */

    /* Open BMP file */
    esp_err_t bmp_open(
        FILE **fp,
        const char *filename);

    /* Close BMP file */
    void bmp_close(
        FILE *fp);

    /* --------------------------------------------------------------------------
     * Header Functions
     * -------------------------------------------------------------------------- */

    /* Read BMP file header */
    esp_err_t bmp_read_file_header(
        FILE *fp,
        bmp_file_header_t *file_header);

    /* Read BMP info header */
    esp_err_t bmp_read_info_header(
        FILE *fp,
        bmp_info_header_t *info_header);

    /* Read complete BMP information */
    esp_err_t bmp_get_info(
        const char *filename,
        bmp_image_info_t *info);

    /* --------------------------------------------------------------------------
     * Validation
     * -------------------------------------------------------------------------- */

    /* Validate BMP format */
    esp_err_t bmp_validate(
        const bmp_file_header_t *file_header,
        const bmp_info_header_t *info_header);

    /* --------------------------------------------------------------------------
     * Utility Functions
     * -------------------------------------------------------------------------- */

    /* Calculate padded row size */
    uint32_t bmp_get_row_size(
        uint32_t width);

    /* Move file pointer to pixel data */
    esp_err_t bmp_seek_pixel_data(
        FILE *fp,
        const bmp_file_header_t *file_header);

    /* --------------------------------------------------------------------------
     * Image Functions
     * -------------------------------------------------------------------------- */

    /* Read one BMP row */
    esp_err_t bmp_read_row(
        FILE *fp,
        uint8_t *row_buffer,
        uint32_t row_size);

    /* Convert RGB888 row to RGB565 */
    void bmp_rgb888_to_rgb565(
        const uint8_t *src,
        uint16_t *dst,
        uint32_t width);

    /* --------------------------------------------------------------------------
     * Display Functions
     * -------------------------------------------------------------------------- */

    /* Draw BMP image */
    esp_err_t display_draw_bmp(
        int x,
        int y,
        const char *filename);


#ifdef __cplusplus
}
#endif

#endif /* BMP_H */