# BikeVision & Camera Module

ESP32-CAM based camera and motorcycle dashboard project using JPEG capture, SD-card storage, Wi-Fi/HTTP access, TFT display, GPS, IMU, and a planned real-time wireless video pipeline.

---

## 1. Project Structure

```text
TFT Custom Display
│
├── V1.0
│   └── Frozen / Stable Baseline
│
├── V1.x
│   └── Minor improvements and fixes
│
└── V2.x
    └── Major optimization and new features


Camera Module
│
├── V1.x
│   └── Camera + JPEG + SD + Wi-Fi + HTTP + Video File
│
└── V2.x
    └── Real-time wireless video transport


BikeVision
│
├── Foundation
│   ├── Frozen TFT V1.0
│   └── Camera Module V1.x
│
├── V1.x
│   └── Basic integrated dashboard features
│
└── V2.x
    └── Real-time camera + wireless + advanced dashboard
```

---

# 2. Project Status

## Current Overall Status

### Camera Module

**Phase:** V1.x — Development / Stabilization

```text
Camera             ████████████████████  Complete
SD Card            ████████████████████  Complete
Wi-Fi              ████████████████████  Complete
HTTP               ████████████████████  Working
Photo              ████████████████████  Working
Video File         ░░░░░░░░░░░░░░░░░░░░  Planned
Testing            ███████░░░░░░░░░░░░░  In progress
```

### BikeVision

**Phase:** Foundation / V2 architecture planning

```text
TFT Foundation     ████████████████████  Existing Foundation
Camera Foundation  ███████████████████░  In Development
GPS                ░░░░░░░░░░░░░░░░░░░░  Planned / Integration
IMU                ░░░░░░░░░░░░░░░░░░░░  Planned / Integration
ESP-NOW            ░░░░░░░░░░░░░░░░░░░░  Planned
JPEG Transport     ░░░░░░░░░░░░░░░░░░░░  Planned
JPEG Decode        ░░░░░░░░░░░░░░░░░░░░  Planned
RGB565/TFT Video   ░░░░░░░░░░░░░░░░░░░░  Planned
```

---

# 3. Development Rules

1. TFT V1.0 is the frozen/stable baseline.
2. TFT V1.0 must not be modified while developing V2.
3. TFT V1.x contains minor fixes and improvements.
4. TFT V2.x contains major optimization and new features.
5. BikeVision uses the frozen TFT V1.0 as its display foundation.
6. Camera Module V1.x must be stabilized before major V2 architecture work.
7. Existing completed functions should not be reimplemented unnecessarily.
8. New features must be classified as V1.x, V2.x, or Future before implementation.
9. Actual ESP-IDF and Arduino logs must be kept separate.
10. Benchmark values must come from actual logs; missing values must not be invented or calculated.
11. Explicitly locked benchmark values are the official baseline.
12. When a version is frozen, technical documentation should be updated.

---

# 4. Camera Module

## 4.1 Purpose

The Camera Module is the independent camera subsystem used as the foundation for BikeVision.

## 4.2 Current Architecture

```text
                 ┌──────────────┐
                 │    OV3660    │
                 │    Camera    │
                 └──────┬───────┘
                        │
                        ▼
                 JPEG 640×480
                        │
             ┌──────────┴──────────┐
             │                     │
             ▼                     ▼
         SD Card                 Wi-Fi
             │                     │
             ▼                     ▼
       photo.jpg              HTTP Server
                                   │
                                   ▼
                              Web Browser
```

## 4.3 Current Camera Configuration

- ESP-IDF: 5.1.4
- Camera detected by current firmware: OV3660
- Image format: JPEG
- Resolution: 640×480 (VGA)
- JPEG quality setting: 12
- PSRAM detected: 8 MB
- SD card: mounted successfully
- HTTP server: port 80
- Wi-Fi: Station mode

> Hardware pin configuration is intentionally maintained separately from this README feature list.

---

# 5. Camera Functions

