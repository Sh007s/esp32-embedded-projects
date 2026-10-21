#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "font5x7.h"

#define TFT_WIDTH 240
#define TFT_HEIGHT 320

#define TFT_BLACK 0x0000
#define TFT_BLUE 0x001F
#define TFT_RED 0xF800
#define TFT_GREEN 0x0400
#define TFT_CYAN 0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_YELLOW 0xFFE0
#define TFT_WHITE 0xFFFF
#define TFT_GRAY 0x8410
#define TFT_DARKGREY 0xAD55
#define TFT_ORANGE 0xFD20
#define TFT_NAVY 0x000F
#define TFT_DARKGREEN 0x0320
#define TFT_LIGHTGREY 0xD69A
#define TFT_PINK 0xFE19
#define TFT_PURPLE 0x780F
#define TFT_BROWN 0xA145
#define TFT_LIME 0x07E0
#define TFT_GOLD 0xFEA0
#define TFT_VIOLET 0x801F

#define ILI9341_SLPOUT 0x11
#define ILI9341_INVOFF 0x20
#define ILI9341_INVON 0x21
#define ILI9341_DISPON 0x29
#define ILI9341_CASET 0x2A
#define ILI9341_RASET 0x2B
#define ILI9341_RAMWR 0x2C
#define ILI9341_MADCTL 0x36
#define ILI9341_VSCRSADD 0x37

class TFT
{
public:
    explicit TFT(uint8_t tft_id = 0);
    ~TFT();

    void begin(uint8_t CS = 5, uint8_t DC = 21, uint8_t MOSI = 23,
               uint8_t MISO = 19, uint8_t SCK = 18, uint8_t RST = 22,
               uint8_t BL = 255);

    void setFrequency(uint32_t frequency);
    void setRotation(uint8_t rotation);
    uint8_t getRotation() const;
    int16_t width() const;
    int16_t height() const;

    uint16_t color565(uint8_t r, uint8_t g, uint8_t b);

    void invertDisplay(bool invert);
    void scrollTo(uint16_t y);

    void startWrite();
    void endWrite();
    void writeCommand(uint8_t command);
    void setAddrWindow(uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h);

    void writePixel(uint16_t color);
    void writePixel(int16_t x, int16_t y, uint16_t color);
    void writePixelXY(int16_t x, int16_t y, uint16_t color);

    void writePixels(const uint16_t *colors, uint32_t len);

    // BikeVision C5 (strip drawing): send pixel bytes in the background with
    // DMA while the CPU decodes the next strip. Only one transfer is in
    // flight; waitPixelQueue() must be called before any other SPI write.
    void queuePixelBytes(const uint8_t *data, size_t len);
    void waitPixelQueue();
    void writeColor(uint16_t color, uint32_t len);
    void pushColor(uint16_t color);

    void fillScreen(uint16_t color);
    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void drawFastHLine(int16_t x, int16_t y,
                       int16_t w, uint16_t color);
    void drawFastVLine(int16_t x, int16_t y,
                       int16_t h, uint16_t color);
    void drawLine(int16_t x0, int16_t y0,
                  int16_t x1, int16_t y1,
                  uint16_t color);

    void drawRect(int16_t x, int16_t y,
                  int16_t w, int16_t h,
                  uint16_t color);

    void fillRect(int16_t x, int16_t y,
                  int16_t w, int16_t h,
                  uint16_t color);

    void drawRoundRect(int16_t x, int16_t y,
                       int16_t w, int16_t h,
                       int16_t r, uint16_t color);

    void fillRoundRect(int16_t x, int16_t y,
                       int16_t w, int16_t h,
                       int16_t r, uint16_t color);

    void drawCircle(int16_t x0, int16_t y0,
                    int16_t r, uint16_t color);

    void fillCircle(int16_t x0, int16_t y0,
                    uint16_t r, uint16_t color);

    void drawTriangle(int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2,
                      uint16_t color);

