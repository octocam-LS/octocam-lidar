# Octocam Lidar

ESP-NOW camera trigger sender with a TF-Luna lidar distance sensor, on an ESP32 18650 board
(a "T-Energy S3" clone that is really a classic ESP32), 0.91" SSD1306 OLED and battery indicator.
Successor of the ESP8266 Octocam ToF. Used like the Octocam LS senders: it sends to paired
Octocam triggers (each trigger sets its own shutter duration); no camera cord of its own.

- `OctocamLidar_ESP32/` – Arduino sketch (board "ESP32 Dev Module", Flash Mode DIO, 40 MHz,
  Partition Scheme "Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)", library U8g2).
- `firmware/OctocamLidar_ESP32.bin` + `firmware/version.txt` – what the device downloads when
  "Update Firmware" is tapped in its web app. `version.txt` must equal `FIRMWARE_VERSION`
  in the sketch for each release.

The first install (or a change of partition scheme) has to be done over USB; after that the
device updates itself over the home WiFi saved in its web app. The WiFi password is stored only
on the device, never in this repository.

Wiring: see the comment at the top of `OctocamLidar_ESP32/OctocamLidar_ESP32.ino`.
