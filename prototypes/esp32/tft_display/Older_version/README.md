# TFT Custom Display Driver (ESP32 + ESP-IDF)

## Overview

This project focuses on developing a custom TFT graphics library from scratch for a 2.8-inch SPI TFT display using an ESP32 Dev Module and ESP-IDF.

The objective is to understand low-level graphics rendering, display driver development, RGB565 color processing, bitmap rendering, and graphics primitives without relying on Arduino graphics libraries such as Adafruit GFX or TFT_eSPI.

This graphics library serves as the foundation for future embedded GUI applications including:

* BikeVision Dashboard
* ESP32-CAM Image Viewer
* Robot Status Display
* Touchscreen User Interface
* LVGL Integration
* Embedded Dashboard Applications

---

# Hardware

## Display Information

| Parameter        | Value                             |
| ---------------- | --------------------------------- |
| Display Size     | 2.8 Inch                          |
| Resolution       | 240 × 320                         |
| Interface        | SPI                               |
| Color Format     | RGB565                            |
| Touch Controller | XPT2046                           |
| Display Driver   | ESP-IDF ILI9341 Compatible Driver |

---

# Development Environment

* ESP-IDF v5.1.4
* Visual Studio Code
* Ubuntu Linux
* ESP32 Dev Module

---

# Graphics Library Features

## Display Driver

* Display Initialization
* SPI Bus Configuration
* Panel Reset
* Display Enable
* RGB565 Support

Functions:

```c
display_init();
display_fill();
```

---

## Pixel Rendering

Functions:

```c
display_draw_pixel();
```

Features:

* Individual pixel rendering
* RGB565 color drawing
* Foundation for all graphics primitives

---

## Line Rendering

Functions:

```c
display_draw_hline();
display_draw_vline();
display_draw_line();
```

Implemented using Bresenham's Line Algorithm.

Supports:

* Horizontal lines
* Vertical lines
* Arbitrary angled lines

---

## Rectangle Rendering

Functions:

```c
display_draw_rect();
display_fill_rect();
```

Supports:

* Rectangle outlines
* Filled rectangles
* Partial screen updates

---

## Circle Rendering

Functions:

```c
display_draw_circle();
display_fill_circle();
```

Implemented using the Midpoint Circle Algorithm.

Supports:

* Circle outlines
* Filled circles

---

## Text Rendering (In Progress)

Functions:

```c
display_draw_char();
display_draw_string();
```

Current Features

* 5×7 bitmap font
* Character rendering
* String rendering
* RGB565 text color

Planned

* Complete ASCII font
* Scalable fonts
* Multiple font sizes
* Text alignment
* Integer rendering
* Floating-point rendering

---

# RGB565 Color Processing

Implemented:

```c
rgb_to_panel();
```

Features:

* RGB565 conversion
* RGB/BGR correction
* Color decoding
* Color validation

---

# Graphics Testing

Completed Tests

* Full Screen Fill
* RGB565 Color Test
* Four Color Pattern
* Eight Color Pattern
* Rectangle Test
* Line Test
* Circle Test
* Filled Circle Test
* Character Rendering Test
* String Rendering Test

---

# Interactive RGB565 Color Tester

Implemented terminal-controlled color testing.

Supports:

* Decimal RGB565 input
* Hexadecimal display
* RGB component decoding
* Real-time display updates

---

# Technical Challenges Solved

* SPI Communication
* ESP-IDF LCD Driver Integration
* RGB565 Color Conversion
* Display Orientation
* Coordinate Mapping
* Partial Screen Rendering
* Graphics Primitive Development
* Character Bitmap Rendering

---

# Current Graphics Library

## Display

```c
display_init();
display_fill();
```

## Pixel

```c
display_draw_pixel();
```

## Line

```c
display_draw_hline();
display_draw_vline();
display_draw_line();
```

## Rectangle

```c
display_draw_rect();
display_fill_rect();
```

## Circle

```c
display_draw_circle();
display_fill_circle();
```

## Text

```c
display_draw_char();
display_draw_string();
```

---

# Current Status

## Completed

* Display Initialization
* RGB565 Color Processing
* Pixel Rendering
* Horizontal Line Drawing
* Vertical Line Drawing
* General Line Drawing
* Rectangle Drawing
* Filled Rectangle Drawing
* Circle Drawing
* Filled Circle Drawing
* Character Rendering
* String Rendering
* Graphics Validation Tests

## In Progress

* Complete ASCII Font
* Bitmap Rendering

## Planned

* Bitmap Rendering
* RGB565 Image Rendering
* Touchscreen Driver
* JPEG/BMP Rendering
* Animation Engine
* Camera Frame Rendering
* LVGL Integration
* BikeVision Dashboard

---

# Future Roadmap

### Phase 1

* Complete Text Rendering

### Phase 2

* Bitmap Rendering

### Phase 3

* Image Rendering

### Phase 4

* Camera Rendering

### Phase 5

* Dashboard UI

### Phase 6

* Touchscreen Support

### Phase 7

* LVGL Integration

### Phase 8

* BikeVision Dashboard

---

# Author

**Shankar S**

Developing a custom embedded graphics library using ESP-IDF for ESP32-based TFT displays with a focus on graphics rendering, embedded GUI development, robotics, and real-time dashboard applications.
