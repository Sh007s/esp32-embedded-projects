#include "tft.h"
#include "jpeg_decoder.h"
#include "esp_timer.h"
#include "tft.h"
#include "esp_heap_caps.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "TFT";

// ============================================================================
// BikeVision PHASE 2 PROFILING (temporary, measurement only).
// While s_prof.on is true (only during a JPEG-from-RAM draw), the time spent in
// each part of the draw is added up. One summary line per frame is printed
// with tag "TFT_PROF". Remove after the speed-up work.
// ============================================================================
struct TFTProf
{
    bool on;
    uint32_t blocks;
    int64_t cb_us;      // whole block callback
    int64_t conv_us;    // RGB888 -> RGB565 in the callback
    int64_t cbmalloc_us;// malloc/free of the block buffer in the callback
    int64_t addr_us;    // setAddrWindow()
    int64_t wpmalloc_us;// malloc/free inside writePixels()
    int64_t swap_us;    // byte packing loop inside writePixels()
    int64_t spi_us;     // writeBytes() SPI transfer inside writePixels()
};
static TFTProf s_prof = {};

#define ILI9341_MADCTL_MY 0x80
#define ILI9341_MADCTL_MX 0x40
#define ILI9341_MADCTL_MV 0x20
#define ILI9341_MADCTL_BGR 0x08

struct RotationData
{
    uint8_t madctl;
    uint16_t width;
    uint16_t height;
};

static const RotationData ili9341_rotations[4] = {
    {static_cast<uint8_t>(ILI9341_MADCTL_MX | ILI9341_MADCTL_BGR), TFT_WIDTH, TFT_HEIGHT},
    {static_cast<uint8_t>(ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR), TFT_HEIGHT, TFT_WIDTH},
    {static_cast<uint8_t>(ILI9341_MADCTL_MY | ILI9341_MADCTL_BGR), TFT_WIDTH, TFT_HEIGHT},
    {static_cast<uint8_t>(ILI9341_MADCTL_MX | ILI9341_MADCTL_MY |
                          ILI9341_MADCTL_MV | ILI9341_MADCTL_BGR),
     TFT_HEIGHT, TFT_WIDTH}};

TFT::TFT(uint8_t tft_id) : _id(tft_id)
{
    _freq = 50000000;
}

TFT::~TFT()
{
    if (_spi)
    {
        spi_bus_remove_device(_spi);
        _spi = nullptr;
    }
    spi_bus_free(_host);
}

void TFT::begin(uint8_t CS, uint8_t DC, uint8_t MOSI, uint8_t MISO,
                uint8_t SCK, uint8_t RST, uint8_t BL)
{
    _cs = CS;
    _dc = DC;
    _mosi = MOSI;
    _miso = MISO;
    _sck = SCK;
    _rst = RST;
    _bl = (BL == 255) ? -1 : BL;

    gpio_config_t io = {};
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask =
        (1ULL << _dc) |
        (1ULL << _cs) |
        (1ULL << _rst);

    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;

    ESP_ERROR_CHECK(gpio_config(&io));

    gpio_set_level(
        static_cast<gpio_num_t>(_dc), 1);

    gpio_set_level(
        static_cast<gpio_num_t>(_cs), 1);

    gpio_set_level(
        static_cast<gpio_num_t>(_rst), 1);

    if (_bl >= 0)
    {
        gpio_set_direction(
            static_cast<gpio_num_t>(_bl),
            GPIO_MODE_OUTPUT);

        gpio_set_level(
            static_cast<gpio_num_t>(_bl), 1);
    }

    spi_bus_config_t buscfg = {};

    buscfg.mosi_io_num = _mosi;
    buscfg.miso_io_num = _miso;
    buscfg.sclk_io_num = _sck;

    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;

    buscfg.max_transfer_sz =
        TFT_WIDTH * TFT_HEIGHT * 2;

    ESP_ERROR_CHECK(
        spi_bus_initialize(
            _host,
            &buscfg,
            SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {};

    devcfg.clock_speed_hz = static_cast<int>(_freq);

    devcfg.mode = 0;

    // CS is controlled manually.
    devcfg.spics_io_num = -1;

    devcfg.queue_size = 1;

    /*
     * Allow operation above the normal
     * full-duplex frequency limitation.
     */
    devcfg.flags = SPI_DEVICE_NO_DUMMY;

    ESP_LOGI(
        TAG,
        "Adding TFT SPI device at %lu Hz",
        (unsigned long)_freq);

    ESP_ERROR_CHECK(
        spi_bus_add_device(
            _host,
            &devcfg,
            &_spi));

    hwReset();

    init();

    setRotation(0);

    ESP_LOGI(
        TAG,
        "ILI9341 initialized: %dx%d, SPI=%lu Hz",
        _width,
        _height,
        (unsigned long)_freq);
}

void TFT::hwReset()
{
    gpio_set_level(static_cast<gpio_num_t>(_rst), 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(static_cast<gpio_num_t>(_rst), 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(_rst), 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

void TFT::init()
{
    // Exact ILI9341 initialization sequence from the supplied Arduino driver.
    startWrite();

    writeCommand(0xCB);
    writeByte(0x39);
    writeByte(0x2C);
    writeByte(0x00);
    writeByte(0x34);
    writeByte(0x02);

    writeCommand(0xCF);
    writeByte(0x00);
    writeByte(0xC1);
    writeByte(0x30);

    writeCommand(0xE8);
    writeByte(0x85);
    writeByte(0x00);
    writeByte(0x78);

    writeCommand(0xEA);
    writeByte(0x00);
    writeByte(0x00);

    writeCommand(0xED);
    writeByte(0x64);
    writeByte(0x03);
    writeByte(0x12);
    writeByte(0x81);

    writeCommand(0xF7);
    writeByte(0x20);

    writeCommand(0xC0);
    writeByte(0x23);
    writeCommand(0xC1);
    writeByte(0x10);

    writeCommand(0xC5);
    writeByte(0x3E);
    writeByte(0x28);
    writeCommand(0xC7);
    writeByte(0x86);

    writeCommand(0x36);
    writeByte(0x48);
    writeCommand(0x3A);
    writeByte(0x55);

    writeCommand(0xB1);
    writeByte(0x00);
    writeByte(0x18);

    writeCommand(0xB6);
    writeByte(0x08);
    writeByte(0x82);
    writeByte(0x27);

    writeCommand(0xF2);
    writeByte(0x00);

    writeCommand(0x2A);
    writeByte(0x00);
    writeByte(0x00);
    writeByte(0x00);
    writeByte(0xEF);

    writeCommand(0x2B);
    writeByte(0x00);
    writeByte(0x00);
    writeByte(0x01);
    writeByte(0x3F);

    writeCommand(0x26);
    writeByte(0x01);

    const uint8_t gammaPos[] = {
        0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
        0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
    writeCommand(0xE0);
    writeBytes(gammaPos, sizeof(gammaPos));

    const uint8_t gammaNeg[] = {
        0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
        0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
    writeCommand(0xE1);
    writeBytes(gammaNeg, sizeof(gammaNeg));

    writeCommand(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    writeCommand(0x2C);

    writeCommand(0x29);
    writeCommand(0x2C);

    endWrite();
}

void TFT::setFrequency(uint32_t frequency)
{
    static const char *TAG = "TFT_SPI";

    ESP_LOGI(
        TAG,
        "setFrequency: requested=%lu Hz, current=%lu Hz",
        (unsigned long)frequency,
        (unsigned long)_freq);

    if (frequency > 80000000UL)
    {
        ESP_LOGW(
            TAG,
            "setFrequency: limiting %lu -> 80000000 Hz",
            (unsigned long)frequency);

        frequency = 80000000UL;
    }

    /*
     * Nothing to do if the SPI device is already
     * configured at the requested frequency.
     */
    if (_freq == frequency && _spi != nullptr)
    {
        ESP_LOGI(
            TAG,
            "setFrequency: already at %lu Hz",
            (unsigned long)frequency);

        return;
    }

    _freq = frequency;

    /*
     * Re-create the SPI device with the new
     * clock frequency.
     */
    if (_spi != nullptr)
    {
        ESP_LOGI(
            TAG,
            "setFrequency: removing SPI device");

        ESP_ERROR_CHECK(
            spi_bus_remove_device(_spi));

        _spi = nullptr;
    }

    spi_device_interface_config_t devcfg = {};

    devcfg.clock_speed_hz =
        static_cast<int>(_freq);

    devcfg.mode = 0;

    // CS is controlled manually.
    devcfg.spics_io_num = -1;

    devcfg.queue_size = 1;

    /*
     * IMPORTANT:
     * Must match begin().
     */
    devcfg.flags = SPI_DEVICE_NO_DUMMY;

    ESP_LOGI(
        TAG,
        "setFrequency: adding SPI device at %lu Hz",
        (unsigned long)_freq);

    esp_err_t err =
        spi_bus_add_device(
            _host,
            &devcfg,
            &_spi);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "setFrequency: spi_bus_add_device FAILED: %s",
            esp_err_to_name(err));

        _spi = nullptr;
        return;
    }

    ESP_LOGI(
        TAG,
        "setFrequency: SPI device now running at %lu Hz",
        (unsigned long)_freq);
}

void TFT::startWrite()
{
    if (!_spi)
        return;

    if (!_busAcquired)
    {
        ESP_ERROR_CHECK(spi_device_acquire_bus(_spi, portMAX_DELAY));
        _busAcquired = true;
    }
    gpio_set_level(static_cast<gpio_num_t>(_cs), 0);
}

void TFT::endWrite()
{
    if (!_spi)
        return;

    gpio_set_level(static_cast<gpio_num_t>(_cs), 1);

    if (_busAcquired)
    {
        spi_device_release_bus(_spi);
        _busAcquired = false;
    }
}

void TFT::writeCommand(uint8_t command)
{
    gpio_set_level(static_cast<gpio_num_t>(_dc), 0);
    writeByte(command);
    gpio_set_level(static_cast<gpio_num_t>(_dc), 1);
}

// BikeVision C2: polling transmit. spi_device_transmit() waits for an
// interrupt and a task switch on every transfer; for the many tiny
// command/address transfers that overhead is bigger than the transfer
// itself. Polling busy-waits instead. Safe: startWrite() holds the bus.
void TFT::writeByte(uint8_t value)
{
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &value;
    ESP_ERROR_CHECK(spi_device_polling_transmit(_spi, &t));
}

void TFT::writeBytes(const uint8_t *data, size_t len)
{
    if (!data || !len)
        return;

    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(_spi, &t));
}

uint8_t TFT::transferByte(uint8_t value)
{
    uint8_t rx = 0;
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &value;
    t.rx_buffer = &rx;
    ESP_ERROR_CHECK(spi_device_transmit(_spi, &t));
    return rx;
}

void TFT::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    int64_t prof_t0 = s_prof.on ? esp_timer_get_time() : 0;  // BikeVision: profiling
    struct ProfEnd { int64_t t0; ~ProfEnd() { if (t0) s_prof.addr_us += esp_timer_get_time() - t0; } } prof_end{prof_t0};

    if (w == 0 || h == 0)
        return;

    if (x >= _width ||
        y >= _height)
        return;

    uint16_t x2 = x + w - 1;
    uint16_t y2 = y + h - 1;

    if (x2 >= _width)
        x2 = _width - 1;

    if (y2 >= _height)
        y2 = _height - 1;

    // BikeVision C1: send each 4-byte address as ONE transfer
    // (was 4 single-byte transfers): 5 transfers per window instead of 11.
    const uint8_t xs[4] = {
        static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xFF),
        static_cast<uint8_t>(x2 >> 8), static_cast<uint8_t>(x2 & 0xFF)};
    const uint8_t ys[4] = {
        static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF),
        static_cast<uint8_t>(y2 >> 8), static_cast<uint8_t>(y2 & 0xFF)};

    writeCommand(ILI9341_CASET);
    writeBytes(xs, 4);

    writeCommand(ILI9341_RASET);
    writeBytes(ys, 4);

    writeCommand(ILI9341_RAMWR);
}

void TFT::writePixel(uint16_t color)
{
    uint8_t data[2] = {
        static_cast<uint8_t>(color >> 8),
        static_cast<uint8_t>(color & 0xFF)};
    writeBytes(data, 2);
}

void TFT::writePixels(const uint16_t *colors, uint32_t len)
{
    if (!colors || len == 0)
        return;

    constexpr uint32_t PIXELS_PER_TRANSFER = 2048;

    int64_t prof_t = s_prof.on ? esp_timer_get_time() : 0;  // BikeVision: profiling
    uint8_t *buf =
        static_cast<uint8_t *>(
            malloc(PIXELS_PER_TRANSFER * 2));
    if (prof_t)
        s_prof.wpmalloc_us += esp_timer_get_time() - prof_t;

    if (!buf)
    {
        ESP_LOGE("TFT",
                 "writePixels: buffer allocation failed");
        return;
    }

    while (len > 0)
    {
        uint32_t n =
            std::min<uint32_t>(
                len,
                PIXELS_PER_TRANSFER);

        uint32_t i = 0;
        int64_t prof_s = s_prof.on ? esp_timer_get_time() : 0;  // BikeVision: profiling

        /*
         * Process two RGB565 pixels at a time.
         *
         * Output:
         *
         * pixel0 HIGH
         * pixel0 LOW
         * pixel1 HIGH
         * pixel1 LOW
         */
        for (; i + 1 < n; i += 2)
        {
            const uint16_t p0 = colors[i];
            const uint16_t p1 = colors[i + 1];

            const uint32_t packed =
                (static_cast<uint32_t>(p0) << 16) |
                p1;

            const uint32_t out =
                i * 2;

            buf[out + 0] =
                static_cast<uint8_t>(packed >> 24);

            buf[out + 1] =
                static_cast<uint8_t>(packed >> 16);

            buf[out + 2] =
                static_cast<uint8_t>(packed >> 8);

            buf[out + 3] =
                static_cast<uint8_t>(packed);
        }

        /*
         * Handle one remaining pixel.
         */
        if (i < n)
        {
            const uint16_t color = colors[i];

            buf[i * 2] =
                static_cast<uint8_t>(color >> 8);

            buf[i * 2 + 1] =
                static_cast<uint8_t>(color);
        }

        int64_t prof_w = s_prof.on ? esp_timer_get_time() : 0;  // BikeVision: profiling
        if (prof_s)
            s_prof.swap_us += prof_w - prof_s;
        writeBytes(buf, n * 2);
        if (prof_w)
            s_prof.spi_us += esp_timer_get_time() - prof_w;

        colors += n;
        len -= n;
    }

    prof_t = s_prof.on ? esp_timer_get_time() : 0;  // BikeVision: profiling
    free(buf);
    if (prof_t)
        s_prof.wpmalloc_us += esp_timer_get_time() - prof_t;
}

void TFT::writeColor(uint16_t color, uint32_t len)
{
    uint16_t temp[64];
    for (auto &v : temp)
        v = color;

    while (len)
    {
        uint32_t n = std::min<uint32_t>(len, 64);
        writePixels(temp, n);
        len -= n;
    }
}

uint16_t TFT::color565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(
        ((r & 0xF8) << 8) |
        ((g & 0xFC) << 3) |
        ((b & 0xF8) >> 3));
}

void TFT::setRotation(uint8_t rotation)
{
    _rotation = rotation & 3;
    _width = ili9341_rotations[_rotation].width;
    _height = ili9341_rotations[_rotation].height;

    startWrite();
    writeCommand(ILI9341_MADCTL);
    writeByte(ili9341_rotations[_rotation].madctl);
    endWrite();
}

uint8_t TFT::getRotation() const { return _rotation; }
int16_t TFT::width() const { return _width; }
int16_t TFT::height() const { return _height; }

void TFT::invertDisplay(bool invert)
{
    startWrite();
    writeCommand(invert ? ILI9341_INVON : ILI9341_INVOFF);
    endWrite();
}

void TFT::scrollTo(uint16_t y)
{
    startWrite();
    writeCommand(ILI9341_VSCRSADD);
    writeByte(y >> 8);
    writeByte(y & 0xFF);
    endWrite();
}

void TFT::pushColor(uint16_t color)
{
    startWrite();
    writePixel(color);
    endWrite();
}

void TFT::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= _width || y >= _height)
        return;

    startWrite();
    setAddrWindow(x, y, 1, 1);
    writePixel(color);
    endWrite();
}