| ID | Function | Status | Version |
|---|---|---|---|
| CAM-01 | Camera initialization | ✅ Complete | V1.x |
| CAM-02 | Camera sensor detection | ✅ Complete | V1.x |
| CAM-03 | OV3660 detection | ✅ Complete | V1.x |
| CAM-04 | XCLK configuration | ✅ Complete | V1.x |
| CAM-05 | JPEG frame capture | ✅ Complete | V1.x |
| CAM-06 | 640×480 VGA capture | ✅ Complete | V1.x |
| CAM-07 | Camera warm-up | ✅ Complete | V1.x |
| CAM-08 | JPEG header verification | ✅ Complete | V1.x |
| CAM-09 | Frame-buffer management | ✅ Complete | V1.x |
| CAM-10 | Repeated capture testing | 🔧 Testing | V1.x |
| CAM-11 | Camera error recovery | 🔧 Remaining | V1.x |
| CAM-12 | Image-quality tuning | 🔧 Remaining | V1.x |

---

# 6. SD Card Functions

| ID | Function | Status | Version |
|---|---|---|---|
| SD-01 | SD card initialization | ✅ Complete | V1.x |
| SD-02 | FAT filesystem mounting | ✅ Complete | V1.x |
| SD-03 | SD card detection | ✅ Complete | V1.x |
| SD-04 | JPEG file creation | ✅ Complete | V1.x |
| SD-05 | JPEG file writing | ✅ Complete | V1.x |
| SD-06 | `photo.jpg` storage | ✅ Complete | V1.x |
| SD-07 | Repeated file overwrite testing | 🔧 Testing | V1.x |
| SD-08 | SD write error handling | 🔧 Remaining | V1.x |
| SD-09 | Storage-capacity monitoring | ⬜ Planned | V1.x |

---

# 7. Wi-Fi Functions

| ID | Function | Status | Version |
|---|---|---|---|
| WIFI-01 | NVS initialization | ✅ Complete | V1.x |
| WIFI-02 | Wi-Fi STA mode | ✅ Complete | V1.x |
| WIFI-03 | SSID configuration | ✅ Complete | V1.x |
| WIFI-04 | Password configuration | ✅ Complete | V1.x |
| WIFI-05 | Wi-Fi connection | ✅ Complete | V1.x |
| WIFI-06 | IP acquisition | ✅ Complete | V1.x |
| WIFI-07 | Connection retry | ✅ Complete | V1.x |
| WIFI-08 | Wi-Fi reconnect testing | 🔧 Testing | V1.x |
| WIFI-09 | Connection error handling | 🔧 Remaining | V1.x |

---

# 8. HTTP / Web Functions

Current web interface:

```text
ESP32-CAM
     │
     ▼
   Wi-Fi
     │
     ▼
 HTTP Server
     │
     ▼
 Browser
```

| ID | Function | Status | Version |
|---|---|---|---|
| HTTP-01 | HTTP server startup | ✅ Complete | V1.x |
| HTTP-02 | Web dashboard | ✅ Complete | V1.x |
| HTTP-03 | `/` endpoint | ✅ Complete | V1.x |
| HTTP-04 | `/photo` endpoint | ✅ Complete | V1.x |
| HTTP-05 | `/capture` endpoint | ✅ Complete | V1.x |
| HTTP-06 | `/download` endpoint | ✅ Complete | V1.x |
| HTTP-07 | Browser photo display | ✅ Complete | V1.x |
| HTTP-08 | Browser new-photo capture | ✅ Complete | V1.x |
| HTTP-09 | Browser cache prevention | ✅ Complete | V1.x |
| HTTP-10 | HTTPD stack stability | 🔧 Testing | V1.x |
| HTTP-11 | Repeated HTTP requests | 🔧 Testing | V1.x |
| HTTP-12 | HTTP error handling | 🔧 Remaining | V1.x |

---

# 9. Video File Features

Video file capture is separate from real-time wireless video streaming.

## 9.1 V1.x Video Concept

