# Launcher Port X4 — Release Guide

This guide covers installing the Launcher build on your Xteink X4. Choose the path that matches your setup.

## Option A: Flash directly (download from EinkHub)
1) Visit EinkHub and download the latest Launcher release asset for X4 (e.g., `.bin` or packed firmware file).
2) Put your X4 in flashing mode and connect via USB.
3) Use your preferred flashing tool (e.g., esptool/ESP flasher GUI) to flash the downloaded file to the device’s main firmware slot.
   - Common command (example):
     ```bash
     esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 write_flash 0x0 launcher-x4.bin
     ```
   - Replace the filename and port with the ones that match your environment.
4) Power-cycle or reset the device. The Launcher should boot and present the main menu.

## Option B: Flash a packed image (if provided)
If the release includes a packed image with partition offsets, follow the offsets listed in the release notes. Example:
```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x1000 bootloader.bin 0x8000 partitions.bin 0xE000 boot_app0.bin 0x10000 launcher-x4.bin
```
Use the exact filenames/offsets from the release.

## Verify after flashing
- Device should boot to Launcher home.
- Check Wi-Fi/network setup inside Launcher (if applicable).
- If storage features exist, verify SD/flash mounting screens.

## Recovery
If the device fails to boot:
- Re-flash the prior known-good image.
- Ensure correct file, baud rate, and offsets.
- Use lower baud (115200) if you see flashing errors.