void TFT::writePixelXY(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= _width || y >= _height)
    {
        return;
    }

    setAddrWindow((uint16_t)x, (uint16_t)y, 1, 1);

    writePixel(color);
}

void TFT::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (w <= 0 || h <= 0 || x >= _width || y >= _height)
        return;

    int16_t x2 = x + w - 1;
    int16_t y2 = y + h - 1;

    if (x2 < 0 || y2 < 0)
        return;

    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > _width)
        w = _width - x;
    if (y + h > _height)
        h = _height - y;

    if (w <= 0 || h <= 0)
        return;

    setAddrWindow(x, y, w, h);
    writeColor(color, static_cast<uint32_t>(w) * h);
}

void TFT::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    startWrite();
    writeFillRect(x, y, w, h, color);
    endWrite();
}

void TFT::fillScreen(uint16_t color)
{
    fillRect(0, 0, _width, _height, color);
}

void TFT::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    startWrite();
    writeFillRect(x, y, w, 1, color);
    endWrite();
}

void TFT::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    startWrite();
    writeFillRect(x, y, 1, h, color);
    endWrite();
}

void TFT::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (w <= 0 || h <= 0)
        return;
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void TFT::drawLine(int16_t x0, int16_t y0,
                   int16_t x1, int16_t y1,
                   uint16_t color)
{
    // Horizontal line
    if (y0 == y1)
    {
        if (x1 < x0)
        {
            int16_t temp = x0;
            x0 = x1;
            x1 = temp;
        }

        drawFastHLine(x0, y0, x1 - x0 + 1, color);
        return;
    }

    // Vertical line
    if (x0 == x1)
    {
        if (y1 < y0)
        {
            int16_t temp = y0;
            y0 = y1;
            y1 = temp;
        }

        drawFastVLine(x0, y0, y1 - y0 + 1, color);
        return;
    }

    // Bresenham line algorithm
    int16_t dx = std::abs(x1 - x0);
    int16_t sx = (x0 < x1) ? 1 : -1;

    int16_t dy = -std::abs(y1 - y0);
    int16_t sy = (y0 < y1) ? 1 : -1;

    int16_t err = dx + dy;

    uint32_t pixelCount = 0;

    startWrite();

    while (true)
    {
        writePixelXY(x0, y0, color);

        pixelCount++;

        // Allow FreeRTOS idle task to run
        if ((pixelCount & 0x1F) == 0)
        {
            vTaskDelay(1);
        }

        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        int16_t e2 = 2 * err;

        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }

    endWrite();
}

void TFT::drawCircle(int16_t x0, int16_t y0,
                     int16_t r, uint16_t color)
{
    if (r <= 0)
        return;

    int16_t x = -r;
    int16_t y = 0;
    int16_t err = 2 - 2 * r;

    uint32_t pixelCount = 0;

    startWrite();

    do
    {
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 - y, y0 - x, color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 + y, y0 + x, color);

        pixelCount += 4;

        // Allow FreeRTOS idle task to run
        if ((pixelCount & 0x1F) == 0)
        {
            vTaskDelay(1);
        }

        r = err;

        if (r <= y)
            err += ++y * 2 + 1;

        if (r > x || err > y)
            err += ++x * 2 + 1;

    } while (x < 0);

    endWrite();
}

void TFT::fillCircle(int16_t x0, int16_t y0, uint16_t r, uint16_t color)
{
    drawFastVLine(x0, y0 - r, 2 * r + 1, color);

    int16_t f = 1 - static_cast<int16_t>(r);
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * static_cast<int16_t>(r);
    int16_t x = 0;
    int16_t y = r;

    while (x < y)
    {
        if (f >= 0)
        {
            --y;
            ddF_y += 2;
            f += ddF_y;
        }
        ++x;
        ddF_x += 2;
        f += ddF_x;

        drawFastVLine(x0 + x, y0 - y, 2 * y + 1, color);
        drawFastVLine(x0 - x, y0 - y, 2 * y + 1, color);
        drawFastVLine(x0 + y, y0 - x, 2 * x + 1, color);
        drawFastVLine(x0 - y, y0 - x, 2 * x + 1, color);
    }
}

void TFT::drawTriangle(int16_t x0, int16_t y0,
                       int16_t x1, int16_t y1,
                       int16_t x2, int16_t y2,
                       uint16_t color)
{
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

void TFT::fillTriangle(int16_t x0, int16_t y0,
                       int16_t x1, int16_t y1,
                       int16_t x2, int16_t y2,
                       uint16_t color)
{
    if (y0 > y1)
    {
        std::swap(y0, y1);
        std::swap(x0, x1);
    }
    if (y1 > y2)
    {
        std::swap(y1, y2);
        std::swap(x1, x2);
    }
    if (y0 > y1)
    {
        std::swap(y0, y1);
        std::swap(x0, x1);
    }

    if (y0 == y2)
    {
        int16_t a = std::min({x0, x1, x2});
        int16_t b = std::max({x0, x1, x2});
        drawFastHLine(a, y0, b - a + 1, color);
        return;
    }

    int32_t dx01 = x1 - x0, dy01 = y1 - y0;
    int32_t dx02 = x2 - x0, dy02 = y2 - y0;
    int32_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;

    int16_t y, last = (y1 == y2) ? y1 : y1 - 1;

    for (y = y0; y <= last; y++)
    {
        int16_t a = x0 + sa / dy01;
        int16_t b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        if (a > b)
            std::swap(a, b);
        drawFastHLine(a, y, b - a + 1, color);
    }

    sa = dx12 * (y - y1);
    sb = dx02 * (y - y0);

    for (; y <= y2; y++)
    {
        int16_t a = x1 + sa / dy12;
        int16_t b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        if (a > b)
            std::swap(a, b);
        drawFastHLine(a, y, b - a + 1, color);
    }
}

uint8_t TFT::readcommand8(uint8_t command, uint8_t index)
{
    static const char *TAG = "TFT_SPI";

    ESP_LOGI(TAG,
             "readcommand8: command=0x%02X index=%u current_freq=%lu Hz",
             command,
             index,
             (unsigned long)_freq);

    uint32_t oldFreq = _freq;

    if (_freq > 24000000UL)
    {
        ESP_LOGI(TAG,
                 "readcommand8: reducing SPI frequency %lu -> 24000000 Hz",
                 (unsigned long)_freq);

        setFrequency(24000000UL);
    }

    ESP_LOGI(TAG,
             "readcommand8: starting read transaction at %lu Hz",
             (unsigned long)_freq);

    startWrite();

    writeCommand(0xD9);
    writeByte(0x10 + index);

    gpio_set_level(
        static_cast<gpio_num_t>(_dc), 0);

    writeByte(command);

    gpio_set_level(
        static_cast<gpio_num_t>(_dc), 1);

    uint8_t value =
        transferByte(0x00);

    ESP_LOGI(TAG,
             "readcommand8: received value=0x%02X",
             value);

    endWrite();

    if (_freq != oldFreq)
    {
        ESP_LOGI(TAG,
                 "readcommand8: restoring SPI frequency %lu -> %lu Hz",
                 (unsigned long)_freq,
                 (unsigned long)oldFreq);

        setFrequency(oldFreq);
    }

    ESP_LOGI(TAG,
             "readcommand8: complete, SPI frequency=%lu Hz",
             (unsigned long)_freq);

    return value;
}
// ============================================================================
// Additional complete Arduino-driver-compatible functionality
// ============================================================================

static uint16_t rgb565_from_rgb888(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

bool TFT::TFTFile::open(const char *path)
{
    fp = fopen(path, "rb");
    return fp != nullptr;
}

size_t TFT::TFTFile::size()
{
    if (!fp)
        return 0;
    long p = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, p, SEEK_SET);
    return n > 0 ? (size_t)n : 0;
}

size_t TFT::TFTFile::read(void *buf, size_t len) { return fp ? fread(buf, 1, len, fp) : 0; }
int TFT::TFTFile::readByte() { return fp ? fgetc(fp) : EOF; }
size_t TFT::TFTFile::readBytes(char *buf, size_t len) { return read(buf, len); }
bool TFT::TFTFile::seek(size_t pos) { return fp && fseek(fp, (long)pos, SEEK_SET) == 0; }
long TFT::TFTFile::tell() { return fp ? ftell(fp) : -1; }
void TFT::TFTFile::close()
{
    if (fp)
    {
        fclose(fp);
        fp = nullptr;
    }
}

uint16_t TFT::bmpRead16(const uint8_t *b, int p) { return (uint16_t)b[p] | ((uint16_t)b[p + 1] << 8); }
uint32_t TFT::bmpRead32(const uint8_t *b, int p) { return (uint32_t)b[p] | ((uint32_t)b[p + 1] << 8) | ((uint32_t)b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24); }

void TFT::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c)
{
    if (w <= 0 || h <= 0)
        return;
    r = std::min<int16_t>(r, std::min(w, h) / 2);
    drawFastHLine(x + r, y, w - 2 * r, c);
    drawFastHLine(x + r, y + h - 1, w - 2 * r, c);
    drawFastVLine(x, y + r, h - 2 * r, c);
    drawFastVLine(x + w - 1, y + r, h - 2 * r, c);
    drawCircleHelper(x + r, y + r, r, 1, c);
    drawCircleHelper(x + w - r - 1, y + r, r, 2, c);
    drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, c);
    drawCircleHelper(x + r, y + h - r - 1, r, 8, c);
}
void TFT::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c)
{
    if (w <= 0 || h <= 0)
        return;
    r = std::min<int16_t>(r, std::min(w, h) / 2);
    fillRect(x + r, y, w - 2 * r, h, c);
    fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, c);
    fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, c);
}
void TFT::drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corner, uint16_t c)
{
    int16_t f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
    while (x < y)
    {
        if (f >= 0)
        {
            --y;
            ddy += 2;
            f += ddy;
        }
        ++x;
        ddx += 2;
        f += ddx;
        if (corner & 1)
        {
            drawPixel(x0 - x, y0 - y, c);
            drawPixel(x0 - y, y0 - x, c);
        }
        if (corner & 2)
        {
            drawPixel(x0 + x, y0 - y, c);
            drawPixel(x0 + y, y0 - x, c);
        }
        if (corner & 4)
        {
            drawPixel(x0 + x, y0 + y, c);
            drawPixel(x0 + y, y0 + x, c);
        }
        if (corner & 8)
        {
            drawPixel(x0 - x, y0 + y, c);
            drawPixel(x0 - y, y0 + x, c);
        }
    }
}
void TFT::fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corner, int16_t delta, uint16_t c)
{
    int16_t f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
    while (x < y)
    {
        if (f >= 0)
        {
            --y;
            ddy += 2;
            f += ddy;
        }
        ++x;
        ddx += 2;
        f += ddx;
        if (corner & 1)
        {
            drawFastVLine(x0 + x, y0 - y, 2 * y + 1 + delta, c);
            drawFastVLine(x0 + y, y0 - x, 2 * x + 1 + delta, c);
        }
        if (corner & 2)
        {
            drawFastVLine(x0 - x, y0 - y, 2 * y + 1 + delta, c);
            drawFastVLine(x0 - y, y0 - x, 2 * x + 1 + delta, c);
        }
    }
}