```text
Camera
   │
   ▼
JPEG Frame 1
JPEG Frame 2
JPEG Frame 3
     .
     .
JPEG Frame N
   │
   ▼
SD Card
   │
   ▼
Recorded sequence / video file
```

| ID | Function | Status | Version |
|---|---|---|---|
| VID-01 | Continuous JPEG frame capture | ⬜ Planned | V1.x |
| VID-02 | Frame timing control | ⬜ Planned | V1.x |
| VID-03 | Video recording start | ⬜ Planned | V1.x |
| VID-04 | Video recording stop | ⬜ Planned | V1.x |
| VID-05 | Recording duration control | ⬜ Planned | V1.x |
| VID-06 | Store recorded frames on SD | ⬜ Planned | V1.x |
| VID-07 | Recording file management | ⬜ Planned | V1.x |
| VID-08 | 5-second recording test | ⬜ Planned | V1.x |
| VID-09 | 10-second recording test | ⬜ Planned | V1.x |
| VID-10 | Long-duration recording test | ⬜ Planned | V1.x |
| VID-11 | Dropped-frame testing | ⬜ Planned | V1.x |
| VID-12 | Actual FPS measurement | ⬜ Planned | V1.x |
| VID-13 | SD write-performance testing | ⬜ Planned | V1.x |
| VID-14 | Recorded video/file screening | ⬜ Planned | V1.x |
| VID-15 | Browser recorded-video viewing | ⬜ Planned | V1.x |
| VID-16 | Recorded-file download | ⬜ Planned | V1.x |

### Video Format Note

A sequence of JPEG frames is not automatically an MP4 video.

Before implementing a final video-file format, the recording/container approach must be selected and tested. The README should only call it MP4 after an actual MP4/container implementation exists.

---

# 10. Camera Module Testing

## Photo Testing

- [ ] Repeated photo capture
- [ ] Repeated browser refresh
- [ ] Capture → save → display verification
- [ ] Verify new capture is actually different when the scene changes
- [ ] Verify JPEG integrity
- [ ] Verify SD file integrity

## Wi-Fi Testing

- [ ] Normal connection
- [ ] Connection retry
- [ ] Router/hotspot disconnect
- [ ] Reconnection after network recovery
- [ ] Browser access after reconnect

## HTTP Testing

- [ ] `/`
- [ ] `/photo`
- [ ] `/capture`
- [ ] `/download`
- [ ] Repeated requests
- [ ] Repeated capture
- [ ] Browser disconnect handling
- [ ] HTTPD stack stability

## Video Testing

- [ ] 5-second recording
- [ ] 10-second recording
- [ ] Long-duration recording
- [ ] Actual FPS logging
- [ ] Dropped-frame detection
- [ ] SD write stability
- [ ] Storage usage
- [ ] Recorded-file integrity
- [ ] Playback/screening on PC/phone
- [ ] Browser recorded-video viewing

---

# 11. Image Quality

Current image output is functional but can be improved.

Potential V1.x tuning areas:

- Brightness
- Contrast
- Saturation
- Exposure
- White balance
- Gain control
- JPEG quality
- Resolution
- Sharpness, where supported by the camera sensor

Image-quality changes should be tested one parameter at a time where practical, with actual captured images used for comparison.

---

# 12. Camera Module V1.x Freeze Criteria

The Camera Module should not be frozen until the following are validated:

```text
[✓] Camera initialization
[✓] JPEG capture
[✓] SD storage
[✓] Wi-Fi
[✓] HTTP server
[✓] Browser photo viewing
[✓] Browser-triggered capture
[✓] Photo download

[ ] Repeated capture stability
[ ] HTTP stability
[ ] Wi-Fi reconnect validation
[ ] SD reliability testing
[ ] Image-quality tuning
[ ] Video recording
[ ] Video recording testing
[ ] Video screening/playback
[ ] Error/recovery testing
[ ] Final documentation
```

When these are complete:

**Camera Module V1.x → Frozen**

---

# 13. BikeVision

## 13.1 Purpose

BikeVision combines the camera, TFT display, GPS, IMU and wireless communication into a motorcycle/bike-oriented information and vision system.

