# ESP-IDF TFT complete port

Files:
- `main/tft.h`
- `main/tft.cpp`
- `main/font5x7.h`
- `main/tft_display_module.cpp`
- `main/CMakeLists.txt`
- `idf_component.yml`
- `partitions.csv`
- `sdkconfig.defaults`

JPEG decoder dependency:
`espressif/esp_jpeg`.

Build:
```bash
idf.py reconfigure
idf.py build
```

For SPIFFS image testing, place BMP/JPEG/GIF files under the project's `spiffs/` directory and use:
```bash
idf.py build
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor
```

The test app currently expects:
- `/spiffs/test.bmp`
- `/spiffs/Delaware 320x240.jpg`
- `/spiffs/birds.gif`

The 5x7 font table is retained as the supplied `font5x7.h`.