    void fillTriangle(int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2,
                      uint16_t color);

    // ========================================================
    // 5x7 TEXT ENGINE
    // ========================================================

    void setCursor(uint16_t x, uint16_t y);
    void setTextColor(uint16_t color);
    void setTextSize(uint8_t size);
    void setTextOrientation(uint8_t orientation);

    size_t write(uint8_t c);
    size_t write(const uint8_t *buffer, size_t size);

    void println();
    void println(const char *str);
    void println(int value);
    void println(unsigned int value);
    void println(long value);
    void println(unsigned long value);
    void println(float value, int digits = 2);
    void println(double value, int digits = 2);
    void println(unsigned int value, int base);
    void println(unsigned long value, int base);

    void writeChar(char c);
    void writeText(const char *text);
    void writeText(const uint8_t *text);

    // ========================================================
    // RAW RGB565 IMAGE HELPERS
    // ========================================================

    bool drawRGB565(const uint16_t *pixels,
                    uint16_t w,
                    uint16_t h,
                    int16_t x = 0,
                    int16_t y = 0);

    bool drawRGB565Scaled(const uint16_t *pixels,
                          uint16_t w,
                          uint16_t h,
                          uint16_t dstW,
                          uint16_t dstH,
                          int16_t x = 0,
                          int16_t y = 0);

    // ========================================================
    // FILE IMAGE APIs
    // ========================================================

    bool drawBmpFile(const char *path,
                     uint16_t x = 0,
                     uint16_t y = 0,
                     uint16_t maxWidth = 0,
                     uint16_t maxHeight = 0,
                     uint16_t offX = 0,
                     uint16_t offY = 0);

    bool drawJpgFile(const char *path,
                     uint16_t x = 0,
                     uint16_t y = 0,
                     uint16_t maxWidth = 0,
                     uint16_t maxHeight = 0,
                     uint16_t offX = 0,
                     uint16_t offY = 0);

    bool drawGifFile(const char *path,
                     uint16_t x = 0,
                     uint16_t y = 0,
                     uint8_t repeat = 1);

    // ========================================================
    // Arduino-compatible BMP alias
    // ========================================================

    bool drawBMPFile(const char *path,
                     uint16_t x = 0,
                     uint16_t y = 0,
                     uint16_t maxWidth = 0,
                     uint16_t maxHeight = 0,
                     uint16_t offX = 0,
                     uint16_t offY = 0)
    {
        return drawBmpFile(
            path,
            x,
            y,
            maxWidth,
            maxHeight,
            offX,
            offY);
    }

    // ========================================================
    // JPEG FROM RAM (BikeVision: moved from private to public so a
    // camera frame can be drawn straight from the receive buffer)
    // ========================================================

    bool drawJpegBuffer(const uint8_t *jpg,
                        size_t jpgSize,
                        uint16_t x = 0,
                        uint16_t y = 0,
                        uint16_t maxWidth = 0,
                        uint16_t maxHeight = 0);

    uint8_t readcommand8(uint8_t command,
                         uint8_t index = 0);

    // BikeVision: set the ILI9341 up again after it reset itself (white
    // screen). Hardware reset + init sequence + rotation; SPI bus untouched.
    void reinit();

private:
    // ========================================================
    // TFT FILE
    // ========================================================

    struct TFTFile
    {
        FILE *fp = nullptr;

        bool open(const char *path);

        bool valid() const
        {
            return fp != nullptr;
        }

        size_t size();
        size_t read(void *buf, size_t len);
        int readByte();
        size_t readBytes(char *buf, size_t len);
        bool seek(size_t pos);
        long tell();
        void close();
    };

    // ========================================================
    // TFT CORE
    // ========================================================

    void init();
    void hwReset();

    void writeByte(uint8_t value);
    void writeBytes(const uint8_t *data, size_t len);

    uint8_t transferByte(uint8_t value);