void TFT::writePixel(int16_t x, int16_t y, uint16_t color)
{
    writePixelXY(x, y, color);
}
void TFT::setCursor(uint16_t x, uint16_t y)
{
    _curX = x;
    _curY = y;
}
void TFT::setTextColor(uint16_t c) { _textColor = c; }
void TFT::setTextSize(uint8_t s) { _textSize = s ? s : 1; }
void TFT::setTextOrientation(uint8_t o) { _textOrientation = o ? 1 : 0; }
void TFT::writeChar(char c)
{
    if (c == '\n')
    {
        _curX = 0;
        _curY += 8 * _textSize;
        return;
    }
    if (c == '\r')
        return;
    if ((uint8_t)c < 32 || (uint8_t)c > 126)
        return;
    if (_textOrientation == 0)
    {
        if (_curX + 6 * _textSize > _width)
        {
            _curX = 0;
            _curY += 8 * _textSize;
        }
        if (_curY + 8 * _textSize > _height)
            return;
        const uint8_t *g = font5x7[(uint8_t)c - 32];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1u << row))
                {
                    if (_textSize == 1)
                        drawPixel(_curX + col, _curY + row, _textColor);
                    else
                        fillRect(_curX + col * _textSize, _curY + row * _textSize, _textSize, _textSize, _textColor);
                }
        _curX += 6 * _textSize;
    }
    else
    {
        if (_curY + 6 * _textSize > _height)
        {
            _curY = 0;
            _curX += 8 * _textSize;
        }
        if (_curX + 8 * _textSize > _width)
            return;
        const uint8_t *g = font5x7[(uint8_t)c - 32];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1u << row))
                {
                    if (_textSize == 1)
                        drawPixel(_curX + row, _curY + col, _textColor);
                    else
                        fillRect(_curX + row * _textSize, _curY + col * _textSize, _textSize, _textSize, _textColor);
                }
        _curY += 6 * _textSize;
    }
}
size_t TFT::write(uint8_t c)
{
    writeChar((char)c);
    return 1;
}
size_t TFT::write(const uint8_t *b, size_t n)
{
    if (!b)
        return 0;
    for (size_t i = 0; i < n; i++)
        writeChar((char)b[i]);
    return n;
}

void TFT::println()
{
    _curX = 0;
    _curY += 8 * _textSize;

    if (_curY + 8 * _textSize > _height)
    {
        _curY = 0;
    }
}

void TFT::println(const char *str)
{
    if (str == nullptr)
    {
        println();
        return;
    }

    while (*str)
    {
        write((uint8_t)*str++);
    }

    println();
}

void TFT::println(int value)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%d", value);
    println(buffer);
}

void TFT::println(unsigned int value)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%u", value);
    println(buffer);
}

void TFT::println(long value)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%ld", value);
    println(buffer);
}

void TFT::println(unsigned long value)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%lu", value);
    println(buffer);
}

void TFT::println(float value, int digits)
{
    char buffer[64];

    snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    println(buffer);
}

void TFT::println(double value, int digits)
{
    char buffer[64];

    snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    println(buffer);
}
void TFT::println(unsigned int value, int base)
{
    char buffer[32];

    if (base == 16)
    {
        snprintf(buffer, sizeof(buffer), "%X", value);
    }
    else if (base == 10)
    {
        snprintf(buffer, sizeof(buffer), "%u", value);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%u", value);
    }

    println(buffer);
}
void TFT::println(unsigned long value, int base)
{
    char buffer[32];

    if (base == 16)
    {
        snprintf(buffer, sizeof(buffer), "%lX", value);
    }
    else if (base == 10)
    {
        snprintf(buffer, sizeof(buffer), "%lu", value);
    }
    else
    {
        // Basic fallback
        snprintf(buffer, sizeof(buffer), "%lu", value);
    }

    println(buffer);
}
void TFT::writeText(const char *s)
{
    if (!s)
        return;
    while (*s)
        writeChar(*s++);
}
void TFT::writeText(const uint8_t *s)
{
    if (!s)
        return;
    while (*s)
        writeChar((char)*s++);
}

bool TFT::drawRGB565(const uint16_t *p, uint16_t w, uint16_t h, int16_t x, int16_t y)
{
    if (!p || !w || !h || x < 0 || y < 0 || x + w > _width || y + h > _height)
        return false;
    startWrite();
    setAddrWindow(x, y, w, h);
    writePixels(p, (uint32_t)w * h);
    endWrite();
    return true;
}
bool TFT::drawRGB565Scaled(const uint16_t *p, uint16_t w, uint16_t h, uint16_t dw, uint16_t dh, int16_t x, int16_t y)
{
    if (!p || !w || !h || !dw || !dh || x < 0 || y < 0 || x + dw > _width || y + dh > _height)
        return false;
    uint16_t *line = (uint16_t *)malloc(dw * sizeof(uint16_t));
    if (!line)
        return false;
    startWrite();
    setAddrWindow(x, y, dw, dh);
    for (uint16_t dy = 0; dy < dh; dy++)
    {
        uint16_t sy = (uint32_t)dy * h / dh;
        for (uint16_t dx = 0; dx < dw; dx++)
            line[dx] = p[(uint32_t)sy * w + ((uint32_t)dx * w / dw)];
        writePixels(line, dw);
    }
    endWrite();
    free(line);
    return true;
}

bool TFT::drawBmpFile(const char *path,
                      uint16_t x,
                      uint16_t y,
                      uint16_t maxW,
                      uint16_t maxH,
                      uint16_t offX,
                      uint16_t offY)
{
    TFTFile f;

    if (!f.open(path))
        return false;

    // ============================================================
    // BMP HEADER
    // ============================================================

    uint8_t h[54];

    if (f.read(h, 54) != 54 ||
        h[0] != 'B' ||
        h[1] != 'M')
    {
        f.close();
        return false;
    }

    uint32_t dataOffset = bmpRead32(h, 10);

    int32_t bw = (int32_t)bmpRead32(h, 18);
    int32_t bh = (int32_t)bmpRead32(h, 22);

    uint16_t bpp = bmpRead16(h, 28);

    // ============================================================
    // Supported formats:
    //
    // 24-bit BMP
    // 8-bit BMP with palette
    // ============================================================

    if (bw <= 0 ||
        bh == 0 ||
        (bpp != 24 && bpp != 8))
    {
        f.close();
        return false;
    }

    uint32_t aw = (uint32_t)std::abs(bw);
    uint32_t ah = (uint32_t)std::abs(bh);

    // ============================================================
    // COORDINATE CHECK
    // ============================================================

    if (offX >= aw ||
        offY >= ah)
    {
        f.close();
        return false;
    }

    if (!maxW)
        maxW = _width - x;

    if (!maxH)
        maxH = _height - y;

    uint16_t ow =
        (uint16_t)std::min<uint32_t>(
            aw - offX,
            maxW);

    uint16_t oh =
        (uint16_t)std::min<uint32_t>(
            ah - offY,
            maxH);

    if (!ow ||
        !oh ||
        x + ow > _width ||
        y + oh > _height)
    {
        f.close();
        return false;
    }

    // ============================================================
    // 8-BIT BMP PALETTE
    //
    // Standard BMP palette:
    //
    // Each entry = 4 bytes
    //
    // B G R 0
    //
    // Normally 256 entries.
    // ============================================================

    uint8_t palette[256][4];

    if (bpp == 8)
    {
        uint32_t paletteOffset = 54;

        if (!f.seek(paletteOffset))
        {
            f.close();
            return false;
        }

        if (f.read(palette, sizeof(palette)) !=
            sizeof(palette))
        {
            f.close();
            return false;
        }

        // Move to pixel data.
        if (!f.seek(dataOffset))
        {
            f.close();
            return false;
        }
    }
    else
    {
        // 24-bit BMP directly starts at pixel data.
        if (!f.seek(dataOffset))
        {
            f.close();
            return false;
        }
    }

    // ============================================================
    // BMP ROW SIZE
    // ============================================================

    uint32_t rowBytes;

    if (bpp == 24)
    {
        rowBytes =
            ((aw * 3 + 3) / 4) * 4;
    }
    else
    {
        // 8-bit = 1 byte per pixel
        // BMP rows are padded to 4-byte boundary.
        rowBytes =
            ((aw + 3) / 4) * 4;
    }

    // ============================================================
    // ROW BUFFER
    // ============================================================

    uint8_t *row =
        (uint8_t *)malloc(rowBytes);

    if (!row)
    {
        f.close();
        return false;
    }

    // ============================================================
    // RGB565 OUTPUT BUFFER
    // ============================================================

    uint16_t *pix =
        (uint16_t *)malloc(
            ow * sizeof(uint16_t));

    if (!pix)
    {
        free(row);
        f.close();
        return false;
    }

    // ============================================================
    // START TFT TRANSACTION
    // ============================================================

    startWrite();

    // ============================================================
    // READ BMP ROWS SEQUENTIALLY
    // ============================================================

    for (uint32_t fileRow = 0;
         fileRow < ah;
         fileRow++)
    {
        // --------------------------------------------------------
        // Read complete row
        // --------------------------------------------------------

        if (f.read(row, rowBytes) != rowBytes)
        {
            endWrite();

            free(pix);
            free(row);
            f.close();

            return false;
        }

        // --------------------------------------------------------
        // BMP orientation
        // --------------------------------------------------------

        int32_t imageY;

        if (bh > 0)
        {
            // Bottom-up BMP
            imageY =
                (int32_t)ah -
                1 -
                (int32_t)fileRow;
        }
        else
        {
            // Top-down BMP
            imageY =
                (int32_t)fileRow;
        }

        // --------------------------------------------------------
        // Vertical crop
        // --------------------------------------------------------

        if (imageY < offY ||
            imageY >= offY + oh)
        {
            continue;
        }

        uint16_t destY =
            y +
            (uint16_t)(imageY - offY);

        // ========================================================
        // CONVERT ROW
        // ========================================================

        for (uint16_t dx = 0;
             dx < ow;
             dx++)
        {
            uint32_t sx =
                offX + dx;

            uint8_t r;
            uint8_t g;
            uint8_t b;

            // ----------------------------------------------------
            // 24-BIT BMP
            // ----------------------------------------------------

            if (bpp == 24)
            {
                const uint8_t *q =
                    &row[sx * 3];

                b = q[0];
                g = q[1];
                r = q[2];
            }

            // ----------------------------------------------------
            // 8-BIT BMP
            //
            // row[sx] = palette index
            // ----------------------------------------------------

            else
            {
                uint8_t index =
                    row[sx];

                b = palette[index][0];
                g = palette[index][1];
                r = palette[index][2];
            }

            // ----------------------------------------------------
            // RGB888 -> RGB565
            // ----------------------------------------------------

            pix[dx] =
                rgb565_from_rgb888(
                    r,
                    g,
                    b);
        }

        // ========================================================
        // SET TFT WINDOW FOR THIS ROW
        // ========================================================

        setAddrWindow(
            x,
            destY,
            ow,
            1);

        // ========================================================
        // SEND ROW
        // ========================================================

        writePixels(
            pix,
            ow);
    }

    // ============================================================
    // END
    // ============================================================

    endWrite();

    free(pix);
    free(row);

    f.close();

    return true;
}

struct TFTJPEGContext
{
    TFT *tft;

