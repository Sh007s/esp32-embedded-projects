# 🏍️ BikeVision Dashboard

**A real-time smart motorcycle dashboard built using ESP32-CAM, ESP32-S3, GPS, IMU sensors, and a TFT display.**

BikeVision Dashboard combines live camera streaming, GPS navigation, environmental sensing, motion tracking, and an interactive display into a compact embedded platform designed for motorcycle applications.

---

## 🚧 Project Status

**Current Phase:** Hardware Bring-up & Display Development

### Completed

* TFT Display Bring-up
* Color Rendering Validation
* Basic Graphics Rendering
* User Input Screen Development

### In Progress

* BMP Image Rendering
* Camera-to-Display Pipeline
* GPS Integration
* Sensor Integration

### Planned

* Real-Time Dashboard UI
* GPS Navigation Screen
* Ride Data Logging
* Touchscreen Interface
* Environmental Monitoring

---

## 📋 Table of Contents

* Project Overview
* System Architecture
* Hardware Components
* Wiring Guide
* Pin Configuration
* Cost Summary
* Constraints & Limitations
* ESP-IDF Configuration
* Development Roadmap

---

## 🎯 Project Overview

### Features

* 📷 Live camera feed using OV2640 camera
* 🛰️ Real-time GPS speed and location tracking
* 📐 Lean angle and motion monitoring using ICM20948
* 🌡️ Environmental monitoring using BME280
* 🎛️ Rotary encoder based user interface
* 💾 Ride data logging to microSD card
* 📺 Real-time TFT dashboard display
* 📡 Wi-Fi and Bluetooth connectivity
* ⚡ Built with ESP-IDF for high performance

### Target Hardware

| Item               | Specification                 |
| ------------------ | ----------------------------- |
| Main Controller    | ESP32-CAM (4MB PSRAM)         |
| Display Controller | ESP32 / ESP32-S3              |
| Camera             | OV2640                        |
| Display            | 2.4" SPI TFT (ILI9341/ST7789) |
| Framework          | ESP-IDF v5.1+                 |

---

## 🏗️ System Architecture

```text
OV2640 Camera
      │
      ▼
 ESP32-CAM
      │
      ▼
 ESP32-S3 Display Controller
      │
      ├── TFT Display
      ├── GPS Module
      ├── ICM20948 IMU
      ├── BME280 Sensor
      ├── Rotary Encoder
      └── microSD Card
```

---

## 🧰 Hardware Components

### Core Processing

* ESP32-CAM with OV2640 Camera
* ESP32 DevKit / ESP32-S3
* FTDI Programmer

### Display System

* 2.8" SPI TFT Display
* ILI9341 or ST7789 Controller

### Sensors

* ATGM336H GPS Module
* ICM20948 9-Axis IMU
* BME280 Environmental Sensor

### User Input

* EC11 Rotary Encoder

### Storage

* External SPI microSD Card Module

---

## 🔌 Wiring Guide

Refer to the wiring tables and hardware documentation inside the `/hardware` directory.

---

## 📊 Estimated Project Cost

| Category             | Estimated Cost      |
| -------------------- | ------------------- |
| Processing & Display | ₹1,200 - ₹1,800     |
| Sensors              | ₹2,850 - ₹3,250     |
| Power System         | ₹250 - ₹500         |
| User Interface       | ₹300 - ₹500         |
| Storage              | ₹550 - ₹850         |
| Enclosure & Wiring   | ₹600 - ₹1,000       |
| **Total**            | **₹5,750 - ₹7,900** |

---

## ⚠️ Known Constraints

* ESP32-CAM SD interface conflicts with TFT usage
* GPIO12 boot configuration must be handled carefully
* Camera and TFT consume significant memory bandwidth
* Stable power supply required during camera operation

---

## 📁 Repository Structure

```text
bikevision_dashboard/
├── docs/
├── hardware/
├── images/
├── firmware/
│   ├── esp32_cam/
│   └── esp32s3_display/
└── README.md
```

---

## 🔧 Development Environment

* ESP-IDF v5.1+
* Visual Studio Code
* Ubuntu Linux
* ESP32 Toolchain

---

## 🚀 Roadmap

### Phase 1

* Display Bring-up
* Graphics Rendering
* User Interface Prototypes

### Phase 2

* BMP Image Rendering
* Camera Integration
* Sensor Drivers

### Phase 3

* Dashboard UI
* GPS Navigation
* Data Logging

### Phase 4

* Field Testing
* Waterproof Enclosure
* Motorcycle Integration

---

## 📜 License

MIT License

---

**BikeVision Dashboard — Ride smarter, safer, and track everything in real time.**
