# TFT CUSTOM DISPLAY

## Version 1.0 — Release & Technical Documentation

**Document ID:** TFT-CD-V1.0-DOC  
**Software Version:** V1.0  
**Baseline State:** **FROZEN / OFFICIAL**  
**Document Status:** V1.0 Release Baseline  
**Project Family:** TFT Custom Display  
**Related Project:** BikeVision  

---

# 1. Executive Summary

TFT Custom Display V1.0 is the **stable and frozen baseline** for the TFT Custom Display project.

V1.0 contains the validated TFT/display functionality established during development. It is the protected reference implementation for:

- V1.x maintenance and minor fixes
- V2.x optimization and new features
- Future performance comparisons
- BikeVision display integration

**V1.0 is now frozen.** Major optimization and architectural changes must be developed independently in V2.x.

---

# 2. Version Architecture

```text
TFT Custom Display

├── V1.0
│   └── Stable / Frozen Baseline
│
├── V1.x
│   └── Maintenance / Minor Fixes
│
├── V2.x
│   └── Optimization / Major Development / New Features
│
└── Older_version
    └── Historical Implementations
```

**BikeVision starts from the frozen TFT V1.0.** TFT V2 development continues independently.

---

# 3. V1.0 Validated Scope

The V1 baseline includes the working TFT/display implementation established during development.

Validated areas include:

- TFT initialization
- ILI9341 operation
- 320×240 display operation
- TFT orientation
- Color mapping
- Basic drawing functions
- Pixel writing
- Buffered pixel transfer
- Address-window / SPI transaction handling
- RGB565 image display
- Image scaling
- Sprite/icon functionality
- SPIFFS image integration
- BMP display
- JPEG/JPG display
- GIF file loading
- GIF parsing
- GIF LZW decoding
- GIF color tables
- GIF transparency/compositing
- GIF frame delays
- GIF framebuffer management
- Buffered GIF frame display

The V1 implementation is now the protected baseline.

---

# 4. V1.0 Final Validation Status

## GIF

Final GIF validation has been completed.

Validated:

- Individual GIF playback
- Frames changing
- Playback reaching the end
- No observed corruption during validation
- Four-GIF regression
- Repeated playback
- Long-duration repeated playback
- Changing playback delays

The user reported approximately **1 hour 15 minutes** of repeated playback working successfully.

The user also reported a **10-minute repeated test with changing delays** working without observed instability.

### Final GIF regression set

```text
birds.gif
clock.gif
Radar.gif
tap.gif
```

**GIF V1 validation: PASS**

---

## BMP

The supplied ESP-IDF regression log completed all five BMP tests successfully.

| BMP | Logged display time |
|---|---:|
| tiger.bmp | 0.423 s |
| flower.bmp | 0.422 s |
| Wall_e.bmp | 0.408 s |
| RedRose.bmp | 0.422 s |
| tree.bmp | 0.422 s |

The log concluded with:

```text
ALL BMP TESTS COMPLETE
```

**BMP V1 validation: PASS**

---

## JPEG

JPEG functionality was confirmed working during V1 validation.

**JPEG V1 validation: PASS**

---

# 5. OFFICIAL V1 GIF PERFORMANCE BASELINE

The following values are the **final official V1 GIF benchmarks explicitly locked by the project owner**.

| GIF | Official V1 benchmark |
|---|---:|
| `birds.gif` | **7.433 s** 🔒 |
| `clock.gif` | **10.953 s** 🔒 |
| `Radar.gif` | **18.903 s** 🔒 |
| `tap.gif` | **3.673 s** 🔒 |

### Locked ESP-IDF V1 reference

```text
birds.gif  =  7.433 s
clock.gif  = 10.953 s
Radar.gif  = 18.903 s
tap.gif    =  3.673 s
```

**These four values are the official V1 comparison baseline.**

Older measurements are historical only and must not replace these values.

---

# 6. Benchmark Policy

1. Use actual logged measurements.
2. Preserve explicitly locked benchmark values.
3. Do not invent missing measurements.
4. Do not silently calculate replacement benchmarks.
5. Keep Arduino and ESP-IDF measurements separate.
6. Do not replace an official baseline with an unverified run.
7. If multiple runs exist, identify the approved baseline rather than averaging or choosing arbitrarily.
8. Future V2 measurements must be compared against the locked V1 values.
9. Historical values must not be presented as the current V1 benchmark.

---

# 7. V1 Freeze Status

## Baseline

- [x] V1.0 baseline identified
- [x] V1.x maintenance path established
- [x] V2.x independent development path established
- [x] BikeVision relationship established
- [x] Official GIF benchmarks locked

## GIF V1 Validation

- [x] GIF orientation/display operation validated
- [x] Frame handling validated
- [x] Frame bounds/display operation validated
- [x] Transparency/compositing validated during testing
- [x] Frame-delay behavior validated
- [x] Repeated playback validated
- [x] Long-duration repeated playback validated
- [x] Four-GIF regression completed