    void writeFillRect(int16_t x,
                       int16_t y,
                       int16_t w,
                       int16_t h,
                       uint16_t color);

    void drawCircleHelper(int16_t x0,
                          int16_t y0,
                          int16_t r,
                          uint8_t corner,
                          uint16_t color);

    void fillCircleHelper(int16_t x0,
                          int16_t y0,
                          int16_t r,
                          uint8_t corner,
                          int16_t delta,
                          uint16_t color);

    // ========================================================
    // BMP
    // ========================================================

    static uint16_t bmpRead16(const uint8_t *b, int p);
    static uint32_t bmpRead32(const uint8_t *b, int p);

    bool drawBmp24(TFTFile &f,
                   uint32_t dataOffset,
                   int32_t bw,
                   int32_t bh,
                   uint16_t bpp,
                   uint16_t x,
                   uint16_t y,
                   uint16_t outW,
                   uint16_t outH,
                   uint16_t offX,
                   uint16_t offY);

    // ========================================================
    // GIF STATE / DECODER
    // ========================================================

    TFTFile gif_file;

    bool gif_debug = false;

    // --------------------------------------------------------
    // GIF DEBUG STATE
    // --------------------------------------------------------

    enum GIFDebugState
    {
        GIF_DEBUG_IDLE = 0,
        GIF_DEBUG_START,
        GIF_DEBUG_READING_LZW,
        GIF_DEBUG_DECODING,
        GIF_DEBUG_SENDING,
        GIF_DEBUG_FRAME_DONE,
        GIF_DEBUG_ERROR
    };

    GIFDebugState gif_debug_state = GIF_DEBUG_IDLE;

    // Number of decoded GIF pixels
    uint32_t gif_debug_pixels = 0;

    // Number of decoded rows
    uint32_t gif_debug_rows = 0;

    // Number of SPI transfers used for GIF
    uint32_t gif_debug_spi_transfers = 0;

    // Start timestamp of current GIF frame
    uint32_t gif_debug_frame_start = 0;

    // --------------------------------------------------------
    // GIF LZW / COLOR TABLE MEMORY
    // --------------------------------------------------------

    std::vector<uint16_t> gif_next;
    std::vector<uint8_t> gif_vals;
    std::vector<uint8_t> gif_stack;

    std::vector<uint16_t> gif_GlobalColorTable;
    std::vector<uint16_t> gif_LocalColorTable;

    static constexpr uint8_t gif_MaxLzwBits = 12;

    bool gif_first_read = false;

    bool gif_GlobalColorTableFlag = false;
    bool gif_LocalColorTableFlag = false;

    bool gif_SortFlag = false;
    bool gif_TransparentColorFlag = false;
    bool gif_UserInputFlag = false;

    bool gif_ZeroDataBlock = false;
    bool gif_InterlaceFlag = false;

    uint8_t gif_buffer[15]{};
    uint8_t gif_DSBbuffer[256]{};

    char gif_GifHeader[7]{};

    uint8_t gif_BackgroundColorIndex = 0;
    uint8_t gif_BlockTerninator = 0;
    uint8_t gif_CharacterCellWidth = 0;
    uint8_t gif_CharacterCellHeight = 0;

    uint8_t gif_CodeSize = 0;
    uint8_t gif_ColorResulution = 0;
    uint8_t gif_DisposalMethod = 0;

    uint8_t gif_ImageSeparator = 0;
    uint8_t gif_lenDatablock = 0;
    uint8_t gif_LZWMinimumCodeSize = 0;
    uint8_t gif_PackedFields = 0;

    uint8_t gif_PixelAspectRatio = 0;
    uint8_t gif_TextBackgroundColorIndex = 0;
    uint8_t gif_TextForegroundColorIndex = 0;
    uint8_t gif_TransparentColorIndex = 0;

    uint16_t gif_ClearCode = 0;
    uint16_t gif_DelayTime = 0;
    uint16_t gif_EOIcode = 0;

