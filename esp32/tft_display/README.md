# TFT Custom Display Driver (ESP32 + ESP-IDF)

## Overview

This project focuses on developing a custom TFT display driver for a 2.8-inch SPI TFT display using an ESP32 Dev Module and ESP-IDF.

The primary goal is to understand low-level display communication, color rendering, graphics primitives, and display driver development without relying on Arduino graphics libraries.

This project serves as the foundation for future applications such as:

- Bike dashboard display
- ESP32-CAM image viewer
- Robot status display
- Touchscreen user interfaces
- LVGL-based GUI applications

---

# Hardware

## Display Information

| Parameter | Value |
|------------|---------|
| Display Size | 2.8 Inch |
| Resolution | 240 x 320 |
| Interface | SPI |
| Color Format | RGB565 |
| Touch Controller | XPT2046 (Confirmed) |
| LCD Controller | Under Investigation |

The display currently operates correctly using the ESP-IDF ILI9341-compatible driver, but the actual LCD controller IC has not yet been positively identified.

---

# ESP32 Connections

| TFT Pin | ESP32 Pin |
|----------|------------|
| MOSI | GPIO23 |
| MISO | GPIO19 |
| SCK | GPIO18 |
| CS | GPIO5 |
| DC | GPIO21 |
| RST | GPIO22 |

---

# Development Environment

- ESP-IDF v5.1.4
- Visual Studio Code
- Ubuntu Linux
- ESP32 Dev Module

---

# Features Implemented

## 1. Display Initialization

Implemented display initialization using the ESP-IDF LCD framework.

Features:

- SPI bus configuration
- LCD panel initialization
- Reset handling
- Display enable sequence

Verified successful communication between ESP32 and TFT display.

---

## 2. Full Screen Color Rendering

Implemented:

```c
display_fill();
```

Capabilities:

- Full-screen color fill
- RGB565 color support
- DMA-based transfers

Used to verify display functionality and color rendering.

---

## 3. RGB565 Color Processing

Investigated and resolved color rendering issues.

### Initial Behavior

Observed incorrect color mapping:

- Red displayed incorrectly
- Green displayed incorrectly
- Blue displayed incorrectly

### Solution

Implemented a custom RGB565 conversion routine:

```c
static uint16_t rgb_to_panel(uint16_t color)
{
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;

    return (b << 11) | (r << 5) | g;
}
```

This corrected the color rendering behavior on the display.

---

## 4. Rectangle Drawing

Implemented:

```c
display_fill_rect();
```

Capabilities:

- Draw colored rectangles
- Partial screen updates
- Region-based rendering

Used for graphics testing and color validation.

---

## 5. Multi-Color Display Test

Created test patterns to verify display operation.

### Four Color Test

- Red
- Green
- Blue
- Yellow

### Eight Color Test

- Red
- Green
- Blue
- Yellow
- Cyan
- Magenta
- White
- Black

Used to validate:

- RGB565 color rendering
- Screen coordinate mapping
- Rectangle drawing functions

---

## 6. Interactive RGB565 Color Tester

Developed a terminal-controlled color testing utility.

Features:

- User enters RGB565 values through terminal
- Display updates immediately
- RGB565 decimal value display
- Hexadecimal value display
- RGB component decoding

Example:

```text
Enter Color: 63488

Color = 63488 (0xF800)
R = 31
G = 0
B = 0
```

The display then shows the selected color.

---

# RGB565 Reference Colors

| Color | Decimal | Hex |
|---------|---------|---------|
| Black | 0 | 0x0000 |
| Blue | 31 | 0x001F |
| Green | 2016 | 0x07E0 |
| Cyan | 2047 | 0x07FF |
| Red | 63488 | 0xF800 |
| Magenta | 63519 | 0xF81F |
| Yellow | 65504 | 0xFFE0 |
| White | 65535 | 0xFFFF |

---

# Technical Challenges Solved

## Color Mapping Investigation

Investigated:

- RGB/BGR ordering
- RGB565 bit layout
- Display color conversion

Implemented custom conversion logic to achieve correct color output.

---

## Partial Screen Rendering

Resolved issues involving:

- Display orientation
- Coordinate mapping
- Rectangle rendering

---

## Interactive Console Input

Implemented terminal-based RGB565 testing utility.

Explored:

- getchar()
- fgets()
- scanf()
- ESP-IDF console behavior

Successfully created a color testing framework for rapid display validation.

---

# Future Improvements

## Text Rendering

Planned:

- Character drawing
- String rendering
- Custom bitmap fonts

## Touchscreen Support

Planned:

- XPT2046 integration
- Touch calibration
- Touch event handling

## Image Rendering

Planned:

- BMP display
- RGB565 image arrays
- JPEG decoding

## GUI Development

Planned:

- LVGL integration
- Buttons
- Menus
- Dashboard screens

## Camera Integration

Planned:

- ESP32-CAM image display
- Live preview support

---

# Project Status

### Completed

- ESP32 ↔ TFT SPI Communication
- Display Initialization
- RGB565 Color Rendering
- Full Screen Fill
- Rectangle Drawing
- Four Color Test Pattern
- Eight Color Test Pattern
- Interactive RGB565 Color Tester
- RGB Component Decoder

### In Progress

- Text Rendering

### Planned

- Touchscreen Support
- Image Rendering
- LVGL Integration
- ESP32-CAM Integration
- Bike Dashboard UI

---

# Author

**Shankar S**

Custom TFT display driver development using ESP32 and ESP-IDF for embedded graphics, robotics, and dashboard applications.