## Image Validation

- [x] BMP regression completed
- [x] JPEG functionality confirmed
- [x] GIF functionality confirmed

## Release Documentation

- [x] Final V1 benchmark values recorded
- [x] V1 freeze state documented
- [x] V2 development boundary documented
- [x] BikeVision V1 foundation documented

**V1.0 RELEASE STATUS: FROZEN**

---

# 8. V2.x Optimization / Future Work

Major optimization and new architecture belong in V2.x.

Potential V2 areas:

- GIF partial-frame transfer
- GIF display-transfer optimization
- Framebuffer optimization
- JPEG/JPG optimization
- Advanced BMP improvements
- Memory optimization
- FreeRTOS display architecture
- Double buffering
- Major rendering improvements
- Major new display features

The locked V1 GIF values are the comparison reference:

```text
V1.0 Baseline
     ↓
Measure V2
     ↓
Identify Bottleneck
     ↓
Implement V2 Change
     ↓
Measure
     ↓
Compare Against V1.0
     ↓
Validate
```

The V1 source must not be modified for V2 optimization.

---

# 9. BikeVision Integration

BikeVision starts from the **frozen TFT Custom Display V1.0**.

```text
TFT Custom Display V1.0
        │
        │ Frozen Display Foundation
        ▼
     BikeVision
```

Initial BikeVision architecture:

```text
BikeVision

├── TFT V1.0 display foundation
├── ESP32-CAM
├── Camera capture
├── Image decode
├── 320×240 display
└── Dashboard
```

BikeVision does not need to wait for TFT V2.

---

# 10. Source-of-Truth Hierarchy

1. **Frozen V1.0 source code**
2. **Actual V1.0 hardware configuration**
3. **Actual ESP-IDF logs**
4. **Actual Arduino logs**
5. **Explicitly locked benchmark values**
6. **Approved V1.0 documentation**
7. **Historical documentation**

When sources disagree, investigate the discrepancy.

A missing value remains **unknown** rather than being estimated.

For explicitly locked benchmark values, the locked value is authoritative.

---

# 11. Change Control

Before modifying the project:

```text
Identify Change
      ↓
Review Current Implementation
      ↓
Review Previous Logs
      ↓
Determine Impact
      ↓
Classify V1.x / V2.x
      ↓
Implement Minimum Required Change
      ↓
Test
      ↓
Record Result
```

Existing completed functions should not be unnecessarily reimplemented.

### Version classification

| Change | Version |
|---|---|
| Critical V1 bug | V1.x |
| Small compatibility fix | V1.x |
| Documentation correction | V1.x |
| Minor UI correction | V1.x |
| Major rendering redesign | V2.x |
| Major performance optimization | V2.x |
| New architecture | V2.x |
| Major new feature | V2.x |

---

# 12. Project Direction

```text
                 TFT V1.0
                    │
              🔒 FROZEN
                    │
          ┌─────────┴─────────┐
          │                   │
          ▼                   ▼
      BikeVision            TFT V2.x
          │                   │
          │                   ├── GIF optimization
          │                   ├── Partial-frame transfer
          │                   ├── JPEG/JPG improvements
          │                   ├── BMP improvements
          │                   ├── Memory optimization
          │                   ├── FreeRTOS
          │                   └── Double buffering
          │
          ├── ESP32-CAM
          ├── Camera
          ├── Image decode
          ├── 320×240 display
          └── Dashboard
```

---

# 13. Project State Source of Truth

The engineering project state is maintained separately in:

```text
PROJECT_STATE.md
```

`PROJECT_STATE.md` is the master record for:

- Current project status
- Locked benchmark values
- Validation results
- Important implementation decisions
- Hardware/software environment
- Completed and remaining work
- V1/V2 boundaries
- BikeVision relationship

Whenever a value is explicitly marked **FINAL / OFFICIAL / LOCKED**, it supersedes older measurements for future comparisons.

---

# 14. Revision History

| Revision | Version | Description | Status |
|---|---|---|---|
| 1.0 | V1.0 | Initial baseline documentation | Historical |
| 1.1 | V1.0 | Updated baseline, validation checklist, V2 roadmap and BikeVision relationship | Historical |
| 1.2 | V1.0 | Final V1 validation status and official locked GIF benchmarks | **Current / Frozen** |

---

# Document Control

**Project:** TFT Custom Display  
**Baseline:** V1.0  
**Current State:** **V1.0 FROZEN**  
**V1 GIF Baseline:** **LOCKED**  
**Official GIF Values:** 7.433 / 10.953 / 18.903 / 3.673 s  
**JPEG:** Confirmed working  
**BMP:** Regression PASS  
**GIF:** Regression and repeated-playback PASS  
**BikeVision Foundation:** TFT V1.0  
**Next TFT Development Line:** V2.x  
**Master Project State:** `PROJECT_STATE.md`