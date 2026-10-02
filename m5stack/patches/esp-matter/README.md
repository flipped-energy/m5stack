# esp-matter patches

Three patches against esp-matter `release/v1.5`, both made at HEAD `ae9001236dddd3f5fd953bed1c4f483c1b9beba3`.

## 0001-semantic-tag-count.patch

`0001-semantic-tag-count.patch` changes one line of esp-matter `release/v1.5` (HEAD `ae9001236dddd3f5fd953bed1c4f483c1b9beba3` when it was made):

```
components/esp_matter/data_model/esp_matter_data_model.cpp
-#define ESP_MATTER_MAX_SEMANTIC_TAG_COUNT 3
+#define ESP_MATTER_MAX_SEMANTIC_TAG_COUNT 4
```

### Why

esp-matter stores at most three semantic tags per endpoint, as a plain define with no Kconfig option, and `endpoint::set_semantic_tags` returns `ESP_ERR_INVALID_ARG` ("Tag count should be no more than 3") for more. The Electrical Meter endpoint of this firmware needs four tags, and its `TagList` attribute is mandatory ([docs/design/03-m5stack-matter-firmware.md](../../../docs/design/03-m5stack-matter-firmware.md) sections 4.3 and 6).

## 0002-commodity-metering-init-callback.patch

Removes one line of esp-matter `release/v1.5`:

```
components/esp_matter/data_model_provider/esp_matter_plugin_server_init_callbacks.cpp
-void MatterCommodityMeteringPluginServerInitCallback() {}
```

### Why

esp-matter defines an empty `MatterCommodityMeteringPluginServerInitCallback()` among its plugin stubs, and connectedhomeip's `src/app/clusters/commodity-metering-server/commodity-metering-server.cpp` (line 374) defines the same function. Both objects end up in `libesp_matter.a`. As soon as the firmware uses the SDK's Commodity Metering server (EP 3, [docs/design/03-m5stack-matter-firmware.md](../../../docs/design/03-m5stack-matter-firmware.md) section 8.4), the link stops with:

```
esp_matter_plugin_server_init_callbacks.cpp:72: multiple definition of `_Z47MatterCommodityMeteringPluginServerInitCallbackv'; esp-idf/esp_matter/libesp_matter.a(commodity-metering-server.cpp.obj):/opt/espressif/esp-matter/connectedhomeip/connectedhomeip/src/app/clusters/commodity-metering-server/commodity-metering-server.cpp:374: first defined here
collect2: error: ld returned 1 exit status
```

Upstream has already removed the stub: the file on `release/v1.6` and `main` no longer defines it (read 2026-10-01), so no issue is needed, and the patch is deleted when the project moves to `release/v1.6`.

## 0003-commodity-price-empty-forecast.patch

Changes one line of the connectedhomeip copy inside esp-matter `release/v1.5`:

```
connectedhomeip/connectedhomeip/src/app/clusters/commodity-price-server/commodity-price-server.cpp
-    if (!mOwnedForecastPriceStructBuffer.Calloc(entries))
+    if (entries > 0 && !mOwnedForecastPriceStructBuffer.Calloc(entries))
```

### Why

`CommodityPrice::Instance::SetForecast` deep-copies the list through `Calloc(entries)`. ESP-IDF returns null for a zero-byte allocation, so an empty forecast fails with `CHIP_ERROR_NO_MEMORY` (0x0B). The firmware sets an empty forecast at start-up and whenever the price group has no forecast, and aborts on that error: an M5Stack Dial running this firmware restarted in a loop with `CommodityPrice SetForecast: Error CHIP:0x0000000B`. With the patch an empty list leaves `PriceForecast` empty and returns `CHIP_NO_ERROR`.

## How they are applied

`m5stack/coreaws/build.sh` and `m5stack/dial/build.sh` run `m5stack/patches/apply.sh` inside the toolchain container, before `idf.py build`. For each `esp-matter/*.patch` in name order it applies the patch with `git -C "$ESP_MATTER_PATH" apply` and gives the patched file the patch's timestamp, so the compiler cache still hits. A patch that is already applied (its reverse applies cleanly) is reported as `already applied` and skipped: CI runs both board builds in one container, so the second build finds the patches in place. A patch that neither applies nor is already applied stops the build with git's own error; then that patch is remade, or deleted once upstream no longer needs it.

## Upstream issue for 0001 (to be filed by a maintainer of this repository; not filed yet)

Title: Make ESP_MATTER_MAX_SEMANTIC_TAG_COUNT configurable (Kconfig)

Body:

> `components/esp_matter/data_model/esp_matter_data_model.cpp` defines `#define ESP_MATTER_MAX_SEMANTIC_TAG_COUNT 3` and sizes `semantic_tags[ESP_MATTER_MAX_SEMANTIC_TAG_COUNT]` in every endpoint with it. `endpoint::set_semantic_tags` rejects more than three tags with `ESP_ERR_INVALID_ARG` and the log line "Tag count should be no more than 3".
>
> The Matter 1.5 Device Library topology of our energy endpoint needs four semantic tags in its mandatory `TagList`, so the endpoint cannot be described with esp-matter as it is.
>
> The same define is present on `release/v1.5`, `release/v1.6`, `release/v1.6.1` and `main`. Today the only way to publish a fourth tag is to patch the source.
>
> Request: a Kconfig option, for example `CONFIG_ESP_MATTER_MAX_SEMANTIC_TAG_COUNT` (default 3), used for this define, in the same way as `CONFIG_ESP_MATTER_MAX_DEVICE_TYPE_COUNT` is used for `ESP_MATTER_MAX_DEVICE_TYPE_COUNT` two lines above it.