## 13.2 Main Architecture

```text
             ┌──────────────┐
             │   ESP32-CAM  │
             │    Camera    │
             └──────┬───────┘
                    │
                  JPEG
                    │
                    ▼
              Wireless Link
                    │
                    ▼
             ┌──────────────┐
             │ ESP32        │
             │ Receiver     │
             └──────┬───────┘
                    │
              JPEG Decode
                    │
                 RGB565
                    │
                    ▼
             ┌──────────────┐
             │ ILI9341 TFT  │
             │ Dashboard    │
             └──────────────┘

 GPS ───────────────┐
                    ├──→ Dashboard
 IMU ───────────────┘
```

---

# 14. BikeVision Hardware / Subsystems

| ID | Feature | Status | Version |
|---|---|---|---|
| BV-01 | ESP32 camera module | 🔧 Foundation | V1.x |
| BV-02 | OV3660 camera | 🔧 Foundation | V1.x |
| BV-03 | TFT display | 🟢 Existing Foundation | V1.x |
| BV-04 | GPS | ⬜ Planned / Integration | V1.x |
| BV-05 | IMU | ⬜ Planned / Integration | V1.x |
| BV-06 | Wireless communication | ⬜ Planned | V2.x |
| BV-07 | JPEG transport | ⬜ Planned | V2.x |
| BV-08 | JPEG fragmentation / reassembly | ⬜ Planned | V2.x |
| BV-09 | JPEG decoding | ⬜ Planned | V2.x |
| BV-10 | RGB565 display pipeline | ⬜ Planned | V2.x |
| BV-11 | Real-time camera display | ⬜ Planned | V2.x |

---

# 15. BikeVision Dashboard Functions

| ID | Function | Status | Version |
|---|---|---|---|
| DASH-01 | TFT initialization | 🟢 Foundation | V1.x |
| DASH-02 | Basic dashboard UI | 🟢 Foundation | V1.x |
| DASH-03 | Camera display area | ⬜ Planned | V2.x |
| DASH-04 | GPS data display | ⬜ Planned / Integration | V1.x |
| DASH-05 | Speed display | ⬜ Planned | V1.x |
| DASH-06 | Direction / heading display | ⬜ Planned | V1.x |
| DASH-07 | IMU data display | ⬜ Planned / Integration | V1.x |
| DASH-08 | Camera + sensor dashboard | ⬜ Planned | V2.x |
| DASH-09 | Real-time camera feed | ⬜ Planned | V2.x |
| DASH-10 | UI performance optimization | ⬜ Planned | V2.x |

---

# 16. BikeVision V2 Wireless Video Pipeline

This is the major V2 architecture.

```text
ESP32-CAM
    │
    ▼
OV3660
    │
    ▼
JPEG Frame
    │
    ▼
Frame Fragmentation
    │
    ▼
ESP-NOW
    │
    ▼
ESP32 Receiver
    │
    ▼
Packet Reassembly
    │
    ▼
Complete JPEG
    │
    ▼
JPEG Decode
    │
    ▼
RGB565
    │
    ▼
SPI
    │
    ▼
ILI9341 TFT
```

## V2 Functions

| ID | Function | Status | Version |
|---|---|---|---|
| V2-01 | ESP-NOW initialization | ⬜ Planned | V2.x |
| V2-02 | Camera frame acquisition | ⬜ Planned | V2.x |
| V2-03 | JPEG frame identification | ⬜ Planned | V2.x |
| V2-04 | JPEG fragmentation | ⬜ Planned | V2.x |
| V2-05 | Packet numbering | ⬜ Planned | V2.x |
| V2-06 | Frame ID management | ⬜ Planned | V2.x |
| V2-07 | Packet transmission | ⬜ Planned | V2.x |
| V2-08 | Packet reception | ⬜ Planned | V2.x |
| V2-09 | Packet reassembly | ⬜ Planned | V2.x |
| V2-10 | Missing-packet detection | ⬜ Planned | V2.x |
| V2-11 | Frame validation | ⬜ Planned | V2.x |
| V2-12 | JPEG decoding | ⬜ Planned | V2.x |
| V2-13 | JPEG → RGB565 | ⬜ Planned | V2.x |
| V2-14 | TFT rendering | ⬜ Planned | V2.x |
| V2-15 | Frame-rate measurement | ⬜ Planned | V2.x |
| V2-16 | End-to-end latency measurement | ⬜ Planned | V2.x |
| V2-17 | Wireless reliability testing | ⬜ Planned | V2.x |
| V2-18 | Memory optimization | ⬜ Planned | V2.x |
| V2-19 | CPU optimization | ⬜ Planned | V2.x |
| V2-20 | Real-time video optimization | ⬜ Planned | V2.x |

