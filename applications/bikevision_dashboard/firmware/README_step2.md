# Step 2 — ESP-NOW camera -> display (frames received and checked, not drawn yet)

Copy into `applications/bikevision_dashboard/firmware/`:

```
firmware/
├── components/espnow_link/          NEW (shared by both nodes)
├── camera_node/main/
│   ├── espnow_cam.c, espnow_cam.h   NEW
│   ├── main.c                       2 lines added (include + espnow_cam_start())
│   └── CMakeLists.txt               + "espnow_cam.c", + espnow_link
└── display_node/main/
    ├── display_link_test.c          NEW, temporary app_main for this step
    └── CMakeLists.txt               compiles only display_link_test.c for now
```

Both top-level `CMakeLists.txt` files (camera_node/ and display_node/) need ONE
line, placed BEFORE `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`:

```cmake
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
```

TFT V1.0 files are not changed. Keep the original display main/CMakeLists as
`CMakeLists.txt.v1`; it comes back in step 3 (drawing).
