# Flipped Energy M5Stack firmware

Display your Flipped Energy tariff, spot prices and delayed meter usage, and use tariff and price signals in Matter automations.

Officially supported hardware:

- [M5Stack Core2 for AWS](coreaws/README.md): touch display, adjustable status LEDs and optional audible alerts.
- [M5Stack Dial](dial/README.md): round display, rotary navigation and optional audible alerts.

Create a read-only device token in [APIs and MCPs](https://flipped.energy/accounts/developer?tab=tokens). Use the website's [API reference](https://flipped.energy/accounts/developer?tab=reference), [request tester](https://flipped.energy/accounts/developer?tab=tester) and [MCP tools](https://flipped.energy/accounts/developer?tab=mcp) for live documentation. The [developer guide](https://github.com/flipped-energy/developer) explains the integration choices.

Build with Docker Desktop using the script for your board. Each board's guide covers flashing and pairing. Firmware uses the ESP-IDF/esp-matter toolchain; PlatformIO is not supported.

Both boards let you disable virtual devices and override spot-price visibility. Spot prices default to the selected account's spot-plan status. Optional beeps announce entry into peak/off-peak periods and high/low spot conditions. The devices report as **Flipped Energy** during Matter setup.

Meter data arrives after consumption, usually a day or more later. Displayed power is a historical interval average. The Core2 build includes Eve history support; real-device Eve discovery and chart display remain under validation.