> JPEG data must not be treated as one large ESP-NOW packet. The V2 implementation requires fragmentation, packet identification, reassembly, and appropriate reliability handling.

---

# 17. BikeVision V1.x Features

V1.x should focus on basic integrated dashboard functionality without major architectural changes.

| ID | Feature | Status |
|---|---|---|
| BV1-01 | TFT dashboard foundation | 🟢 Existing |
| BV1-02 | Camera module foundation | 🔧 In Development |
| BV1-03 | GPS integration | ⬜ Planned |
| BV1-04 | Speed information | ⬜ Planned |
| BV1-05 | Heading information | ⬜ Planned |
| BV1-06 | IMU integration | ⬜ Planned |
| BV1-07 | Sensor status | ⬜ Planned |
| BV1-08 | Camera status | ⬜ Planned |
| BV1-09 | Basic dashboard integration | ⬜ Planned |
| BV1-10 | V1.x stability testing | ⬜ Planned |

---

# 18. Future BikeVision Features

These ideas should not automatically become implementation tasks.

Possible future areas include:

## Navigation

- GPS navigation information
- Heading
- Route information
- Location information

## Riding Information

- Speed
- Distance
- Trip information
- Sensor information

## Camera

- Real-time camera feed
- Recording control
- Image capture
- Camera status

## IMU

- Orientation
- Motion information
- Acceleration information
- Tilt information

## Dashboard

- Configurable widgets
- Warning/status indicators
- Custom layouts
- Display optimization

---

# 19. Version Classification

## V1.0

Stable/frozen baseline.

```text
V1.0 = Stable Foundation
```

Do not modify the frozen baseline during V2 development.

## V1.x

Minor improvements, fixes, stabilization, testing, and features that do not require a major architectural change.

Examples:

- Image-quality tuning
- HTTP reliability
- Wi-Fi recovery
- SD reliability
- Video file recording
- Video recording testing
- Browser improvements

## V2.x

Major architectural changes and advanced features.

Examples:

- ESP-NOW
- Real-time wireless video
- JPEG fragmentation
- JPEG reassembly
- JPEG decoding
- RGB565 pipeline
- Real-time TFT camera display
- Major performance optimization

## Future

Ideas requiring further research, hardware, architecture, or product decisions.

---

# 20. New Feature / Idea Tracking

Every new idea should first be added here.

| ID | New Idea | Project | Classification | Status |
|---|---|---|---|---|
| IDEA-001 | — | Camera / BikeVision | V1.x / V2.x / Future | Proposed |

### Classification process

```text
New Idea
   │
   ▼
Does it require major architecture change?
   │
   ├── No ──→ V1.x
   │
   └── Yes ─→ V2.x
              │
              ▼
       If not ready/researched
              │
              ▼
           Future
```

---

# 21. Testing and Benchmarks

All performance measurements must be based on actual logs.

## Rules

- Keep Arduino measurements separate from ESP-IDF measurements.
- Do not invent benchmark values.
- Do not calculate missing benchmark values and present them as measured values.
- Record the exact logged values.
- Record the firmware/version used for each benchmark.
- Record resolution, JPEG configuration, and test conditions where relevant.
- Explicitly locked benchmark values become the official baseline.

## Camera Test Data

