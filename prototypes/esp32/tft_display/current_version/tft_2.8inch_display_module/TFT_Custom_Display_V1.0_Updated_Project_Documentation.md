# TFT CUSTOM DISPLAY
## Version 1.0 — Release & Technical Documentation

**Document ID:** TFT-CD-V1.0-DOC  
**Software Version:** V1.0  
**Baseline State:** **DEFINED / FREEZE PENDING**  
**Document Status:** Updated Project Baseline  
**Project Family:** TFT Custom Display  
**Related Project:** BikeVision  

---

# 1. Executive Summary

TFT Custom Display V1.0 is the **stable baseline definition** for the TFT Custom Display project.

The V1.0 baseline contains the currently working TFT/display functionality and the locked GIF benchmark values established during project development.

V1.0 is intended to become the protected reference implementation for V1.x maintenance, V2.x optimization/new features, performance comparison, and BikeVision display integration.

**Important:** The V1.0 baseline is defined, but final hardware, regression, stability, and build verification remain pending. Therefore this document does not claim that the final release freeze is complete.

---

# 2. Version Architecture

```text
TFT Custom Display
│
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

# 3. V1.0 Scope

The V1 baseline includes the working TFT/display implementation established in the current source baseline.

Known working areas include:

- TFT initialization
- TFT orientation / 320×240 operation
- Color mapping
- Basic drawing functions
- Pixel writing
- Buffered pixel transfer
- Address-window / SPI transaction handling
- RGB565 image display
- Image scaling
- Sprite/icon functionality
- File-system image integration
- GIF file loading
- GIF parsing
- GIF LZW decoding
- GIF color tables
- GIF transparency/compositing
- GIF frame delays
- GIF framebuffer management
- Buffered GIF frame display

The exact function-level inventory must be taken from the frozen V1 source tree when the freeze is performed.

---

# 4. V1.0 Remaining Work Before Freeze

These are **final validation items**, not major optimization:

### GIF validation
- [ ] Verify 240×320 GIF → 320×240 display orientation
- [ ] Verify native 320×240 GIF display
- [ ] Verify frame positioning and bounds
- [ ] Verify transparency/compositing
- [ ] Verify disposal behavior
- [ ] Verify frame-delay behavior
- [ ] Run repeated GIF playback
- [ ] Check memory stability
- [ ] Run final four-GIF regression

### Final regression set

```text
birds.gif
clock.gif
Radar.gif
tap.gif
```

After these checks pass, V1.0 can be formally frozen.

---

# 5. Official V1 GIF Performance Baseline

The following values are the **explicitly locked V1 baseline values supplied during project development**.

| GIF | Original GIF duration | Arduino BEFORE delay | Arduino AFTER delay | ESP32 V1 current |
|---|---:|---:|---:|---:|
| birds.gif | 4.720 s | 4.137 s | 8.853 s | **7.034 s** |
| clock.gif | 7.920 s | 1.672 s | 9.592 s | **10.394 s** |
| Radar.gif | 10.100 s | 2.763 s | 12.863 s | **18.924 s** |
| tap.gif | 1.750 s | 1.804 s | 3.554 s | **3.284 s** |

### Locked ESP32 V1 reference

```text
birds.gif   =  7.034 s
clock.gif   = 10.394 s
Radar.gif   = 18.924 s
tap.gif     =  3.284 s
```

These values are the official V1 comparison reference and must not be replaced by another run unless the V1 baseline is deliberately updated.

Arduino and ESP-IDF measurements remain separate.

---

# 6. Benchmark Policy

1. Use actual logged measurements.
2. Preserve explicitly locked benchmark values.
3. Do not invent missing measurements.
4. Do not silently calculate replacement benchmarks.
5. Keep Arduino and ESP-IDF measurements separate.
6. Do not replace an official baseline with an unverified run.
7. If multiple runs exist, identify the approved baseline rather than averaging or choosing arbitrarily.
8. Compare all future V2 measurements against the locked V1 values.

---

# 7. V2.x Optimization / Future Work

Major optimization and new architecture belong in V2.x.

Current V2 candidates:

- GIF partial-frame transfer
- GIF display-transfer optimization
- Framebuffer optimization
- JPEG/JPG support
- Advanced BMP improvements
- Memory optimization
- FreeRTOS display architecture
- Double buffering
- Major rendering improvements
- Major new display features

The major current performance target is the GIF display-transfer path, with **Radar V1 = 18.924 s** as a locked reference.

V2 optimization must follow:

```text
V1.0 Baseline
      ↓