    uint16_t gif_ImageHeight = 0;
    uint16_t gif_ImageWidth = 0;

    uint16_t gif_ImageLeftPosition = 0;
    uint16_t gif_ImageTopPosition = 0;

    uint16_t gif_LogicalScreenWidth = 0;
    uint16_t gif_LogicalScreenHeight = 0;

    uint16_t gif_MaxCode = 0;
    uint16_t gif_MaxCodeSize = 0;

    uint16_t gif_SizeOfGlobalColorTable = 0;
    uint16_t gif_SizeOfLocalColorTable = 0;

    // GIF frame buffers
    uint16_t *gif_framebuffer = nullptr;
    // Row-backed framebuffer fallback when a large contiguous block can't be allocated.
    uint16_t **gif_framebuffer_rows = nullptr;
    bool gif_framebuffer_row_alloc = false;
    uint16_t *gif_previous_frame = nullptr;
    uint16_t **gif_previous_frame_rows = nullptr;
    bool gif_previous_frame_row_alloc = false;
    // Framebuffer stride (pixels per row) and height for the GIF framebuffer
    uint16_t gif_framebuffer_stride = 0;
    uint16_t gif_framebuffer_height = 0;
    // Previous-frame metadata for disposal handling
    uint16_t gif_prev_left = 0;
    uint16_t gif_prev_top = 0;
    uint16_t gif_prev_width = 0;
    uint16_t gif_prev_height = 0;
    uint8_t gif_prev_disposal = 0;

    // ========================================================
    // GIF FUNCTIONS
    // ========================================================

    int GIF_readGifItems();

    bool GIF_decodeGif(uint16_t x,
                       uint16_t y);

    void GIF_freeMemory();

    void GIF_readHeader();

    void GIF_readLogicalScreenDescriptor();

    void GIF_readImageDescriptor();

    void GIF_readLocalColorTable();

    void GIF_readGlobalColorTable();

    void GIF_readGraphicControlExtension();

    uint8_t GIF_readPlainTextExtension(char *buf);

    uint8_t GIF_readApplicationExtension(char *buf);

    uint8_t GIF_readCommentExtension(char *buf);

    uint8_t GIF_readDataSubBlock(char *buf);

    bool GIF_readExtension(char label);

    int GIF_GetCode(int code_size, int flag);

    int GIF_LZWReadByte(bool init);

    bool GIF_ReadImage(uint16_t x, uint16_t y);

    bool GIF_allocFrameBuffers();
    void GIF_freeFrameBuffers();

    void GIF_clearFrameBuffer(uint16_t color);
    void GIF_displayFrame();
    // Helpers for row-backed framebuffers
    uint16_t *GIF_getRowPtr(uint16_t row);
    uint16_t *GIF_getPrevRowPtr(uint16_t row);
    // void GIF_displayFrame(uint16_t frameX, uint16_t frameY, uint16_t frameW, uint16_t frameH);

    // ========================================================
    // HARDWARE
    // ========================================================

    uint8_t _id = 0;

    spi_host_device_t _host = SPI2_HOST;

    spi_device_handle_t _spi = nullptr;

    uint32_t _freq = 20000000;

    uint16_t _width = TFT_WIDTH;
    uint16_t _height = TFT_HEIGHT;

    uint8_t _rotation = 0;

    uint16_t _curX = 0;
    uint16_t _curY = 0;

    uint16_t _textColor = TFT_WHITE;

    uint8_t _textSize = 1;
    uint8_t _textOrientation = 0;

    int _cs = 5;
    int _dc = 21;
    int _mosi = 23;
    int _miso = 19;
    int _sck = 18;
    int _rst = 22;
    int _bl = -1;

    bool _busAcquired = false;
    spi_transaction_t _qtrans = {};   // BikeVision C5: queued pixel transfer
    bool _qpending = false;
};
