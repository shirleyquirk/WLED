# IRAM is full on ESP32-S3 builds

Status: open, not currently blocking. Recorded while working on DMX support.

## Observation

Every ESP32-S3 build measured reports IRAM completely exhausted, zero bytes remaining:

```
│ IRAM        │  16384 │ 100.0 │      0 │ 16384 │
│    .text    │  15356 │ 93.73 │        │       │
│    .vectors │   1028 │  6.27 │        │       │
```

Identical on `esp32s3dev_8MB_qspi` and `esp32s3_dmx`, so it is not caused by enabling
DMX. Other regions have room to spare — DIRAM 27%, flash 63% — so this is specific to
IRAM rather than a general size problem.

## Why it matters

Code in IRAM keeps executing while the instruction cache is disabled, which happens
whenever flash is written: saving presets, writing `cfg.json`, OTA. Everything else
stalls for the duration.

For DMX that is not academic. `esp_dmx` can place its ISR in IRAM precisely so that
incoming frames survive a cache-disabling flash write; the documented alternative is
to disable the driver around such writes, which is what `dmx_input.cpp` already does
via `DMXInput::disable()` / `enable()`. With no IRAM free, the better option is not
available to us — and the same limit will apply to any other latency-sensitive driver
added later.

The current DMX work targets the non-IRAM path deliberately, so nothing is blocked
today. This is about an option being closed off, not a present-day failure.

## Worth investigating

- What occupies the 15356 bytes of `.text`? Start from the linker map at
  `.pio/build/<env>/firmware.map` and attribute it per object and per library.
- How much is placed there by our own build flags versus by the framework's default
  placements. Candidates: `WLED_USE_SHARED_RMT`, the NeoPixelBus RMT paths, and any
  usermod code carrying `IRAM_ATTR`.
- Whether any of it is IRAM-resident by accident rather than by need.
- Whether `CONFIG_*_ISR_IN_IRAM`-style options are reachable through PlatformIO's
  precompiled Arduino framework at all, or only via an ESP-IDF component build.
- Whether classic ESP32 builds show the same figure. If they do not, something
  target-specific is responsible.

## Reproducing

```
pio run -e esp32s3dev_8MB_qspi
```

Read the memory table in the output; the map file is written alongside the binary.