    uint16_t x;
    uint16_t y;

    uint16_t imageWidth;
    uint16_t imageHeight;

    // Rotate portrait JPEG (240x320) clockwise to TFT landscape (320x240).
    bool rotate90;
};

static int tft_jpeg_output_callback(
    const uint8_t *bitmap,
    const esp_jpeg_rect_t *rect,
    void *user_ctx)
{
    if (!bitmap ||
        !rect ||
        !user_ctx)
    {
        return 0;
    }

    TFTJPEGContext *ctx =
        (TFTJPEGContext *)user_ctx;

    TFT *tft =
        ctx->tft;

    // BikeVision: profiling (whole callback)
    int64_t prof_cb0 = s_prof.on ? esp_timer_get_time() : 0;
    struct ProfCb { int64_t t0; ~ProfCb() { if (t0) { s_prof.cb_us += esp_timer_get_time() - t0; s_prof.blocks++; } } } prof_cb{prof_cb0};

    uint16_t blockW =
        rect->right -
        rect->left +
        1;

    uint16_t blockH =
        rect->bottom -
        rect->top +
        1;

    // --------------------------------------------------------
    // Allocate only the current JPEG block
    //
    // Much smaller than 320x240 framebuffer.
    // --------------------------------------------------------

    int64_t prof_m = s_prof.on ? esp_timer_get_time() : 0;  // BikeVision: profiling
    uint16_t *rgb565 =
        (uint16_t *)malloc(
            blockW * blockH * sizeof(uint16_t));
    if (prof_m)
        s_prof.cbmalloc_us += esp_timer_get_time() - prof_m;

    if (!rgb565)
    {
        ESP_LOGE(
            "TFT_JPEG",
            "Block buffer allocation failed: %u x %u",
            blockW,
            blockH);

        return 0;
    }

    // --------------------------------------------------------
    // TJpgDec default output:
    //
    // JD_FORMAT = 0
    // RGB888
    //
    // 3 bytes per pixel:
    // R G B
    // --------------------------------------------------------

    const uint8_t *src =
        bitmap;
    int64_t prof_c = s_prof.on ? esp_timer_get_time() : 0;  // BikeVision: profiling

    // Convert the current JPEG block from RGB888 to RGB565.
    for (uint16_t row = 0;
         row < blockH;
         row++)
    {
        for (uint16_t col = 0;
             col < blockW;
             col++)
        {
            uint8_t r = *src++;
            uint8_t g = *src++;
            uint8_t b = *src++;

            rgb565[row * blockW + col] =
                rgb565_from_rgb888(r, g, b);
        }
    }

    if (prof_c)
        s_prof.conv_us += esp_timer_get_time() - prof_c;

    if (ctx->rotate90)
    {
        // Source: 240x320 portrait
        // Destination: 320x240 landscape
        // Clockwise rotation:
        //   dstX = imageHeight - 1 - srcY
        //   dstY = srcX
        //
        // The rotated block is blockH x blockW.
        uint16_t rotatedW = blockH;
        uint16_t rotatedH = blockW;

        uint16_t *rotated =
            (uint16_t *)malloc(
                (size_t)rotatedW * rotatedH * sizeof(uint16_t));

        if (!rotated)
        {
            free(rgb565);
            ESP_LOGE(
                "TFT_JPEG",
                "Rotated block allocation failed: %u x %u",
                rotatedW,
                rotatedH);
            return 0;
        }

        for (uint16_t row = 0; row < blockH; row++)
        {
            for (uint16_t col = 0; col < blockW; col++)
            {
                uint16_t dstRow = col;
                uint16_t dstCol = blockH - 1 - row;

                rotated[(size_t)dstRow * rotatedW + dstCol] =
                    rgb565[(size_t)row * blockW + col];
            }
        }

        uint16_t destX =
            ctx->x + ctx->imageHeight - 1 - rect->bottom;

        uint16_t destY =
            ctx->y + rect->left;

        if (destX + rotatedW > tft->width() ||
            destY + rotatedH > tft->height())
        {
            free(rotated);
            free(rgb565);
            ESP_LOGE(
                "TFT_JPEG",
                "Rotated JPEG block outside TFT: %u,%u %ux%u",
                destX, destY, rotatedW, rotatedH);
            return 0;
        }

        tft->setAddrWindow(
            destX,
            destY,
            rotatedW,
            rotatedH);

        tft->writePixels(
            rotated,
            (uint32_t)rotatedW * rotatedH);

        free(rotated);
    }
    else
    {
        uint16_t destX =
            ctx->x + rect->left;

        uint16_t destY =
            ctx->y + rect->top;

        if (destX + blockW > tft->width() ||
            destY + blockH > tft->height())
        {
            free(rgb565);

            ESP_LOGE(
                "TFT_JPEG",
                "JPEG block outside TFT");

            return 0;
        }

        tft->setAddrWindow(
            destX,
            destY,
            blockW,
            blockH);

        tft->writePixels(
            rgb565,
            (uint32_t)blockW * blockH);
    }

    prof_m = s_prof.on ? esp_timer_get_time() : 0;  // BikeVision: profiling
    free(rgb565);
    if (prof_m)
        s_prof.cbmalloc_us += esp_timer_get_time() - prof_m;

    return 1;
}

bool TFT::drawJpgFile(
    const char *path,
    uint16_t x,
    uint16_t y,
    uint16_t maxW,
    uint16_t maxH,
    uint16_t offX,
    uint16_t offY)
{
    (void)offX;
    (void)offY;

    ESP_LOGI(
        "TFT_JPEG",
        "========================================");

    ESP_LOGI(
        "TFT_JPEG",
        "JPEG STREAM DISPLAY");

    ESP_LOGI(
        "TFT_JPEG",
        "File: %s",
        path);

    TFTFile f;

    if (!f.open(path))
    {
        ESP_LOGE(
            "TFT_JPEG",
            "Failed to open JPEG");

        return false;
    }

    size_t jpgSize =
        f.size();

    ESP_LOGI(
        "TFT_JPEG",
        "JPEG size: %u bytes",
        (unsigned)jpgSize);

    if (jpgSize < 4)
    {
        ESP_LOGE(
            "TFT_JPEG",
            "JPEG file too small");

        f.close();

        return false;
    }

    // --------------------------------------------------------
    // JPEG input buffer
    //
    // This is only the compressed JPEG file.
    // Your current JPEG files are ~13-38 KB.
    // --------------------------------------------------------

    uint8_t *jpg =
        (uint8_t *)malloc(jpgSize);

    if (!jpg)
    {
        ESP_LOGE(
            "TFT_JPEG",
            "JPEG input allocation failed");

        f.close();

        return false;
    }

    size_t readBytes =
        f.read(
            jpg,
            jpgSize);

    f.close();

    ESP_LOGI(
        "TFT_JPEG",
        "JPEG read: %u / %u",
        (unsigned)readBytes,
        (unsigned)jpgSize);

    if (readBytes != jpgSize)
    {
        ESP_LOGE(
            "TFT_JPEG",
            "JPEG read incomplete");

        free(jpg);

        return false;
    }

    // --------------------------------------------------------
    // JPEG signature
    // --------------------------------------------------------

    if (jpg[0] != 0xFF ||
        jpg[1] != 0xD8)
    {
        ESP_LOGE(
            "TFT_JPEG",
            "Invalid JPEG signature: %02X %02X",
            jpg[0],
            jpg[1]);

        free(jpg);

        return false;
    }

    ESP_LOGI(
        "TFT_JPEG",
        "JPEG signature OK");

    // --------------------------------------------------------
    // Get JPEG dimensions first
    // --------------------------------------------------------

    esp_jpeg_image_cfg_t infoCfg = {};

    infoCfg.indata =
        jpg;

    infoCfg.indata_size =
        jpgSize;

    infoCfg.out_scale =
        JPEG_IMAGE_SCALE_0;

    infoCfg.out_format =
        JPEG_IMAGE_FORMAT_RGB888;

    esp_jpeg_image_output_t info = {};

    esp_err_t ret =
        esp_jpeg_get_image_info(
            &infoCfg,
            &info);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            "TFT_JPEG",
            "JPEG image info failed: %s",
            esp_err_to_name(ret));

        free(jpg);

        return false;
    }

    ESP_LOGI(
        "TFT_JPEG",
        "JPEG dimensions: %u x %u",
        info.width,
        info.height);

    // --------------------------------------------------------
    // For first test, require image to fit.
    // --------------------------------------------------------

    // --------------------------------------------------------
    // 240x320 portrait JPEG support
    // --------------------------------------------------------
    // The ILI9341 is currently configured as 320x240.
    // Rotate a 240x320 JPEG clockwise while streaming.
    bool rotate90 =
        (info.width == 240 &&
         info.height == 320 &&
         _width == 320 &&
         _height == 240);

    if (rotate90)
    {
        if (x + 320 > _width || y + 240 > _height)
        {
            ESP_LOGE(
                "TFT_JPEG",
                "240x320 rotated image does not fit TFT");
            free(jpg);
            return false;
        }

        ESP_LOGI(
            "TFT_JPEG",
            "Portrait JPEG detected: 240x320 -> rotating 90 degrees -> 320x240");
    }
    else
    {
        if (!maxW)
            maxW = _width - x;

        if (!maxH)
            maxH = _height - y;

        if (info.width > maxW ||
            info.height > maxH)
        {
            ESP_LOGE(
                "TFT_JPEG",
                "JPEG too large for current test");

            ESP_LOGE(
                "TFT_JPEG",
                "Image: %u x %u",
                info.width,
                info.height);

            ESP_LOGE(
                "TFT_JPEG",
                "Available: %u x %u",
                maxW,
                maxH);

            free(jpg);

            return false;
        }
    }

    // --------------------------------------------------------
    // Context for callback
    // --------------------------------------------------------

    TFTJPEGContext ctx = {};

    ctx.tft =
        this;

    ctx.x =
        x;

    ctx.y =
        y;

    ctx.imageWidth =
        info.width;

    ctx.imageHeight =
        info.height;

    ctx.rotate90 =
        rotate90;

    // --------------------------------------------------------
    // Start TFT transaction
    // --------------------------------------------------------

    startWrite();

    ESP_LOGI(
        "TFT_JPEG",
        "Starting streaming JPEG decode...");

    int64_t start =
        esp_timer_get_time();

    ret =
        esp_jpeg_decode_stream(
            jpg,
            jpgSize,
            JPEG_IMAGE_SCALE_0,
            tft_jpeg_output_callback,
            &ctx);

    int64_t end =
        esp_timer_get_time();

    endWrite();

    free(jpg);

    int64_t elapsed =
        end - start;

    ESP_LOGI(
        "TFT_JPEG",
        "JPEG decode time: %lld us",
        elapsed);

    ESP_LOGI(
        "TFT_JPEG",
        "JPEG decode time: %.3f ms",
        elapsed / 1000.0);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            "TFT_JPEG",
            "JPEG streaming decode FAILED: %s",
            esp_err_to_name(ret));

        return false;
    }

    ESP_LOGI(
        "TFT_JPEG",
        "JPEG displayed successfully");

    return true;
}

static bool decodeAndDisplayJPEG(
    TFT *tft,
    const uint8_t *jpg,
    size_t jpgSize,
    uint16_t x,
    uint16_t y,
    uint16_t imageWidth,
    uint16_t imageHeight);

static bool decodeAndDisplayJPEG_Rotated90(
    TFT *tft,
    const uint8_t *jpg,
    size_t jpgSize,
    uint16_t x,
    uint16_t y);

static bool decodeAndDisplayJPEG(
    TFT *tft,
    const uint8_t *jpg,
    size_t jpgSize,
    uint16_t x,
    uint16_t y,
    uint16_t imageWidth,
    uint16_t imageHeight)
{
    if (!tft || !jpg || jpgSize < 4)
        return false;

    ESP_LOGI(
        "TFT_JPEG",
        "Normal JPEG decode: %ux%u",
        imageWidth,
        imageHeight);

    TFTJPEGContext ctx = {};

    ctx.tft = tft;
    ctx.x = x;
    ctx.y = y;
    ctx.imageWidth = imageWidth;
    ctx.imageHeight = imageHeight;
    ctx.rotate90 = false;

    // BikeVision: profiling start
    s_prof = {};
    s_prof.on = true;
    int64_t prof_t0 = esp_timer_get_time();

    tft->startWrite();

    esp_err_t ret = esp_jpeg_decode_stream(
        (uint8_t *)jpg,
        jpgSize,
        JPEG_IMAGE_SCALE_0,
        tft_jpeg_output_callback,
        &ctx);

    tft->endWrite();

    // BikeVision: profiling summary (one line per frame)
    s_prof.on = false;
    {
        int64_t total = esp_timer_get_time() - prof_t0;
        int64_t decode = total - s_prof.cb_us;   // decoder work outside the callback
        int64_t pixels = s_prof.wpmalloc_us + s_prof.swap_us + s_prof.spi_us;
        int64_t other = s_prof.cb_us - s_prof.conv_us - s_prof.cbmalloc_us -
                        s_prof.addr_us - pixels;
        ESP_LOGI("TFT_PROF",
                 "total %lld ms | decode %lld | convert %lld | cb-malloc %lld | "
                 "addr %lld | wp-malloc %lld | swap %lld | spi %lld | other %lld | blocks %lu",
                 total / 1000, decode / 1000, s_prof.conv_us / 1000,
                 s_prof.cbmalloc_us / 1000, s_prof.addr_us / 1000,
                 s_prof.wpmalloc_us / 1000, s_prof.swap_us / 1000,
                 s_prof.spi_us / 1000, other / 1000,
                 (unsigned long)s_prof.blocks);
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            "TFT_JPEG",
            "JPEG decode failed: %s",
            esp_err_to_name(ret));

        return false;
    }

    ESP_LOGI(
        "TFT_JPEG",
        "Normal JPEG display complete");

    return true;
}