Measure
      ↓
Identify Bottleneck
      ↓
Develop V2.x Change
      ↓
Measure
      ↓
Compare Against V1.0
      ↓
Validate
```

---

# 8. BikeVision Integration

BikeVision starts from the **frozen TFT Custom Display V1.0**.

```text
TFT Custom Display V1.0
        │
        │ Frozen Display Foundation
        ▼
     BikeVision
```

Initial BikeVision development:

```text
BikeVision
    │
    ├── TFT V1 display foundation
    ├── ESP32-CAM
    ├── Camera capture
    ├── Image decode
    ├── 320×240 display
    └── Initial dashboard
```

BikeVision does not need to wait for TFT V2.

---

# 9. Source-of-Truth Hierarchy

1. **Frozen V1.0 source code**
2. **Actual V1.0 hardware configuration**
3. **Actual ESP-IDF logs**
4. **Actual Arduino logs**
5. **Explicitly locked benchmark values**
6. **Approved V1.0 documentation**
7. **Historical documentation**

When sources disagree, investigate the discrepancy.

A missing value remains **unknown** rather than being estimated.

---

# 10. Release Freeze Checklist

## Baseline
- [x] V1.0 baseline identified
- [x] V1.x maintenance path established
- [x] V2.x independent development path established
- [x] BikeVision relationship established
- [x] Official V1 GIF benchmark values locked

## GIF V1 Freeze
- [ ] 240×320 orientation verified
- [ ] 320×240 handling verified
- [ ] Frame bounds verified
- [ ] Transparency verified
- [ ] Disposal behavior verified
- [ ] Frame delay verified
- [ ] Repeated playback verified
- [ ] Memory stability verified
- [ ] Four-GIF regression completed

## Documentation / Build
- [ ] Exact hardware configuration finalized
- [ ] Exact software environment finalized
- [ ] Function-level inventory finalized
- [ ] Known limitations finalized
- [ ] Final validation results recorded
- [ ] Build system verified
- [ ] Dependencies verified
- [ ] Configuration verified
- [ ] Flash procedure verified
- [ ] Reproducible build verified

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
CURRENT
   │
   ▼
Finish V1 validation
   │
   ▼
🔒 Freeze TFT V1.0
   │
   ├──────────────► BikeVision
   │                  ├── ESP32-CAM
   │                  ├── Camera → TFT
   │                  ├── 320×240 image
   │                  └── Dashboard
   │
   └──────────────► TFT V2.x
                      ├── GIF optimization
                      ├── Partial-frame transfer
                      ├── JPEG/JPG
                      ├── BMP improvements
                      ├── Memory optimization
                      ├── FreeRTOS
                      └── Double buffering
```

---

# 13. Revision History

| Revision | Version | Description | Status |
|---|---|---|---|
| 1.0 | V1.0 | Initial baseline documentation | Baseline |
| 1.1 | V1.0 | Updated project state, locked GIF benchmark, V1 freeze checklist, V2 roadmap, and BikeVision relationship | **Current** |

---

## Document Control

**Project:** TFT Custom Display  
**Baseline:** V1.0  
**Current State:** **Baseline defined — final freeze verification pending**  
**V1 GIF Baseline:** Locked  
**BikeVision Foundation:** TFT V1.0 after freeze  
**Next TFT Development Line:** V2.x  
**Documentation State:** Updated project baseline
