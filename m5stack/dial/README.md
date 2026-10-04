# M5Stack Dial

The ESP32-S3 M5Stack Dial is an officially supported Flipped Energy display and Matter device.

## Build and install

Install Docker Desktop and run `./m5stack/dial/build.sh` from the repository root. The build uses the shared `flipped-iot-ci` image; build it first with `docker build -t flipped-iot-ci ci`. Build output is in `m5stack/dial/build/`.

Use the ESP-IDF flashing command printed by the successful build, or esptool on the host with the generated `flash_args` from that directory. Select your Dial's USB serial port. Flashing normally preserves settings and pairing; erasing flash removes them.

## Pair and configure

1. Scan the on-screen Matter QR code in your home controller. The development firmware uses test attestation, so Apple Home presents an uncertified-accessory prompt.
2. Let pairing supply Wi-Fi, then follow the device's token-setup QR.
3. Create a read-only token in [APIs and MCPs](https://flipped.energy/accounts/developer?tab=tokens) and enter it on the device setup page.
4. Select the account and meter when prompted.

Turn the dial to move between tariff, spot price, usage, signals, pairing and settings screens. Press on **Virtual devices**, **Spot prices** or **Beep alerts** to toggle the setting. Spot prices are initially automatic for spot-linked accounts; beep alerts are initially off. Settings persist across restarts.

Usage is delayed meter data. The live [API reference](https://flipped.energy/accounts/developer?tab=reference) and [tools](https://flipped.energy/accounts/developer?tab=tester) remain on the Flipped website.
