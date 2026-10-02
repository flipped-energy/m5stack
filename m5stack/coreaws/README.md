# M5Stack Core2 for AWS firmware (`coreaws`)

ESP-IDF v5.5.5 project on esp-matter `release/v1.5`, built in the `flipped-iot-ci` toolchain image ([ci/Dockerfile](../../ci/Dockerfile)). Customer setup: [Flipped Energy developer guides](https://github.com/flipped-energy/developer).

## Commands

| Script | What it does |
|---|---|
| `./build.sh` | Builds the toolchain image, then in it applies [the two esp-matter patches](../patches/esp-matter/README.md), builds the default configuration into `build/`, writes the merged flash image `build/flipped_coreaws-merged.bin` (`idf.py merge-bin`), builds the `CONFIG_FLIPPED_ENERGY_ENDPOINTS=n` variant (`sdkconfig.defaults` plus `sdkconfig.noenergy`) into `build-noenergy/` and the `CONFIG_FLIPPED_EVE_HISTORY=y` variant (`sdkconfig.defaults` plus `sdkconfig.eve`) into `build-eve/`, runs `./test.sh` and prints `ccache --show-stats`. The log is `build-log.txt`; its first lines are the image digests (the `espressif/esp-matter:release-v1.5` digest the build resolved, read from `docker build --metadata-file`, and the built `flipped-iot-ci`), the esp-matter HEAD and the ESP-IDF version; each step's seconds follow it. Only `build/` is merged and flashed; the two variants are compile and link checks. |
| `./test.sh` | In the toolchain image: `spec/verify-vectors.ts`, then the host tests of `m5stack/common/` (`../common/test.sh coreaws`): `common/test/check-common.ts` (no M5 header, board name, `MALLOC_CAP_SPIRAM` or literal look-back setting under `common/`; no ESP-IDF, FreeRTOS or Matter header in `flipped_core`); every vector of `test/vectors/` through the firmware's own parsers, once 7 bytes at a time and once whole, then `computeSignals` and the comparison rules of [00] 5.2; `loop_tests` (the ten sequences of `test/sequences/` through the price loop, account sync, usage sync and start-up against a fake clock and a scripted HTTP fake), `ledger_tests`, `identity_tests`, `projection_tests`, `config_tests`, `switch_plan_tests`, `tariff_tables_tests`, `metering_tests` and `provision_tests`; `eve_history_tests` (every fixture of `test/eve-history/` through the Eve history encoder: entries, over-range intervals, History Status bytes, every History Entries read in replay order, the counters, and a restart through the NVS blob, byte for byte); then `node --test` on the token page. |
| `M5_PORT=<port> ./flash.sh` | Writes `build/` to the board with `esptool` at 1,500,000 baud. Without `M5_PORT` it lists the candidate ports and exits 1. |
| `M5_PORT=<port> ./monitor.sh` | Serial console at 115,200 baud (`pyserial-miniterm`). Opening the port resets the board, so the boot log starts from the top. Leave with Ctrl+]. |
| `./decode.sh <backtrace>` | `xtensa-esp32-elf-addr2line` against `build/flipped_coreaws.elf`, in the toolchain image. Takes the panic line as printed (`./decode.sh Backtrace: 0x400d1234:0x3ffb1230 0x40081a2b:0x3ffb1250`) or bare addresses; it decodes the program counter of each pair. |

## Flashing with `esptool`

Once: `uv tool install esptool`. Docker Desktop on macOS cannot pass a USB serial device into a container, so flashing and the monitor run on the host.

The Core2 for AWS ships with one of two USB bridges: a CP2104 (`/dev/cu.usbserial-*`) or a CH9102 (`/dev/cu.wchusbserial*`). `./flash.sh` with `M5_PORT` unset lists what is plugged in.

```bash
./build.sh
M5_PORT=/dev/cu.usbserial-XXXXXXXX ./flash.sh
M5_PORT=/dev/cu.usbserial-XXXXXXXX ./monitor.sh
```

`flash.sh` runs `esptool --chip esp32 --port "$M5_PORT" --baud 1500000 write-flash "@flash_args"` in `build/` (bootloader at 0x1000, partition table at 0xC000, OTA data at 0x28000, application at 0x30000). The same content as one file, for a machine without the build directory:

```bash
esptool --chip esp32 --port "$M5_PORT" --baud 1500000 write-flash 0x0 build/flipped_coreaws-merged.bin
```

Writes work at 1,500,000 baud; the serial console reads at 115,200. Flashing does not erase NVS: the fabrics, Wi-Fi, the token, the settings, the ledger and the setup code stay. `esptool --chip esp32 --port "$M5_PORT" erase-flash` before flashing removes all of it, and the next boot generates a new setup code.

**Unpair before flashing a build with the other `CONFIG_FLIPPED_ENERGY_ENDPOINTS` setting.** With `n`, EP 2 and EP 3 do not exist and the switch endpoints start at 2; on a commissioned device the fixed IDs collide with the stored switch IDs, `endpoint::resume` fails and the firmware aborts with the SDK's error line ([03] section 6).

## First run

| Order | What happens | Screen |
|---|---|---|
| 1 | Power on with no fabric: the commissioning window opens on BLE for 900 s. The QR payload (`MT:…`) and the 11-digit manual code are on the screen and in the serial log | Boot, then Pairing |
| 2 | Scan the QR in the home app. Apple Home asks about an "Uncertified Accessory" (the SDK's test attestation; vendor ID 0xFFF1): choose Add Anyway. BLE pairing delivers the Wi-Fi credentials | Pairing |
| 3 | Fabric added; Wi-Fi gets an address; SNTP synchronises. The home app shows a bridge with no accessories yet | Status |
| 4 | No token yet | Token setup: scan its QR with the phone camera; in the Flipped app or website open `APIs and MCPs`, create a `Read only` token, paste it into the page and tap `Send to device` |
| 5 | The start-up sequence reads the account; the account is pinned (or chosen on the Account screen when there are several); the five switch endpoints `Peak Rate`, `Off-Peak Rate`, `Shoulder Rate`, `Wholesale Price High` and `Wholesale Price Low` are created and appear in the home app | Status, then Dashboard or the Account chooser |

Wi-Fi cannot come before step 2 (no SoftAP), and the token page needs Wi-Fi, so the order is fixed. On the bench, without a commissioner, the serial console does steps 2 and 4: `matter esp wifi connect <ssid> <password>`, then `matter esp flipped token <value>`.

Removing the bridge in the home app restarts the device; it shows the pairing QR again and keeps Wi-Fi, the token and the ledger. A device whose stored Wi-Fi no longer exists (new router, changed password) can only be recovered by Unpair on the device and adding it again; automations on the old accessories are lost.

## Start-up

`main/app_main.cpp`, in this order ([03] 12.3):

1. `flipped::app::checkTimeZones()`: converts eight fixed instants per POSIX zone of [00] Appendix B (the April and October transitions of 2026 and 2039, one second before and at each) with newlib and compares the local time and offset with the IANA rules; every mismatch is logged and the firmware aborts. On success it logs `tz_check: 40 conversions match the IANA rules (time_t <n> bytes)`.
2. `nvs_flash_init()`.
3. The setup code: loaded from NVS `flipped_mtr`, or generated with the bootloader's random source on the first boot and after Erase everything.
4. The `flipped_app` state (configuration store, ledger, engine); the Matter node with its fixed endpoints and boot checks, the custom commissionable data and device instance info providers, `esp_matter::start()`, the `Grid Energy` outlet (created on first boot, resumed under `flipped_mtr` / `grid_ep` after that) and the EP 2 / EP 3 cluster servers on the Matter thread.
5. The switch endpoints of the stored instance: resumed, replaced, or none before the first account read. A device that ran the four-switch firmware keeps its four switch endpoints and gains `Shoulder Rate` ([03] 6.2).
6. The `matter esp flipped` console commands and `esp_matter::console::init()`.
7. The `flipped_http` and `flipped_app` tasks; `app_main` returns.

The device task (`ui`: board start-up, input, drawing, LEDs) starts between steps 3 and 4, and `app_main` waits for its board-ready notification ([03] 12.3 step 3); that call comes with the board layer (PLAN.md M505, M508).

The `flipped_app` task logs the minimum free internal and external heap and the stack left in its tasks after start-up, after commissioning and after each usage sync (`resources …` lines).

## Console commands

Typed in `./monitor.sh`.

| Command | What it does |
|---|---|
| `matter esp flipped status` | Fabrics, Wi-Fi, clock, token preview and expiry, account, and each group's status and fault (code, HTTP status, byte counts, the kept text verbatim) |
| `matter esp flipped token <value>` | Stores the token; prints the result |
| `matter esp flipped account` | The eligible accounts (number and site address) |
| `matter esp flipped account <number>` | Pins the account; prints the result |
| `matter esp flipped nmi` | The candidate NMIs |
| `matter esp flipped nmi <nmi>` | Pins the NMI; prints the result. A new NMI is a new instance: the five switch endpoints are replaced |
| `matter esp flipped thresholds <high\|off> <low\|off>` | Wholesale price thresholds in c/kWh; prints the result |
| `matter esp flipped refresh` | Requests a usage sync |
| `matter esp flipped pair` | Opens the commissioning window; prints the QR payload and the manual code |
| `matter esp flipped unpair` | Removes every fabric and the Wi-Fi settings and restarts; the token, the settings, the ledger and the setup code stay |
| `matter esp flipped erase` | Erase everything: the same as Unpair plus the token, the settings, the ledger and the setup code |
| `matter esp wifi connect <ssid> <password>` | Joins Wi-Fi without a commissioner (bench only) |

## Reading the device with a Matter controller

The controller runs on the LAN, not in a container: mDNS and IPv6 link-local do not cross the Docker Desktop VM. PLAN.md T11 picks the matter.js shell (`@matter/nodejs-shell`) on the laptop; its commissioning, attribute-read and command syntax is taken from its own `help` when T11 is run, and has not been run yet, so no controller command is recorded here.

## Device settings

Open **More > Screen and lights**. Adjust screen brightness and LED brightness (0–100%), night dimming, beep alerts, virtual devices and spot prices. Scroll to see all settings. Settings survive restart. Touching the display restores its selected brightness for two minutes, including at night.

Spot prices are shown automatically for the selected spot-linked account until you override the setting. Disabling them hides the price tab and price signals. Beep alerts are off by default; when enabled they sound on entering peak or off-peak, or high/low spot conditions while spot prices are enabled.

## Eve history (`CONFIG_FLIPPED_EVE_HISTORY`, enabled in the default build)

The default Core2 build now includes the native energy-history endpoint and Eve history cluster `0x130AFC01`, including the historical average W and cumulative kWh attributes. The endpoint is created after restored virtual switches so an upgrade does not reuse their persisted IDs. The no-energy build omits Eve history.

History retains 4,032 ten-minute records in NVS. Values come from delayed meter data, not live electrical measurements. History Status, History Entries and History Request are covered by byte-level host tests, and firmware compilation is checked. **Eve app discovery, chart display, reboot persistence and HomeKit upgrade behaviour still require an on-device check.** Do not treat compilation as proof of Eve compatibility. Apple Home itself does not render Eve history graphs.

The Matter product and node names are **Flipped Energy**. Existing controller-assigned names may remain until changed in that controller.

## Limits

| Asked for | What this firmware does |
|---|---|
| A Matter 1.5 "power provider" in HomeKit | Apple Home has no meter, tariff or price accessory type. EP 2 (Electrical Utility Meter) and EP 3 (Electrical Meter with Commodity Price, Commodity Tariff, Commodity Metering, Electrical Energy Measurement) are visible to a Matter 1.5 controller and to Home Assistant, and invisible in Apple Home, which shows the bridge, the five switch outlets and the `Grid Energy` outlet. `Grid Energy` ([03] 6.4) carries EP 3's imported energy in Electrical Energy Measurement on an always-off outlet, for the iOS 27 Energy tab; whether Apple Home lists it and counts it is unverified (probe P6) |
| Energy history through the Matter 1.5 APIs | No Matter version has a history query. Commodity Metering (provisional in 1.5, whole kWh, one value) carries the newest complete local day per tariff period; Electrical Energy Measurement carries a cumulative total from the persisted ledger and one periodic measurement per sync. Flipped's data is a day or more late and is whole-of-home |
| Eve history on the device | Whether the Eve app shows Eve's history cluster for a Matter device without Eve's identity is not established; probe P5 decides it (PLAN.md OQ9). The encoder is built and host-tested; the Matter carrier is built behind `CONFIG_FLIPPED_EVE_HISTORY`, enabled in the default Core2 build, with on-device verification pending |
| Read-only switches that can be "unknown" | A Matter On/Off attribute is a writable, non-nullable boolean. Commands on the five switch outlets and on `Grid Energy` are refused, and "unknown" is `Reachable = false` on the bridged endpoint with `OnOff` untouched. How Apple Home renders both is unverified |
| The reference PlatformIO / Arduino project | The Arduino Matter library has no energy endpoints and PlatformIO has no supported esp-matter integration: this is ESP-IDF with esp-matter in Docker, and `pio` cannot build it |
| Daylight saving | Fixed POSIX rules ([00] Appendix B). A legislated change needs a USB reflash of every device; the boot self-check catches a toolchain that converts them wrongly, not a rule change |
| Uncertified Accessory prompt | The device uses the SDK's test attestation and the test vendor ID 0xFFF1; removing the prompt needs CSA membership, a vendor ID, certification and production certificates |
| Wi-Fi changed after pairing | No second Wi-Fi path: Unpair on the device and add it again |

## Measured

Laptop: Apple M3 Max, Docker Desktop VM with 14 CPUs and 7.75 GiB, the image emulated as x86_64, builds limited to 8 CPUs (`--cpuset-cpus`). 2026-10-01.

| Item | Value |
|---|---|
| Toolchain image | `flipped-iot-ci`, 23.8 GB on disk (`docker images`), 6,814,515,124 bytes content (`docker image inspect`) |
| Base image | `espressif/esp-matter:release-v1.5@sha256:8c831095c34285aca9f58808f1917a45ad81a388e34d6dd202ab92236e931806` |
| esp-matter | `ae9001236dddd3f5fd953bed1c4f483c1b9beba3` with [the two patches](../patches/esp-matter/README.md) |
| ESP-IDF | v5.5.5 |
| Default configuration (`build/`), first build, empty compiler cache | 476 s |
| Variant (`build-noenergy/`), first build, after the default | 402 s |
| Both configurations, rebuild after a change to `flipped_core` | 74 s and 65 s |
| Both configurations, rebuild after the full start-up of `app_main` (`flipped_app`, `main`, the second esp-matter patch) | 37 s and 77 s |
| `./test.sh` inside `./build.sh`, host test build up to date | 6 s |
| Whole `./build.sh`, first run (image check, both configurations) | 887 s |
| Whole `./build.sh` after the start-up change (image check, both configurations, merge, tests) | 130 s |
| Application image, default (`build/flipped_coreaws.bin`) | 2,017,088 bytes (0x1EC740) of the 4,194,304-byte slot, 52 % free |
| Application image, variant (`build-noenergy/flipped_coreaws.bin`) | 1,931,552 bytes (0x1D7920) of the 4,194,304-byte slot, 54 % free |
| Application image, Eve variant (`build-eve/flipped_coreaws.bin`) | 2,031,280 bytes (0x1EFEB0) of the 4,194,304-byte slot, 52 % free |
| Static DRAM, Eve variant | 106,916 bytes used, 17,664 left (the 32,256-byte entry ring and the 24,219-byte blob buffer are heap allocations above the 4,096-byte `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` threshold, so `malloc` places them in PSRAM; not observed on the device) |
| Eve variant (`build-eve/`), rebuild with the compiler cache filled by the other two | 26 s |
| Merged image (`build/flipped_coreaws-merged.bin`, offset 0x0 to the end of the application) | 2,213,664 bytes |
| Static DRAM, default / variant (`esp_idf_size` on the map file) | 106,900 bytes used, 17,680 left / 101,956 bytes used, 22,624 left |
| Static IRAM, both configurations | 105,867 bytes used, 25,205 left |
| `build/`, `build-noenergy/`, `managed_components/` on disk (first build) | 834 MB, 834 MB, 217 MB |
| Heap minima and stack high-water marks on the device ([03] 11.2) | not measured yet: owed with the first boot log (PLAN.md V14) |