static bool decodeAndDisplayJPEG_Rotated90(
    TFT *tft,
    const uint8_t *jpg,
    size_t jpgSize,
    uint16_t x,
    uint16_t y)
{
    if (!tft || !jpg || jpgSize < 4)
        return false;

    ESP_LOGI(
        "TFT_JPEG",
        "Rotating JPEG 90 degrees");

    TFTJPEGContext ctx = {};

    ctx.tft = tft;
    ctx.x = x;
    ctx.y = y;

    // Original JPEG:
    // 240 x 320
    //
    // After rotation:
    // 320 x 240

    ctx.imageWidth = 240;
    ctx.imageHeight = 320;
    ctx.rotate90 = true;

    tft->startWrite();

    esp_err_t ret = esp_jpeg_decode_stream(
        (uint8_t *)jpg,
        jpgSize,
        JPEG_IMAGE_SCALE_0,
        tft_jpeg_output_callback,
        &ctx);

    tft->endWrite();

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            "TFT_JPEG",
            "Rotated JPEG decode failed: %s",
            esp_err_to_name(ret));

        return false;
    }

    ESP_LOGI(
        "TFT_JPEG",
        "Rotated JPEG display complete");

    return true;
}

bool TFT::drawJpegBuffer(const uint8_t *jpg, size_t jpgSize, uint16_t x, uint16_t y, uint16_t maxW, uint16_t maxH)
{
    ESP_LOGI("TFT_JPEG", "----------------------------------------");
    ESP_LOGI("TFT_JPEG", "JPEG DECODER START");
    ESP_LOGI("TFT_JPEG", "----------------------------------------");

    if (!jpg || jpgSize < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8)
    {
        ESP_LOGE("TFT_JPEG", "Invalid JPEG signature");
        return false;
    }

    esp_jpeg_image_cfg_t cfg = {};

    cfg.indata = (uint8_t *)jpg;
    cfg.indata_size = jpgSize;

    /*
     * First get JPEG information.
     * This tells us the original image dimensions.
     */
    esp_jpeg_image_output_t info = {};

    cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
    cfg.out_scale = JPEG_IMAGE_SCALE_0;
    cfg.flags.swap_color_bytes = 0;

    esp_err_t ret = esp_jpeg_get_image_info(&cfg, &info);

    if (ret != ESP_OK)
    {
        ESP_LOGE("TFT_JPEG", "JPEG info failed: %s", esp_err_to_name(ret));

        return false;
    }

    ESP_LOGI("TFT_JPEG", "JPEG dimensions: %u x %u", info.width, info.height);

    uint16_t srcW = info.width;
    uint16_t srcH = info.height;

    /*
     * ----------------------------------------------------
     * CASE 1
     * Image already fits TFT
     * Example: 320x240
     * ----------------------------------------------------
     */

    if (srcW <= _width && srcH <= _height)
    {
        ESP_LOGI("TFT_JPEG", "JPEG fits TFT directly");

        return decodeAndDisplayJPEG(this, jpg, jpgSize, x, y, srcW, srcH);
    }

    /*
     * ----------------------------------------------------
     * CASE 2
     * Portrait image 240x320
     *
     * TFT = 320x240
     *
     * Rotate:
     *
     * 240x320
     *    ↓ 90°
     * 320x240
     * ----------------------------------------------------
     */

    if (srcW == 240 && srcH == 320 && _width == 320 && _height == 240)
    {
        ESP_LOGI("TFT_JPEG", "Portrait JPEG detected: 240x320");

        ESP_LOGI("TFT_JPEG", "Rotating JPEG 90 degrees");

        return decodeAndDisplayJPEG_Rotated90(this, jpg, jpgSize, x, y);
    }

    /*
     * ----------------------------------------------------
     * CASE 3
     * General scaling
     * ----------------------------------------------------
     */

    if (!maxW)
        maxW = _width - x;

    if (!maxH)
        maxH = _height - y;

    float sx = (float)maxW / srcW;

    float sy = (float)maxH / srcH;

    float scale = std::min(sx, sy);

    uint16_t dstW = (uint16_t)std::max(1, (int)std::floor(srcW * scale));

    uint16_t dstH = (uint16_t)std::max(1, (int)std::floor(srcH * scale));

    ESP_LOGI("TFT_JPEG", "Scaling JPEG: %ux%u -> %ux%u", srcW, srcH, dstW, dstH);

    return decodeAndDisplayJPEG(this, jpg, jpgSize, x, y, dstW, dstH);
}

// ============================================================================
// GIF decoder port. The state machine is the same algorithm used by the
// supplied Arduino driver, with Arduino File/String calls replaced by FILE/VFS.
// ============================================================================
/*
bool TFT::drawGifFile(const char *path, uint16_t x, uint16_t y, uint8_t repeat)
{
    int iterations = repeat ? repeat : 1;
    for (int n = 0; n < iterations; n++)
    {
        gif_file.close();
        if (!gif_file.open(path))
            return false;
        GIF_readGifItems();
        if (strncmp(gif_GifHeader, "GIF", 3) != 0)
        {
            gif_file.close();
            GIF_freeMemory();
            return false;
        }
        if ((uint32_t)gif_LogicalScreenWidth * gif_LogicalScreenHeight > 155000)
        {
            gif_file.close();
            GIF_freeMemory();
            return false;
        }
        while (GIF_decodeGif(x, y))
        {
        }
        gif_file.close();
    }
    GIF_freeMemory();
    return true;
}

*/

bool TFT::drawGifFile(
    const char *path,
    uint16_t x,
    uint16_t y,
    uint8_t repeat)
{
    static const char *TAG = "TFT_GIF";

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "GIF FILE DISPLAY");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "File: %s", path);

    if (path == nullptr)
    {
        ESP_LOGE(TAG, "GIF path is NULL");
        return false;
    }

    /*
     * ---------------------------------------------------------
     * Clean up anything left by a previous GIF.
     * ---------------------------------------------------------
     */

    GIF_freeFrameBuffers();
    GIF_freeMemory();

    /*
     * ---------------------------------------------------------
     * Framebuffer will be allocated after reading GIF header
     * so the GIF logical size can be used for stride/height.
     * ---------------------------------------------------------
     */

    uint8_t iterations = repeat;

    if (iterations == 0)
        iterations = 1;

    bool overallOK = true;

    for (uint8_t loop = 0;
         loop < iterations;
         loop++)
    {
        ESP_LOGI(TAG,
                 "GIF playback %u/%u",
                 loop + 1,
                 iterations);

        /*
         * -----------------------------------------------------
         * Reset GIF decoder memory/state before each playback.
         * -----------------------------------------------------
         */

        GIF_freeMemory();

        /*
         * -----------------------------------------------------
         * Make sure previous file is closed.
         * -----------------------------------------------------
         */

        if (gif_file.valid())
        {
            gif_file.close();
        }

        /*
         * -----------------------------------------------------
         * Open GIF.
         * -----------------------------------------------------
         */

        if (!gif_file.open(path))
        {
            ESP_LOGE(TAG,
                     "Failed to open GIF: %s",
                     path);

            overallOK = false;
            break;
        }

        /*
         * -----------------------------------------------------
         * Read GIF header/items.
         * -----------------------------------------------------
         */

        if (GIF_readGifItems() != 0)
        {
            ESP_LOGE(TAG,
                     "GIF header/read failed: %s",
                     path);

            gif_file.close();

            overallOK = false;
            break;
        }

        ESP_LOGI(TAG,
                 "GIF logical size: %ux%u",
                 gif_LogicalScreenWidth,
                 gif_LogicalScreenHeight);

        /* Allocate framebuffer now that logical size is known */
        if (!GIF_allocFrameBuffers())
        {
            ESP_LOGE(TAG, "GIF framebuffer allocation failed after reading header");
            gif_file.close();
            overallOK = false;
            break;
        }

        GIF_clearFrameBuffer(TFT_BLACK);

        /*
         * -----------------------------------------------------
         * Decode and display all frames.
         * -----------------------------------------------------
         */

        while (true)
        {
            bool result =
                GIF_decodeGif(x, y);

            if (!result)
            {
                /*
                 * GIF_decodeGif() returns false when the
                 * GIF trailer/end is reached.
                 *
                 * This is the normal end of this GIF.
                 */

                break;
            }
        }

        /*
         * -----------------------------------------------------
         * Close current GIF.
         * -----------------------------------------------------
         */

        gif_file.close();

        ESP_LOGI(TAG,
                 "GIF playback reached end");
    }

    /*
     * ---------------------------------------------------------
     * Final cleanup.
     * ---------------------------------------------------------
     */

    GIF_freeFrameBuffers();
    GIF_freeMemory();

    ESP_LOGI(TAG,
             "GIF display complete: %s",
             path);

    return overallOK;
}

void TFT::GIF_readHeader()
{
    uint8_t b[6]{};
    gif_file.read(b, 6);
    memcpy(gif_GifHeader, b, 6);
    gif_GifHeader[6] = 0;
}
void TFT::GIF_readLogicalScreenDescriptor()
{
    uint8_t b[7]{};
    gif_file.read(b, 7);
    gif_LogicalScreenWidth = b[0] | (b[1] << 8);
    gif_LogicalScreenHeight = b[2] | (b[3] << 8);
    gif_PackedFields = b[4];
    gif_BackgroundColorIndex = b[5];
    gif_PixelAspectRatio = b[6];
    gif_GlobalColorTableFlag = (gif_PackedFields & 0x80) != 0;
    gif_ColorResulution = ((gif_PackedFields >> 4) & 7) + 1;
    gif_SortFlag = (gif_PackedFields & 8) != 0;
    gif_SizeOfGlobalColorTable = 2u << (gif_PackedFields & 7);
}
void TFT::GIF_readGlobalColorTable()
{
    if (!gif_GlobalColorTableFlag)
        return;
    gif_GlobalColorTable.resize(gif_SizeOfGlobalColorTable);
    uint8_t b[3];
    for (uint16_t i = 0; i < gif_SizeOfGlobalColorTable; i++)
    {
        if (gif_file.read(b, 3) != 3)
        {
            gif_GlobalColorTable.clear();
            return;
        }
        gif_GlobalColorTable[i] = rgb565_from_rgb888(b[0], b[1], b[2]);
    }
}
void TFT::GIF_readImageDescriptor()
{
    uint8_t b[9]{};
    gif_file.read(b, 9);
    gif_ImageLeftPosition = b[0] | (b[1] << 8);
    gif_ImageTopPosition = b[2] | (b[3] << 8);
    gif_ImageWidth = b[4] | (b[5] << 8);
    gif_ImageHeight = b[6] | (b[7] << 8);
    gif_PackedFields = b[8];
    gif_LocalColorTableFlag = (gif_PackedFields & 0x80) != 0;
    gif_InterlaceFlag = (gif_PackedFields & 0x40) != 0;
    gif_SortFlag = (gif_PackedFields & 8) != 0;
    gif_SizeOfLocalColorTable = 2u << (gif_PackedFields & 7);
}
void TFT::GIF_readLocalColorTable()
{
    if (!gif_LocalColorTableFlag)
    {
        gif_LocalColorTable.clear();
        return;
    }
    gif_LocalColorTable.resize(gif_SizeOfLocalColorTable);
    uint8_t b[3];
    for (uint16_t i = 0; i < gif_SizeOfLocalColorTable; i++)
    {
        if (gif_file.read(b, 3) != 3)
        {
            gif_LocalColorTable.clear();
            return;
        }
        gif_LocalColorTable[i] = rgb565_from_rgb888(b[0], b[1], b[2]);
    }
}
void TFT::GIF_readGraphicControlExtension()
{
    uint8_t b[6]{};
    if (gif_file.read(b, 6) != 6)
        return;
    gif_PackedFields = b[1];
    gif_DisposalMethod = (gif_PackedFields >> 2) & 7;
    gif_UserInputFlag = (gif_PackedFields & 2) != 0;
    gif_TransparentColorFlag = (gif_PackedFields & 1) != 0;
    gif_DelayTime = b[2] | (b[3] << 8);
    gif_TransparentColorIndex = b[4];
}

