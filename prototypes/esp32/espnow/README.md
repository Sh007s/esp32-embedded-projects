# ESP-NOW link — reusable prototype

A small, general-purpose ESP-NOW link between **two ESP32 boards**, plus a test
that measures packet loss. Not tied to any one project: copy the `components/`
folder into anything that needs a direct board-to-board link.

ESP-IDF v5.1.x, target `esp32`.

## Folder layout

```
espnow/
├── components/          reusable
│   ├── wifi_sta/        join a Wi-Fi network; retries forever, power save off
│   ├── espnow_link/     ESP-NOW + auto-pairing + send-with-ACK
│   └── link_test/       packet formats for the test only
├── sender_node/         test sender   (pairing INITIATOR)
└── receiver_node/       test receiver (pairing RESPONDER)
```

## Using it in another project

Top-level `CMakeLists.txt` of your project:

```cmake
set(EXTRA_COMPONENT_DIRS "<path>/espnow/components")
```

`main/CMakeLists.txt`:

```cmake
PRIV_REQUIRES espnow_link wifi_sta
```

Minimal code:

```c
wifi_sta_start("ssid", "password");
wifi_sta_wait_connected(portMAX_DELAY);

espnow_link_start(ESPNOW_LINK_ROLE_INITIATOR, my_rx_callback); // other board: RESPONDER
espnow_link_wait_paired(portMAX_DELAY);

espnow_link_send(ESPNOW_LINK_TYPE_APP_FIRST, data, len, pdMS_TO_TICKS(100));
```

- Payload: up to **242 bytes** per packet (250 ESP-NOW limit minus an 8-byte header).
- Your packet types start at `ESPNOW_LINK_TYPE_APP_FIRST` (0x10).
- `espnow_link_send()` returns `ESP_OK` only if the peer ACKed the packet.
- The receive callback runs in the link's own task, not the Wi-Fi task.

## How pairing works

1. Both boards join the same Wi-Fi network, so they are on the same channel.
2. The initiator broadcasts `HELLO` every 500 ms.
3. The responder answers `HELLO_ACK`. Both add each other as peers and save the MAC in NVS.
4. After 20 unACKed sends in a row (for example the responder board was replaced), the initiator starts broadcasting `HELLO` again.

Limit: one pair of boards per network. Two pairs on the same network would pair with whichever answers first.

## Running the test

**Receiver** (first terminal):

```bash
cd espnow/receiver_node
idf.py set-target esp32
idf.py menuconfig      # ESP-NOW link test -> Wi-Fi SSID / password
idf.py -p /dev/ttyUSB0 build flash monitor
```

**Sender** (second terminal):

```bash
cd espnow/sender_node
idf.py set-target esp32
idf.py menuconfig      # same SSID / password
idf.py -p /dev/ttyUSB1 build flash monitor
```

Every 10 s the sender sends 1000 packets of 242 bytes; the receiver reports:

```
I receiver_node: Run 1 (END received): received 1000/1000, lost 0 (0.00%), ... -> PASS
I sender_node:   Run 1 receiver report: received 1000/1000, lost 0 (0.00%) ... -> PASS
```

Settings in menuconfig: packets per run (10–2000), seconds between runs.

## Pass criteria

| Test | Pass |
| --- | --- |
| Pairing | Pairs automatically within a few seconds |
| Loss | Under 1% lost, 0 corrupted, across at least 5 runs |
| Reflash receiver | Re-pairs automatically, runs continue |
| Reflash sender | Re-pairs automatically |
| Wi-Fi off ~30 s, then on | Both reconnect; runs resume |

## Notes

- `sdkconfig` contains your Wi-Fi password. Don't commit it.
- Compiled with the ESP32 toolchain (GCC 12.2) against ESP-IDF v5.1.4 headers.