| Test | Result | Measurement | Firmware | Status |
|---|---|---:|---|---|
| JPEG capture | — | — | ESP-IDF | ⬜ |
| JPEG size | — | — | ESP-IDF | ⬜ |
| Video FPS | — | — | ESP-IDF | ⬜ |
| SD write performance | — | — | ESP-IDF | ⬜ |
| HTTP transfer | — | — | ESP-IDF | ⬜ |
| End-to-end latency | — | — | ESP-IDF | ⬜ |

Values should only be filled after they are measured and logged.

---

# 22. Known Issues

| ID | Issue | Project | Status |
|---|---|---|---|
| ISSUE-001 | HTTPD stack overflow occurred during early web-page testing | Camera Module | 🔧 Addressed / Retest |
| ISSUE-002 | Image quality can be improved | Camera Module | 🔧 V1.x |
| ISSUE-003 | Video recording not implemented | Camera Module | ⬜ Planned |
| ISSUE-004 | Real-time wireless video not implemented | BikeVision | ⬜ V2.x |

---

# 23. Completed

## Camera Module

- Camera initialization
- Camera detection
- OV3660 detection
- JPEG capture
- 640×480 image capture
- Camera warm-up
- JPEG verification
- PSRAM operation
- SD card mounting
- JPEG storage
- Wi-Fi STA
- Wi-Fi connection
- IP acquisition
- HTTP server
- Web dashboard
- Browser photo display
- Browser-triggered photo capture
- Photo download
- Browser cache handling

## BikeVision

- TFT V1.0 foundation
- Camera module identified as the camera foundation
- V2 wireless video architecture defined

---

# 24. Remaining

## Camera Module V1.x

- Repeated photo stability testing
- HTTP stability testing
- Wi-Fi reconnect validation
- SD reliability testing
- Image-quality tuning
- Error/recovery testing
- Video file capture
- Video recording testing
- Video screening/playback
- Recorded-file handling
- Final documentation
- V1.x freeze

## BikeVision V1.x

- GPS integration
- IMU integration
- Dashboard sensor integration
- Basic integrated dashboard
- V1.x stability testing

## BikeVision V2.x

- ESP-NOW
- JPEG fragmentation
- Packet transmission
- Packet reception
- Packet reassembly
- JPEG decoding
- RGB565 conversion
- TFT camera rendering
- Real-time video
- End-to-end performance optimization

---

# 25. Future Ideas

| ID | Idea | Classification | Status |
|---|---|---|---|
| IDEA-001 | — | Future | Proposed |

New ideas should be added to the table first and classified before implementation.

---

# 26. Recommended Development Order

```text
CAMERA MODULE
     │
     ├── Camera + JPEG              ✅
     ├── SD Card                    ✅
     ├── Wi-Fi                      ✅
     ├── HTTP                       ✅
     ├── Browser Photo              ✅
     ├── Browser Capture            ✅
     ├── Download                   ✅
     │
     ├── Stability Testing          🔧
     ├── Image Quality              🔧
     ├── Video File Capture         ⬜
     ├── Video Testing              ⬜
     ├── Video Screening            ⬜
     └── V1.x Freeze                ⬜
              │
              ▼
        BIKVISION V1.x
              │
              ├── TFT Foundation
              ├── Camera Foundation
              ├── GPS
              ├── IMU
              └── Dashboard
                      │
                      ▼
                BIKVISION V2.x
                      │
                      ├── ESP-NOW
                      ├── JPEG Transport
                      ├── Fragmentation
                      ├── Receiver
                      ├── JPEG Decode
                      ├── RGB565
                      ├── TFT Video
                      └── Optimization
```

---

# 27. Version Freeze Documentation

When a version becomes stable, record:

- Version number
- Date
- Hardware configuration
- Software/ESP-IDF version
- Completed functions
- Known limitations
- Test results
- Locked benchmark values
- Build/flash instructions
- Architecture diagram
- Changes from previous version

This README should be updated whenever a function moves between **Planned → In Development → Testing → Complete → Frozen**, or when a new feature is proposed.