uint8_t TFT::GIF_readDataSubBlock(char *buf)
{
    int n = gif_file.readByte();
    if (n <= 0)
    {
        gif_ZeroDataBlock = true;
        return 0;
    }
    gif_ZeroDataBlock = false;
    if (gif_file.read(buf, n) != (size_t)n)
        return 0;
    return (uint8_t)n;
}
uint8_t TFT::GIF_readPlainTextExtension(char *buf)
{
    int n = gif_file.readByte();
    if (n > 0)
        gif_file.read(gif_buffer, std::min(n, 15));
    return GIF_readDataSubBlock(buf);
}
uint8_t TFT::GIF_readApplicationExtension(char *buf)
{
    int n = gif_file.readByte();
    if (n > 0)
    {
        uint8_t tmp[256];
        gif_file.read(tmp, std::min(n, 256));
    }
    return GIF_readDataSubBlock(buf);
}
uint8_t TFT::GIF_readCommentExtension(char *buf) { return GIF_readDataSubBlock(buf); }
bool TFT::GIF_readExtension(char label)
{
    char buf[256]{};
    switch ((uint8_t)label)
    {
    case 0xF9:
        GIF_readGraphicControlExtension();
        return true;
    case 0x01:
        GIF_readPlainTextExtension(buf);
        return true;
    case 0xFF:
        GIF_readApplicationExtension(buf);
        return true;
    case 0xFE:
        GIF_readCommentExtension(buf);
        return true;
    default:
        return false;
    }
}

int TFT::GIF_GetCode(int code_size, int flag)
{
    /*
     * GIF LZW bit-stream buffer.
     *
     * Keep 2 extra bytes at the beginning so that when a
     * data sub-block changes, the last 2 bytes of the previous
     * block remain available.
     */
    static uint8_t dsb[300];

    static int curbit = 0;
    static int lastbit = 0;
    static int done = 0;
    static int last_byte = 0;

    /*
     * ---------------------------------------------------------
     * INITIALIZE BIT READER
     * ---------------------------------------------------------
     */
    if (flag)
    {
        curbit = 0;
        lastbit = 0;
        done = 0;
        last_byte = 0;

        return 0;
    }

    /*
     * ---------------------------------------------------------
     * MAKE SURE ENOUGH BITS ARE AVAILABLE
     * ---------------------------------------------------------
     *
     * Use '>' rather than '>='.
     *
     * If the requested code ends exactly at the end of the
     * current block, consume it first. The next call will load
     * the next sub-block.
     */
    if ((curbit + code_size) > lastbit)
    {
        /*
         * No more GIF data.
         */
        if (done)
        {
            if (curbit >= lastbit)
                return 0;

            return -1;
        }

        /*
         * Preserve the final two bytes of the previous
         * sub-block.
         *
         * These bytes may contain bits belonging to a code
         * that crosses the sub-block boundary.
         */
        if (last_byte >= 2)
        {
            dsb[0] =
                dsb[last_byte - 2];

            dsb[1] =
                dsb[last_byte - 1];
        }

        /*
         * Read next GIF data sub-block.
         *
         * dsb[2] ... contain the actual compressed data.
         */
        const uint8_t count =
            GIF_readDataSubBlock(
                (char *)&dsb[2]);

        if (count == 0)
        {
            done = 1;
        }

        last_byte =
            2 + count;

        /*
         * Keep the two bytes carried from the previous block.
         */
        curbit =
            (curbit - lastbit) + 16;

        lastbit =
            (2 + count) * 8;
    }

    /*
     * ---------------------------------------------------------
     * FAST CODE EXTRACTION
     * ---------------------------------------------------------
     *
     * OLD VERSION:
     *
     *     for every bit:
     *         i / 8
     *         i % 8
     *         shift
     *         mask
     *
     * That is expensive because GIF_GetCode() is called
     * hundreds of thousands of times.
     *
     * NEW VERSION:
     *
     * Read 3 bytes as one 24-bit little-endian value and
     * extract the requested code with one shift and one mask.
     *
     * Maximum GIF LZW code size = 12 bits, so 3 bytes are
     * sufficient even when the code crosses a byte boundary.
     */

    const uint32_t byteIndex =
        (uint32_t)(curbit >> 3);

    const uint32_t bitOffset =
        (uint32_t)(curbit & 7);

    uint32_t data =
        (uint32_t)dsb[byteIndex];

    data |=
        (uint32_t)dsb[byteIndex + 1] << 8;

    data |=
        (uint32_t)dsb[byteIndex + 2] << 16;

    /*
     * Extract LZW code.
     */
    const uint32_t mask =
        (1U << code_size) - 1U;

    const int ret =
        (int)((data >> bitOffset) & mask);

    /*
     * Advance bit position.
     */
    curbit += code_size;

    return ret;
}

int TFT::GIF_LZWReadByte(bool init)
{
    static bool fresh = false;
    static int firstcode = 0;
    static int oldcode = 0;
    static uint8_t *sp = nullptr;

    /*
     * ---------------------------------------------------------
     * INITIALIZATION
     * ---------------------------------------------------------
     */
    if (init)
    {
        const size_t tableSize =
            (size_t)1 << gif_MaxLzwBits;

        const size_t stackSize =
            (size_t)1 << (gif_MaxLzwBits + 1);

        /*
         * Allocate only when required.
         * This code is executed once per GIF/frame initialization,
         * NOT for every decoded pixel.
         */
        if (gif_next.size() != tableSize)
            gif_next.resize(tableSize);

        if (gif_vals.size() != tableSize)
            gif_vals.resize(tableSize);

        if (gif_stack.size() != stackSize)
            gif_stack.resize(stackSize);

        /*
         * GIF LZW parameters.
         */
        gif_CodeSize =
            gif_LZWMinimumCodeSize + 1;

        gif_ClearCode =
            1 << gif_LZWMinimumCodeSize;

        gif_EOIcode =
            gif_ClearCode + 1;

        gif_MaxCode =
            gif_ClearCode + 2;

        gif_MaxCodeSize =
            gif_ClearCode * 2;

        /*
         * Reset bit reader.
         */
        GIF_GetCode(0, 1);

        fresh = true;

        /*
         * Base dictionary.
         *
         * Only entries 0 ... ClearCode-1 are required.
         */
        uint8_t *vals = gif_vals.data();
        uint16_t *next = gif_next.data();

        for (int i = 0; i < gif_ClearCode; ++i)
        {
            vals[i] = (uint8_t)i;
            next[i] = 0;
        }

        sp = gif_stack.data();

        return 0;
    }

    /*
     * ---------------------------------------------------------
     * FIRST CODE
     * ---------------------------------------------------------
     */
    if (fresh)
    {
        fresh = false;

        firstcode =
            GIF_GetCode(gif_CodeSize, 0);

        while (firstcode == gif_ClearCode)
        {
            firstcode =
                GIF_GetCode(gif_CodeSize, 0);
        }

        oldcode = firstcode;

        return firstcode;
    }

    /*
     * ---------------------------------------------------------
     * STACK FAST PATH
     *
     * A decoded dictionary sequence may already be waiting
     * on the stack. Returning it avoids another GIF_GetCode().
     * ---------------------------------------------------------
     */
    uint8_t *stackBase =
        gif_stack.data();

    if (sp > stackBase)
        return *--sp;

    /*
     * ---------------------------------------------------------
     * CACHE TABLE POINTERS
     *
     * These are used heavily below.
     * ---------------------------------------------------------
     */
    uint16_t *next =
        gif_next.data();

    uint8_t *vals =
        gif_vals.data();

    const int tableSize =
        1 << gif_MaxLzwBits;

    int code;
    int incode;

    /*
     * ---------------------------------------------------------
     * MAIN LZW LOOP
     * ---------------------------------------------------------
     */
    while ((code =
                GIF_GetCode(gif_CodeSize, 0)) >= 0)
    {
        /*
         * -----------------------------------------------------
         * CLEAR CODE
         * -----------------------------------------------------
         */
        if (code == gif_ClearCode)
        {
            /*
             * Reset base dictionary.
             */
            for (int i = 0;
                 i < gif_ClearCode;
                 ++i)
            {
                vals[i] = (uint8_t)i;
                next[i] = 0;
            }

            gif_CodeSize =
                gif_LZWMinimumCodeSize + 1;

            gif_MaxCodeSize =
                gif_ClearCode * 2;

            gif_MaxCode =
                gif_ClearCode + 2;

            sp = stackBase;

            /*
             * First code after CLEAR.
             */
            firstcode =
                GIF_GetCode(gif_CodeSize, 0);

            oldcode = firstcode;

            return firstcode;
        }

        /*
         * -----------------------------------------------------
         * END OF IMAGE
         * -----------------------------------------------------
         */
        if (code == gif_EOIcode)
        {
            char buffer[260];

            while (GIF_readDataSubBlock(buffer) > 0)
            {
            }

            return -2;
        }

        incode = code;

        /*
         * -----------------------------------------------------
         * SPECIAL KWK CASE
         *
         * Code has not yet been inserted into dictionary.
         * -----------------------------------------------------
         */
        if (code >= gif_MaxCode)
        {
            *sp++ =
                (uint8_t)firstcode;

            code = oldcode;
        }

        /*
         * -----------------------------------------------------
         * DICTIONARY WALK
         *
         * This is the main CPU-heavy part of this function.
         * -----------------------------------------------------
         */
        while (code >= gif_ClearCode)
        {
            /*
             * Safety check.
             */
            if ((unsigned)code >=
                (unsigned)gif_MaxCode)
            {
                ESP_LOGE(
                    "TFT_GIF",
                    "Invalid LZW code=%d max=%d",
                    code,
                    gif_MaxCode);

                return -1;
            }

            *sp++ =
                vals[code];

            /*
             * Detect circular dictionary.
             */
            if (code == next[code])
            {
                ESP_LOGE(
                    "TFT_GIF",
                    "GIF LZW dictionary loop");

                return -1;
            }

            code =
                next[code];
        }

        /*
         * -----------------------------------------------------
         * FIRST CHARACTER OF STRING
         * -----------------------------------------------------
         */
        firstcode =
            vals[code];

        *sp++ =
            firstcode;

        /*
         * -----------------------------------------------------
         * ADD DICTIONARY ENTRY
         * -----------------------------------------------------
         */
        if (gif_MaxCode < tableSize)
        {
            const int newCode =
                gif_MaxCode;

            next[newCode] =
                (uint16_t)oldcode;

            vals[newCode] =
                firstcode;

            gif_MaxCode =
                newCode + 1;

            /*
             * Increase code width when required.
             */
            if (gif_MaxCode >= gif_MaxCodeSize &&
                gif_MaxCodeSize < tableSize)
            {
                gif_MaxCodeSize <<= 1;

                ++gif_CodeSize;

                if (gif_CodeSize >
                    gif_MaxLzwBits)
                {
                    gif_CodeSize =
                        gif_MaxLzwBits;
                }
            }
        }

        oldcode = incode;

        /*
         * -----------------------------------------------------
         * RETURN DECODED PIXEL
         * -----------------------------------------------------
         */
        return *--sp;
    }

    return code;
}

