#include "bmp.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"

static const char *TAG = "BMP";

/* --------------------------------------------------------------------------
 * File Operations
 * -------------------------------------------------------------------------- */

esp_err_t bmp_open(
    FILE **fp,
    const char *filename)
{
    if ((fp == NULL) || (filename == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    *fp = fopen(filename, "rb");

    if (*fp == NULL)
    {
        ESP_LOGE(TAG, "Failed to open BMP file: %s", filename);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void bmp_close(
    FILE *fp)
{
    if (fp != NULL)
    {
        fclose(fp);
    }
}

/* --------------------------------------------------------------------------
 * Header Functions
 * -------------------------------------------------------------------------- */

esp_err_t bmp_read_file_header(
    FILE *fp,
    bmp_file_header_t *file_header)
{
    if ((fp == NULL) || (file_header == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (fread(&file_header->signature,
              sizeof(uint16_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&file_header->file_size,
              sizeof(uint32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&file_header->reserved1,
              sizeof(uint16_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&file_header->reserved2,
              sizeof(uint16_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&file_header->pixel_data_offset,
              sizeof(uint32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t bmp_read_info_header(
    FILE *fp,
    bmp_info_header_t *info_header)
{
    if ((fp == NULL) || (info_header == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (fread(&info_header->header_size,
              sizeof(uint32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->width,
              sizeof(int32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->height,
              sizeof(int32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->planes,
              sizeof(uint16_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->bits_per_pixel,
              sizeof(uint16_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->compression,
              sizeof(uint32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->image_size,
              sizeof(uint32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->x_pixels_per_meter,
              sizeof(int32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->y_pixels_per_meter,
              sizeof(int32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->colors_used,
              sizeof(uint32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->important_colors,
              sizeof(uint32_t),
              1,
              fp) != 1)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t bmp_get_info(
    const char *filename,
    bmp_image_info_t *info)
{
    if ((filename == NULL) || (info == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *fp = NULL;

    bmp_file_header_t file_header;
    bmp_info_header_t info_header;

    ESP_ERROR_CHECK(bmp_open(&fp, filename));

    if (bmp_read_file_header(fp, &file_header) != ESP_OK)
    {
        bmp_close(fp);
        return ESP_FAIL;
    }

    if (bmp_read_info_header(fp, &info_header) != ESP_OK)
    {
        bmp_close(fp);
        return ESP_FAIL;
    }

    bmp_close(fp);

    info->width = info_header.width;
    info->height = info_header.height;
    info->bits_per_pixel = info_header.bits_per_pixel;

    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Validation
 * -------------------------------------------------------------------------- */

esp_err_t bmp_validate(
    const bmp_file_header_t *file_header,
    const bmp_info_header_t *info_header)
{
    if ((file_header == NULL) || (info_header == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* Check BMP signature */
    if (file_header->signature != BMP_SIGNATURE)
    {
        ESP_LOGE(TAG, "Invalid BMP signature");
        return ESP_FAIL;
    }

    /* Only support BITMAPINFOHEADER (40 bytes) */
    if (info_header->header_size != 40)
    {
        ESP_LOGE(TAG, "Unsupported BMP header");
        return ESP_FAIL;
    }

    /* Number of planes must be 1 */
    if (info_header->planes != 1)
    {
        ESP_LOGE(TAG, "Invalid BMP planes");
        return ESP_FAIL;
    }

    /* Only support 24-bit BMP */
    if (info_header->bits_per_pixel != BMP_BPP_24)
    {
        ESP_LOGE(TAG,
                 "Unsupported bits per pixel: %d",
                 info_header->bits_per_pixel);
        return ESP_FAIL;
    }

    /* Only support uncompressed BMP */
    if (info_header->compression != BMP_COMPRESSION_RGB)
    {
        ESP_LOGE(TAG, "Compressed BMP not supported");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Utility Functions
 * -------------------------------------------------------------------------- */

uint32_t bmp_get_row_size(
    uint32_t width)
{
    /* 24-bit BMP rows are aligned to 4-byte boundaries */
    return ((width * 3U + 3U) & ~3U);
}

esp_err_t bmp_seek_pixel_data(
    FILE *fp,
    const bmp_file_header_t *file_header)
{
    if ((fp == NULL) || (file_header == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (fseek(fp,
              file_header->pixel_data_offset,
              SEEK_SET) != 0)
    {
        ESP_LOGE(TAG, "Failed to seek pixel data");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Image Functions
 * -------------------------------------------------------------------------- */

esp_err_t bmp_read_row(
    FILE *fp,
    uint8_t *row_buffer,
    uint32_t row_size)
{
    if ((fp == NULL) ||
        (row_buffer == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (fread(row_buffer,
              1,
              row_size,
              fp) != row_size)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

void bmp_rgb888_to_rgb565(
    const uint8_t *src,
    uint16_t *dst,
    uint32_t width)
{
    if ((src == NULL) ||
        (dst == NULL))
    {
        return;
    }

    for (uint32_t i = 0; i < width; i++)
    {
        uint8_t blue  = src[(i * 3U) + 0];
        uint8_t green = src[(i * 3U) + 1];
        uint8_t red   = src[(i * 3U) + 2];

        dst[i] =
            ((red & 0xF8) << 8) |
            ((green & 0xFC) << 3) |
            (blue >> 3);
    }
}

/* --------------------------------------------------------------------------
 * Display Functions
 * -------------------------------------------------------------------------- */

esp_err_t display_draw_bmp(
    int x,
    int y,
    const char *filename)
{
    if (filename == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;

    FILE *fp = NULL;

    bmp_file_header_t file_header;
    bmp_info_header_t info_header;

    uint32_t row_size;

    uint8_t *row_buffer = NULL;
    uint16_t *rgb565_buffer = NULL;

    /* Open file */
    ret = bmp_open(&fp, filename);
    if (ret != ESP_OK)
    {
        return ret;
    }

    /* Read headers */
    ret = bmp_read_file_header(fp, &file_header);
    if (ret != ESP_OK)
    {
        bmp_close(fp);
        return ret;
    }

    ret = bmp_read_info_header(fp, &info_header);
    if (ret != ESP_OK)
    {
        bmp_close(fp);
        return ret;
    }

    /* Validate BMP */
    ret = bmp_validate(&file_header, &info_header);
    if (ret != ESP_OK)
    {
        bmp_close(fp);
        return ret;
    }

    /* Move to pixel data */
    ret = bmp_seek_pixel_data(fp, &file_header);
    if (ret != ESP_OK)
    {
        bmp_close(fp);
        return ret;
    }

    row_size = bmp_get_row_size(info_header.width);

    row_buffer = malloc(row_size);

    if (row_buffer == NULL)
    {
        bmp_close(fp);
        return ESP_ERR_NO_MEM;
    }

    rgb565_buffer = malloc(info_header.width * sizeof(uint16_t));

    if (rgb565_buffer == NULL)
    {
        free(row_buffer);
        bmp_close(fp);
        return ESP_ERR_NO_MEM;
    }

    /*
     * Standard BMP files are stored bottom-to-top.
     * Therefore the first row read belongs to the bottom
     * of the image.
     */
    for (int32_t row = info_header.height - 1;
         row >= 0;
         row--)
    {
        ret = bmp_read_row(
            fp,
            row_buffer,
            row_size);

        if (ret != ESP_OK)
        {
            free(rgb565_buffer);
            free(row_buffer);
            bmp_close(fp);
            return ret;
        }

        bmp_rgb888_to_rgb565(
            row_buffer,
            rgb565_buffer,
            info_header.width);

        ret = display_push_image(
            x,
            y + row,
            info_header.width,
            1,
            rgb565_buffer);

        if (ret != ESP_OK)
        {
            free(rgb565_buffer);
            free(row_buffer);
            bmp_close(fp);
            return ret;
        }
    }

    free(rgb565_buffer);
    free(row_buffer);

    bmp_close(fp);

    return ESP_OK;
}