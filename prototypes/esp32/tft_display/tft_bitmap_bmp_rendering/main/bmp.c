#include "bmp.h"
#include "display.h"

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
    printf("fopen() SUCCESS\n");
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

    if (fread(&file_header->signature, sizeof(uint16_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&file_header->file_size, sizeof(uint32_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&file_header->reserved1, sizeof(uint16_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&file_header->reserved2, sizeof(uint16_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&file_header->pixel_data_offset, sizeof(uint32_t), 1, fp) != 1)
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

    if (fread(&info_header->header_size, sizeof(uint32_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->width, sizeof(int32_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->height, sizeof(int32_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->planes, sizeof(uint16_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->bits_per_pixel, sizeof(uint16_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->compression, sizeof(uint32_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->image_size, sizeof(uint32_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->x_pixels_per_meter, sizeof(int32_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->y_pixels_per_meter, sizeof(int32_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->colors_used, sizeof(uint32_t), 1, fp) != 1)
    {
        return ESP_FAIL;
    }

    if (fread(&info_header->important_colors, sizeof(uint32_t), 1, fp) != 1)
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

uint32_t bmp_get_row_size(uint32_t width)
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

esp_err_t bmp_read_row(FILE *fp, uint8_t *row_buffer, uint32_t row_size)
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

void bmp_rgb888_to_rgb565(const uint8_t *src, uint16_t *dst, uint32_t width)
{
    if ((src == NULL) || (dst == NULL))
    {
        return;
    }

    for (uint32_t i = 0; i < width; i++)
    {
        uint8_t blue = src[(i * 3U) + 0];
        uint8_t green = src[(i * 3U) + 1];
        uint8_t red = src[(i * 3U) + 2];

        dst[i] =
            ((red & 0xF8) << 8) |
            ((green & 0xFC) << 3) |
            (blue >> 3);
    }
}
esp_err_t display_draw_bmp(int x, int y, const char *filename)
{

    printf("Entered display_draw_bmp()\n");

    if (filename == NULL)
    {
        printf("Error: filename is NULL\n");
        return ESP_ERR_INVALID_ARG;
    }

    printf("Opening BMP: %s\n", filename);

    esp_err_t ret;

    FILE *fp = NULL;

    bmp_file_header_t file_header;
    bmp_info_header_t info_header;

    uint32_t row_size;

    uint8_t *row_buffer = NULL;
    uint16_t *rgb565_buffer = NULL;

    /* Open file */
    ret = bmp_open(&fp, filename);
    printf("bmp_open() = %d\n", ret);

    if (ret != ESP_OK)
    {
        printf("Failed to open BMP file\n");
        return ret;
    }

    /* Read File Header */
    ret = bmp_read_file_header(fp, &file_header);
    printf("bmp_read_file_header() = %d\n", ret);

    if (ret != ESP_OK)
    {
        bmp_close(fp);
        return ret;
    }

    /* Read Info Header */
    ret = bmp_read_info_header(fp, &info_header);
    printf("bmp_read_info_header() = %d\n", ret);

    if (ret != ESP_OK)
    {
        bmp_close(fp);
        return ret;
    }

    printf("\n----- FILE HEADER -----\n");
    printf("Signature   : 0x%04X\n", file_header.signature);
    printf("File Size   : %lu\n", file_header.file_size);
    printf("Pixel Offset: %lu\n", file_header.pixel_data_offset);

    printf("\n----- INFO HEADER -----\n");
    printf("Header Size : %lu\n", info_header.header_size);
    printf("Width       : %ld\n", info_header.width);
    printf("Height      : %ld\n", info_header.height);
    printf("Planes      : %d\n", info_header.planes);
    printf("BPP         : %d\n", info_header.bits_per_pixel);
    printf("Compression : %lu\n", info_header.compression);
    printf("Image Size  : %lu\n", info_header.image_size);

    /* Validate */
    ret = bmp_validate(&file_header, &info_header);
    printf("bmp_validate() = %d\n", ret);

    if (ret != ESP_OK) 
    {
        bmp_close(fp);
        return ret;
    }

    /* Seek Pixel Data */
    ret = bmp_seek_pixel_data(fp, &file_header);
    printf("bmp_seek_pixel_data() = %d\n", ret);

    if (ret != ESP_OK)
    {
        bmp_close(fp);
        return ret;
    }

    row_size = bmp_get_row_size(info_header.width);

    printf("Row Size = %lu\n", row_size);

    row_buffer = malloc(row_size);

    if (row_buffer == NULL)
    {
        printf("Failed to allocate row buffer\n");
        bmp_close(fp);
        return ESP_ERR_NO_MEM;
    }

    rgb565_buffer = malloc(info_header.width * sizeof(uint16_t));

    if (rgb565_buffer == NULL)
    {
        printf("Failed to allocate RGB565 buffer\n");

        free(row_buffer);
        bmp_close(fp);
        return ESP_ERR_NO_MEM;
    }

    printf("Starting image drawing...\n");

    for (int32_t row = 0; row < info_header.height; row++)
    {
        ret = bmp_read_row(fp, row_buffer, row_size);

        if (row == 0)
        {
            printf("First 5 pixels (B G R):\n");

            for (int i = 0; i < 5; i++)
            {
                printf("%d: B=%3d G=%3d R=%3d\n",
                       i,
                       row_buffer[i * 3 + 0],
                       row_buffer[i * 3 + 1],
                       row_buffer[i * 3 + 2]);
            }
        }

        if (ret != ESP_OK)
        {
            printf("Failed to read row %ld\n", row);

            free(rgb565_buffer);
            free(row_buffer);
            bmp_close(fp);

            return ret;
        }

        bmp_rgb888_to_rgb565(row_buffer, rgb565_buffer, info_header.width);

        // // /* Reverse one row horizontally */
        // for (int i = 0; i < info_header.width / 2; i++)
        // {
        //     uint16_t temp = rgb565_buffer[i];
        //     rgb565_buffer[i] = rgb565_buffer[info_header.width - 1 - i];
        //     rgb565_buffer[info_header.width - 1 - i] = temp;
        // }
        for (int i = 0; i < info_header.width; i++)
        {
            rgb565_buffer[i] = rgb_to_panel(rgb565_buffer[i]);
        }

        // /* Draw bottom-up BMP correctly on the display */
        display_push_image(x, y + (row), info_header.width, 1, rgb565_buffer);
        /* Draw bottom-up BMP correctly on the display */
        //  display_push_image(x, y + (info_header.height - 1 - row), info_header.width, 1, rgb565_buffer);

        if ((row % 20) == 0)
        {
            printf("Drawn Row : %ld\n", row);
        }

        if (row < 3)
        {
            printf("Display Push: X=%d Y=%ld Width=%ld Height=1\n", x, y + (info_header.height - 1 - row), info_header.width);
            printf("Display Push: X=%d Y=%ld Width=%ld Height=1\n", x, y + row, info_header.width);
        }
    }
    printf("BMP Drawing Complete\n");

    free(rgb565_buffer);
    free(row_buffer);

    bmp_close(fp);

    return ESP_OK;
}