bool TFT::GIF_allocFrameBuffers()
{
    static const char *TAG = "TFT_GIF";

    /*
     * Already allocated.
     */
    if (gif_framebuffer != nullptr)
    {
        ESP_LOGI(TAG,
                 "GIF framebuffer already allocated");

        return true;
    }

    /* Determine stride/height from GIF logical size when available. */
    const uint16_t stride = (gif_LogicalScreenWidth != 0) ? gif_LogicalScreenWidth : (uint16_t)TFT_WIDTH;
    const uint16_t height = (gif_LogicalScreenHeight != 0) ? gif_LogicalScreenHeight : (uint16_t)TFT_HEIGHT;

    const size_t pixels = (size_t)stride * height;
    const size_t bufferSize = pixels * sizeof(uint16_t);

    ESP_LOGI(TAG, "Allocating GIF framebuffer: %u bytes", (unsigned)bufferSize);
    ESP_LOGI(TAG, "Free heap before allocation: %u", (unsigned)esp_get_free_heap_size());

    /* Try contiguous allocation in internal RAM first. */
    gif_framebuffer = (uint16_t *)heap_caps_malloc(bufferSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    /* If that fails, try PSRAM (if available). */
    if (gif_framebuffer == nullptr)
    {
        gif_framebuffer = (uint16_t *)heap_caps_malloc(bufferSize, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    }

    /* If still failed, try any 8-bit capable heap. */
    if (gif_framebuffer == nullptr)
    {
        gif_framebuffer = (uint16_t *)heap_caps_malloc(bufferSize, MALLOC_CAP_8BIT);
    }

    /* If contiguous allocation succeeded, clear and return. */
    if (gif_framebuffer != nullptr)
    {
        memset(gif_framebuffer, 0, bufferSize);

        ESP_LOGI(TAG, "GIF framebuffer allocated: %u bytes (contiguous)", (unsigned)bufferSize);
        ESP_LOGI(TAG, "Free heap after allocation: %u", (unsigned)esp_get_free_heap_size());

        gif_framebuffer_stride = stride;
        gif_framebuffer_height = height;

        gif_framebuffer_row_alloc = false;

        return true;
    }

    /* Contiguous allocation failed - fall back to per-row allocations.
       This avoids the need for a single large contiguous block. */
    ESP_LOGW(TAG, "Contiguous allocation failed; falling back to per-row allocation");

    gif_framebuffer_rows = (uint16_t **)malloc((size_t)height * sizeof(uint16_t *));

    if (gif_framebuffer_rows == nullptr)
    {
        ESP_LOGE(TAG, "Failed to allocate row pointer array");
        return false;
    }

    bool ok = true;
    for (uint16_t r = 0; r < height; ++r)
    {
        gif_framebuffer_rows[r] = (uint16_t *)malloc((size_t)stride * sizeof(uint16_t));
        if (gif_framebuffer_rows[r] == nullptr)
        {
            ok = false;
            /* free any rows we already allocated */
            for (uint16_t j = 0; j < r; ++j)
                free(gif_framebuffer_rows[j]);
            free(gif_framebuffer_rows);
            gif_framebuffer_rows = nullptr;
            break;
        }
        /* clear row */
        memset(gif_framebuffer_rows[r], 0, (size_t)stride * sizeof(uint16_t));
    }

    if (!ok)
    {
        ESP_LOGE(TAG, "Per-row allocation failed");
        return false;
    }

    ESP_LOGI(TAG, "GIF framebuffer allocated: %u rows x %u pixels (row-backed)", (unsigned)height, (unsigned)stride);

    gif_framebuffer_stride = stride;
    gif_framebuffer_height = height;
    gif_framebuffer_row_alloc = true;

    return true;
}

uint16_t *TFT::GIF_getRowPtr(uint16_t row)
{
    if (gif_framebuffer_row_alloc && gif_framebuffer_rows != nullptr)
    {
        if (row < gif_framebuffer_height)
            return gif_framebuffer_rows[row];
        return nullptr;
    }

    if (gif_framebuffer != nullptr)
    {
        return gif_framebuffer + (size_t)row * gif_framebuffer_stride;
    }

    return nullptr;
}

uint16_t *TFT::GIF_getPrevRowPtr(uint16_t row)
{
    if (gif_previous_frame_row_alloc && gif_previous_frame_rows != nullptr)
    {
        if (row < gif_framebuffer_height)
            return gif_previous_frame_rows[row];
        return nullptr;
    }

    if (gif_previous_frame != nullptr)
    {
        return gif_previous_frame + (size_t)row * gif_framebuffer_stride;
    }

    return nullptr;
}

void TFT::GIF_clearFrameBuffer(uint16_t color)
{
    const size_t stride = (gif_framebuffer_stride != 0) ? gif_framebuffer_stride : TFT_WIDTH;
    const size_t height = (gif_framebuffer_height != 0) ? gif_framebuffer_height : TFT_HEIGHT;

    if (gif_framebuffer_row_alloc && gif_framebuffer_rows != nullptr)
    {
        for (size_t r = 0; r < height; ++r)
        {
            for (size_t c = 0; c < stride; ++c)
                gif_framebuffer_rows[r][c] = color;
        }
        return;
    }

    if (gif_framebuffer == nullptr)
        return;

    const size_t pixels = stride * height;
    for (size_t i = 0; i < pixels; i++)
        gif_framebuffer[i] = color;
}

void TFT::GIF_freeFrameBuffers()
{
    /* Free main framebuffer so it can be reallocated for a new GIF size. */
    if (gif_framebuffer_row_alloc && gif_framebuffer_rows != nullptr)
    {
        for (uint16_t r = 0; r < gif_framebuffer_height; ++r)
        {
            if (gif_framebuffer_rows[r] != nullptr)
                free(gif_framebuffer_rows[r]);
        }
        free(gif_framebuffer_rows);
        gif_framebuffer_rows = nullptr;
        gif_framebuffer_row_alloc = false;
        gif_framebuffer_stride = 0;
        gif_framebuffer_height = 0;

        ESP_LOGI("TFT_GIF", "GIF framebuffer (row-backed) freed");
    }
    else if (gif_framebuffer != nullptr)
    {
        heap_caps_free(gif_framebuffer);
        gif_framebuffer = nullptr;
        gif_framebuffer_stride = 0;
        gif_framebuffer_height = 0;

        ESP_LOGI("TFT_GIF", "GIF framebuffer freed");
    }

    /* Free previous-frame buffer if present. */
    if (gif_previous_frame_row_alloc && gif_previous_frame_rows != nullptr)
    {
        for (uint16_t r = 0; r < gif_framebuffer_height; ++r)
        {
            if (gif_previous_frame_rows[r] != nullptr)
                free(gif_previous_frame_rows[r]);
        }
        free(gif_previous_frame_rows);
        gif_previous_frame_rows = nullptr;
        gif_previous_frame_row_alloc = false;

        ESP_LOGI("TFT_GIF", "GIF previous-frame (row-backed) buffer freed");
    }
    else if (gif_previous_frame != nullptr)
    {
        free(gif_previous_frame);
        gif_previous_frame = nullptr;

        ESP_LOGI("TFT_GIF", "GIF previous-frame buffer freed");
    }
}
#if 0
void TFT::GIF_displayFrame()
{
    static const char *TAG = "TFT_GIF";

    if (gif_framebuffer == nullptr && !(gif_framebuffer_row_alloc && gif_framebuffer_rows != nullptr))
    {
        ESP_LOGE(TAG, "GIF framebuffer is NULL");
        return;
    }

    /*
     * GIF source:
     *
     *     240 x 320
     *
     * TFT destination:
     *
     *     320 x 240
     *
     * Rotation:
     *
     *     dstX = srcH - 1 - srcY
     *     dstY = srcX
     */

    const uint16_t srcW = 240;
    const uint16_t srcH = 320;

    const uint16_t dstW = 320;
    const uint16_t dstH = 240;

    /*
     * Small working buffer.
     *
     * 1024 RGB565 pixels = 2048 bytes.
     *
     * This avoids allocating another full
     * 320 x 240 framebuffer.
     */
    const uint16_t BUFFER_PIXELS = 1024;

    uint16_t *buffer = (uint16_t *)malloc(BUFFER_PIXELS * sizeof(uint16_t));

    if (buffer == nullptr)
    {
        ESP_LOGE(TAG, "GIF display buffer allocation failed");

        return;
    }

    /*
     * Set the complete destination window ONCE.
     */
    startWrite();

    setAddrWindow(0, 0, dstW, dstH);

    /*
     * Generate rotated pixels in chunks.
     *
     * The TFT receives pixels in normal
     * destination order:
     *
     *   row 0:  320 pixels
     *   row 1:  320 pixels
     *   ...
     *   row 239: 320 pixels
     */
    uint32_t totalPixels =
        (uint32_t)dstW * dstH;

    uint32_t outputIndex = 0;

    while (outputIndex < totalPixels)
    {
        uint32_t count = totalPixels - outputIndex;

        if (count > BUFFER_PIXELS)
            count = BUFFER_PIXELS;

        /*
         * Convert linear destination index
         * into rotated source coordinates.
         */
        for (uint32_t i = 0; i < count; i++)
        {
            uint32_t dstIndex = outputIndex + i;

            uint16_t dstY = dstIndex / dstW;

            uint16_t dstX = dstIndex % dstW;

            /*
             * 90-degree rotation.
             */
            uint16_t srcX = dstY;

            uint16_t srcY = srcH - 1 - dstX;

            uint32_t srcIndex = (uint32_t)srcY * srcW + srcX;

            uint16_t *rowPtr = GIF_getRowPtr(srcY);
            buffer[i] = (rowPtr != nullptr) ? rowPtr[srcX] : 0;
        }

        /*
         * Send this chunk.
         *
         * writePixels() already converts
         * RGB565 to MSB-first bytes and
         * performs the SPI transfer.
         */
        writePixels(buffer, count);

        outputIndex += count;
    }

    endWrite();

    free(buffer);

    ESP_LOGI(TAG, "GIF frame displayed: %ux%u -> %ux%u", srcW, srcH, dstW, dstH);
}
#else

void TFT::GIF_displayFrame()
{
    static const char *TAG = "TFT_GIF";

    if (gif_framebuffer == nullptr && !(gif_framebuffer_row_alloc && gif_framebuffer_rows != nullptr))
    {
        ESP_LOGE(TAG, "GIF framebuffer is NULL");
        return;
    }

    /*
     * Source: use GIF logical screen size when available.
     * Destination: current TFT size.
     */
    const uint16_t srcW = (gif_LogicalScreenWidth != 0) ? gif_LogicalScreenWidth : (uint16_t)TFT_WIDTH;
    const uint16_t srcH = (gif_LogicalScreenHeight != 0) ? gif_LogicalScreenHeight : (uint16_t)TFT_HEIGHT;

    const uint16_t dstW = (uint16_t)_width;
    const uint16_t dstH = (uint16_t)_height;

    constexpr uint16_t BUFFER_PIXELS = 1024;

    uint16_t *buffer =
        static_cast<uint16_t *>(
            malloc(BUFFER_PIXELS * sizeof(uint16_t)));

    if (buffer == nullptr)
    {
        ESP_LOGE(TAG,
                 "GIF display buffer allocation failed");
        return;
    }

    startWrite();

    setAddrWindow(0, 0, dstW, dstH);

    /*
     * Destination is sent row-by-row.
     *
     * Rotation:
     *
     *   srcX = dstY
     *   srcY = srcH - 1 - dstX
     *
     * Instead of calculating:
     *
     *   dstIndex / dstW
     *   dstIndex % dstW
     *
     * for every pixel, process one destination
     * row at a time.
     */

    /*
     * Two common cases:
     * 1) Source == destination (no rotation)
     * 2) Source is rotated 90deg (srcW==dstH && srcH==dstW)
     *
     * For the rotated case we map pixels from the source
     * using the 90-degree transform. Otherwise, if the
     * source matches the TFT size, write the framebuffer
     * directly for best performance.
     */
    if (srcW == dstW && srcH == dstH)
    {
        /* Direct copy (row-major source) */
        /* src stride might equal dstW; write row-by-row to be safe */
        const uint16_t stride = (gif_framebuffer_stride != 0) ? gif_framebuffer_stride : TFT_WIDTH;
        for (uint16_t y = 0; y < dstH; ++y)
        {
            const uint16_t *rowPtr = GIF_getRowPtr(y);
            if (rowPtr)
                writePixels(const_cast<uint16_t *>(rowPtr), dstW);
            else
            {
                uint16_t tmp = 0;
                writePixels(&tmp, 1);
            }
        }
    }
    else if (srcW == dstH && srcH == dstW)
    {
        /* Rotated 90 degrees clockwise: srcX = dstY, srcY = srcH - 1 - dstX */
        for (uint16_t dstY = 0; dstY < dstH; dstY++)
        {
            const uint16_t srcX = dstY;

            const uint16_t stride = (gif_framebuffer_stride != 0) ? gif_framebuffer_stride : TFT_WIDTH;

            uint16_t *dst = buffer;

            for (uint16_t dstX = 0; dstX < dstW; dstX++)
            {
                uint16_t srcY = srcH - 1 - dstX;
                uint16_t *rowPtr = GIF_getRowPtr(srcY);
                uint16_t pix = (rowPtr != nullptr) ? rowPtr[srcX] : 0;
                *dst++ = pix;
            }

            writePixels(buffer, dstW);
        }
    }
    else
    {
        ESP_LOGW(TAG, "Unexpected GIF size %ux%u, TFT %ux%u - attempting best-effort display",
                 srcW, srcH, dstW, dstH);
        /* Best-effort: write the TFT-sized portion line-by-line from framebuffer stride */
        {
            const uint16_t stride = (gif_framebuffer_stride != 0) ? gif_framebuffer_stride : TFT_WIDTH;
            for (uint16_t y = 0; y < dstH; ++y)
            {
                const uint16_t *rowPtr = GIF_getRowPtr(y);
                if (rowPtr)
                    writePixels(const_cast<uint16_t *>(rowPtr), dstW);
            }
        }
    }

    endWrite();

    free(buffer);

    ESP_LOGI(TAG,"GIF frame displayed: %ux%u -> %ux%u", srcW, srcH, dstW, dstH);
}

#endif

bool TFT::GIF_ReadImage(uint16_t x, uint16_t y)
{
    static const char *TAG = "TFT_GIF";

    if (gif_ImageWidth == 0 || gif_ImageHeight == 0)
    {
        ESP_LOGE(TAG, "Invalid GIF image size");
        return false;
    }

    const uint16_t srcW = gif_ImageWidth;
    const uint16_t srcH = gif_ImageHeight;

    const uint16_t frameX = x + gif_ImageLeftPosition;

    const uint16_t frameY = y + gif_ImageTopPosition;

    const uint32_t maxPixels = (uint32_t)srcW * srcH;

    ESP_LOGI(TAG, "GIF_ReadImage: frame=%ux%u at %u,%u", srcW, srcH, frameX, frameY);

    /* ---------------------------------------------------------
       Apply disposal for previous frame (if any)
       --------------------------------------------------------- */
    if (gif_prev_width && gif_prev_height)
    {
        switch (gif_prev_disposal)
        {
        case 2: // Restore to background
        {
            uint16_t bg = TFT_BLACK;
            if (!gif_GlobalColorTable.empty() && gif_BackgroundColorIndex < gif_GlobalColorTable.size())
            {
                bg = gif_GlobalColorTable[gif_BackgroundColorIndex];
            }

            const uint16_t left = gif_prev_left;
            const uint16_t top = gif_prev_top;
            const uint16_t right = left + gif_prev_width;
            const uint16_t bottom = top + gif_prev_height;

            const uint16_t stride = (gif_framebuffer_stride != 0) ? gif_framebuffer_stride : TFT_WIDTH;
            const uint16_t height = (gif_framebuffer_height != 0) ? gif_framebuffer_height : TFT_HEIGHT;

            for (uint16_t ry = top; ry < bottom && ry < height; ++ry)
            {
                uint16_t *rowPtr = GIF_getRowPtr(ry);
                if (!rowPtr)
                    continue;
                for (uint16_t rx = left; rx < right && rx < stride; ++rx)
                {
                    rowPtr[rx] = bg;
                }
            }
            break;
        }
        case 3: // Restore to previous
        {
            if (gif_previous_frame != nullptr)
            {
                const uint16_t left = gif_prev_left;
                const uint16_t top = gif_prev_top;
                const uint16_t right = left + gif_prev_width;
                const uint16_t bottom = top + gif_prev_height;

                const uint16_t stride = (gif_framebuffer_stride != 0) ? gif_framebuffer_stride : TFT_WIDTH;
                const uint16_t height = (gif_framebuffer_height != 0) ? gif_framebuffer_height : TFT_HEIGHT;

                for (uint16_t ry = top; ry < bottom && ry < height; ++ry)
                {
                    uint16_t *dstRow = GIF_getRowPtr(ry);
                    uint16_t *srcRow = GIF_getPrevRowPtr(ry);
                    if (!dstRow || !srcRow)
                        continue;
                    for (uint16_t rx = left; rx < right && rx < stride; ++rx)
                    {
                        dstRow[rx] = srcRow[rx];
                    }
                }
            }
            break;
        }
        default:
            break;
        }

        /* After applying disposal, clear previous-frame metadata so it isn't applied again */
        gif_prev_width = 0;
        gif_prev_height = 0;
    }

    /* ---------------------------------------------------------
       Read LZW minimum code size
       --------------------------------------------------------- */

    gif_LZWMinimumCodeSize = gif_file.readByte();

    ESP_LOGI(TAG, "GIF LZW minimum code size = %d", gif_LZWMinimumCodeSize);

    if (gif_LZWMinimumCodeSize < 2 ||
        gif_LZWMinimumCodeSize > 8)
    {
        ESP_LOGE(TAG, "Invalid LZW minimum code size: %d", gif_LZWMinimumCodeSize);

        return false;
    }

    /* ---------------------------------------------------------
       Initialize LZW decoder
       --------------------------------------------------------- */

    if (GIF_LZWReadByte(true) < 0)
    {
        ESP_LOGE(TAG, "Failed to initialize LZW decoder");

        return false;
    }

    /*
     * If this frame requests "restore to previous" (disposal==3)
     * we must save the current framebuffer so it can be restored
     * after this frame is removed.
     */
    if (gif_DisposalMethod == 3)
    {
        const uint16_t stride = (gif_framebuffer_stride != 0) ? gif_framebuffer_stride : TFT_WIDTH;
        const uint16_t height = (gif_framebuffer_height != 0) ? gif_framebuffer_height : TFT_HEIGHT;

        const size_t pixels = (size_t)stride * height;

        if (gif_framebuffer_row_alloc && gif_framebuffer_rows != nullptr)
        {
            /* Allocate previous-frame row-backed buffers */
            gif_previous_frame_rows = (uint16_t **)malloc((size_t)height * sizeof(uint16_t *));
            if (gif_previous_frame_rows != nullptr)
            {
                bool ok = true;
                for (uint16_t r = 0; r < height; ++r)
                {
                    gif_previous_frame_rows[r] = (uint16_t *)malloc((size_t)stride * sizeof(uint16_t));
                    if (gif_previous_frame_rows[r] == nullptr)
                    {
                        ok = false;
                        for (uint16_t j = 0; j < r; ++j)
                            free(gif_previous_frame_rows[j]);
                        free(gif_previous_frame_rows);
                        gif_previous_frame_rows = nullptr;
                        break;
                    }
                    memcpy(gif_previous_frame_rows[r], gif_framebuffer_rows[r], (size_t)stride * sizeof(uint16_t));
                }

                if (!ok)
                {
                    ESP_LOGW(TAG, "Failed to allocate previous-frame (row-backed) buffer for disposal=3");
                }
                else
                {
                    gif_previous_frame_row_alloc = true;
                }
            }
            else
            {
                ESP_LOGW(TAG, "Failed to allocate previous-frame row pointer array for disposal=3");
            }
        }
        else
        {
            if (gif_previous_frame == nullptr)
            {
                gif_previous_frame = (uint16_t *)malloc(pixels * sizeof(uint16_t));
            }

            if (gif_previous_frame != nullptr)
            {
                memcpy(gif_previous_frame, gif_framebuffer, pixels * sizeof(uint16_t));
            }
            else
            {
                ESP_LOGW(TAG, "Failed to allocate previous-frame buffer for disposal=3");
            }
        }
    }

    /* ---------------------------------------------------------
       Decode directly into existing GIF framebuffer
       --------------------------------------------------------- */

    uint16_t localX = 0;
    uint16_t localY = 0;

    uint32_t count = 0;

    int64_t lzwStart = esp_timer_get_time();

    while (count < maxPixels)
    {
        const int color =
            GIF_LZWReadByte(false);

        if (color < 0)
        {
            ESP_LOGE(TAG,
                     "LZW decode failed at pixel %lu",
                     (unsigned long)count);

            return false;
        }

        const uint16_t px =
            frameX + localX;

        const uint16_t py =
            frameY + localY;

        /* -----------------------------------------------------
           Only update visible/non-transparent pixels.

           This is important for animated GIFs.

           A transparent pixel must NOT erase the previous
           frame with black.
           ----------------------------------------------------- */

        if (!(gif_TransparentColorFlag &&
              color == gif_TransparentColorIndex))
        {
            uint16_t rgb565 = 0;

            /* -------------------------------------------------
               Local color table
               ------------------------------------------------- */

            if (gif_LocalColorTableFlag)
            {
                if ((size_t)color >=
                    gif_LocalColorTable.size())
                {
                    ESP_LOGE(TAG,
                             "Local color index invalid: %d",
                             color);

                    return false;
                }

                rgb565 =
                    gif_LocalColorTable[color];
            }

            /* -------------------------------------------------
               Global color table
               ------------------------------------------------- */

            else
            {
                if ((size_t)color >=
                    gif_GlobalColorTable.size())
                {
                    ESP_LOGE(TAG,
                             "Global color index invalid: %d",
                             color);

                    return false;
                }

                rgb565 =
                    gif_GlobalColorTable[color];
            }

            /* -------------------------------------------------
               Write into existing framebuffer
               ------------------------------------------------- */

            const uint16_t stride = (gif_framebuffer_stride != 0) ? gif_framebuffer_stride : TFT_WIDTH;
            const uint16_t height = (gif_framebuffer_height != 0) ? gif_framebuffer_height : TFT_HEIGHT;

            if (px < stride && py < height)
            {
                uint16_t *rowPtr = GIF_getRowPtr(py);
                if (rowPtr)
                    rowPtr[px] = rgb565;
            }
        }

        /* -----------------------------------------------------
           Advance source GIF position
           ----------------------------------------------------- */

        localX++;

        if (localX >= srcW)
        {
            localX = 0;
            localY++;
        }

        count++;
    }

    int64_t lzwTime =
        esp_timer_get_time() - lzwStart;

    ESP_LOGI(TAG,
             "GIF LZW decode time: %lld us (%.2f ms)",
             (long long)lzwTime,
             lzwTime / 1000.0);

    ESP_LOGI(TAG,
             "GIF frame decoded: %ux%u at %u,%u", srcW, srcH, frameX, frameY);

    ESP_LOGI(TAG, "Decoded pixels: %lu", (unsigned long)count);

    /*
     * IMPORTANT:
     *
     * Do not call GIF_displayFrame() here.
     *
     * GIF_ReadImage() only decodes into gif_framebuffer.
     */

    /*
     * Save metadata for disposal handling by the next frame.
     */
    gif_prev_left = frameX;
    gif_prev_top = frameY;
    gif_prev_width = srcW;
    gif_prev_height = srcH;
    gif_prev_disposal = gif_DisposalMethod;

    GIF_displayFrame();

    /*
     * If the previous-frame buffer exists but the next disposal will
     * not require restoring, free it to save memory.
     */
    if (gif_prev_disposal != 3)
    {
        if (gif_previous_frame_row_alloc && gif_previous_frame_rows != nullptr)
        {
            for (uint16_t r = 0; r < gif_framebuffer_height; ++r)
            {
                if (gif_previous_frame_rows[r] != nullptr)
                    free(gif_previous_frame_rows[r]);
            }
            free(gif_previous_frame_rows);
            gif_previous_frame_rows = nullptr;
            gif_previous_frame_row_alloc = false;
        }
        else if (gif_previous_frame != nullptr)
        {
            free(gif_previous_frame);
            gif_previous_frame = nullptr;
        }
    }

    return true;
}

int TFT::GIF_readGifItems()
{
    static const char *TAG = "TFT_GIF";

    ESP_LOGI(TAG,
             "Reading GIF header");

    GIF_readHeader();

    if (strncmp(gif_GifHeader, "GIF", 3) != 0)
    {
        ESP_LOGE(TAG,
                 "Invalid GIF header");

        gif_debug_state =
            GIF_DEBUG_ERROR;

        return -1;
    }

    ESP_LOGI(TAG,
             "GIF header OK: %.6s",
             gif_GifHeader);

    GIF_readLogicalScreenDescriptor();

    ESP_LOGI(TAG,
             "GIF Logical Screen: %u x %u",
             gif_LogicalScreenWidth,
             gif_LogicalScreenHeight);

    gif_first_read = true;

    gif_debug_state =
        GIF_DEBUG_IDLE;

    return 0;
}

bool TFT::GIF_decodeGif(uint16_t x, uint16_t y)
{
    static const char *TAG = "TFT_GIF";

    if (gif_first_read)
    {
        ESP_LOGI(TAG,
                 "Reading GIF global color table");

        GIF_readGlobalColorTable();

        gif_first_read = false;
    }

    while (true)
    {
        int c = gif_file.readByte();

        if (c == EOF)
        {
            ESP_LOGI(TAG,
                     "GIF reached EOF");

            gif_debug_state =
                GIF_DEBUG_FRAME_DONE;

            return false;
        }

        if (c == ';')
        {
            ESP_LOGI(TAG,
                     "GIF trailer reached");

            gif_debug_state =
                GIF_DEBUG_FRAME_DONE;

            return false;
        }

        if (c == '!')
        {
            int label =
                gif_file.readByte();

            ESP_LOGD(TAG,
                     "GIF extension: 0x%02X",
                     label);

            GIF_readExtension((char)label);

            continue;
        }

        if (c == ',')
        {
            ESP_LOGI(TAG,
                     "GIF image descriptor found");

            GIF_readImageDescriptor();

            GIF_readLocalColorTable();

            ESP_LOGI(TAG,
                     "GIF frame: left=%u top=%u width=%u height=%u",
                     gif_ImageLeftPosition,
                     gif_ImageTopPosition,
                     gif_ImageWidth,
                     gif_ImageHeight);

            bool ok =
                GIF_ReadImage(x, y);

            if (gif_DelayTime)
            {
                ESP_LOGI(TAG,
                         "GIF frame delay = %u ms",
                         gif_DelayTime * 10);

                vTaskDelay(
                    pdMS_TO_TICKS(
                        (uint32_t)gif_DelayTime * 10));
            }

            gif_DelayTime = 0;

            return ok;
        }
    }
}

void TFT::GIF_freeMemory()
{
    static const char *TAG = "TFT_GIF";

    ESP_LOGI(TAG, "Freeing GIF memory");

    gif_next.clear();
    gif_vals.clear();
    gif_stack.clear();

    gif_GlobalColorTable.clear();
    gif_LocalColorTable.clear();

    gif_file.close();

    gif_debug_state = GIF_DEBUG_IDLE;
}