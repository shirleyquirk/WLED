# Container bring-up and QEMU notes

Tooling for building WLED inside a Claude Code remote container, plus what was learned
trying to run the result under emulation. None of it is needed to build WLED normally —
it exists because this particular environment has two constraints that a developer
machine does not.

## The two constraints

**Outbound TLS is re-terminated by an agent proxy.** Tools that carry their own CA
bundle and ignore `REQUESTS_CA_BUNDLE` have to have the proxy CA installed into them.
PlatformIO is one. The espressif32 platform keeps a *second* Python environment at
`~/.platformio/penv` with its own bundle, and that one is the trap: it does not exist
until the first platform install, so fixing only the outer bundle appears to work and
then fails partway through the first real build.

**The PlatformIO package registry is blocked.** `*.registry.platformio.org` returns 403
at the proxy; GitHub and PyPI are reachable. Anything PlatformIO fetches by registry
name therefore has to come from somewhere else — including `platformio/tool-scons`,
without which no build runs at all, and including framework builtins like `SPI`, which
PlatformIO also looks up by name.

## Usage

```sh
tools/dev/setup-build-env.sh      # once per container
npm ci && npm run build           # generates wled00/html_*.h, required before pio
tools/dev/pio-build.sh esp32s3_dmx
```

Use `pio-build.sh` rather than `pio run`. Each wave of newly installed libraries
re-declares dependencies that only the blocked registry can resolve, so building and
stripping alternate until it settles — usually two or three passes.

| File | What it does |
| --- | --- |
| `setup-build-env.sh` | Installs PlatformIO, fixes both CA bundles, builds `tool-scons` from the PyPI sdist, installs the override, builds the `lib_dir` mirror |
| `pio-build.sh` | `pio run` with the dependency fixup applied between attempts |
| `strip-registry-deps.py` | Removes declared dependencies from installed `library.json` files |
| `platformio_override.offline.ini` | Registry-named libraries repinned to equivalent GitHub tags |
| `qemu-boot.sh` | Merges a flash image and boots it under Espressif QEMU |

### Two sharp edges

`strip-registry-deps.py` silently un-declares *real* dependencies along with the
unresolvable ones. That is how `audioreactive` lost `arduinoFFT` and stopped linking,
with no error — the usermod just quietly vanished from the binary. Every library WLED
needs is therefore pinned explicitly in the override. If a usermod goes missing from a
build, suspect this first and check the `INFO: Code from usermod libraries found in
binary:` line at the end of the build.

`lib/NeoESP32RmtHI` declares `makuna/NeoPixelBus` by registry name. The V5 esp32 envs
put it in `lib_ignore`, but PlatformIO reads declared dependencies *before* applying
`lib_ignore`, so it has to be absent from `lib_dir` entirely — hence the symlink mirror
rather than a config setting.

## QEMU

`qemu-boot.sh` fetches Espressif's QEMU fork, merges bootloader + partitions + app into
an 8 MB flash image, and boots it. **It does not currently reach the application.**

Boot stops here:

```
ESP-ROM:esp32s3-20210327
rst:0x1 (POWERON),boot:0x4 (SPI_FLASH_BOOT)
load:0x403cb700,len:0x28e4
entry 0x403c8874
        <- nothing further
```

The ROM loader works and hands off to the second-stage bootloader, which then stalls
silently. The `-d unimp,guest_errors` trace says why:

```
M25P80: Unknown cmd 5a      <- SFDP read
M25P80: Unknown cmd 77
M25P80: Unknown cmd 7a
M25P80: Read id (command 0x90/0xAB) is not supported by device
```

The bootloader probes the flash chip with SFDP and QIO-mode commands that QEMU's
`m25p80` model does not implement. PSRAM is not involved — tried with `ssi_psram` at
2 MB, identical result.

Fixing it means controlling the **bootloader's** flash configuration, which is exactly
what is out of reach when building against arduino-esp32's precompiled IDF libraries
through PlatformIO. The same wall blocks the other interesting possibility: QEMU's
ESP32-S3 machine emulates Opencores Ethernet with user-mode networking, which would put
the web UI and the JSON endpoints within reach of an emulated build — but the openeth
driver is gated behind `CONFIG_ETH_USE_OPENETH` in sdkconfig. Both roads out run through
an ESP-IDF component build.

### What emulation could and could not be worth

Even fully working, QEMU could not validate DMX output. DMX512 is defined by timing —
a ≥88 µs break, an 8 µs mark-after-break, 250 kbaud 8N2 — and QEMU's UART is a
character device where the baud divisor is decorative. `esp_dmx` also depends on
hardware break *detection* and on hardware timers for RDM turnaround, neither of which
a simplified UART model raises. A frame that looks perfect under emulation would say
nothing about what a fixture sees.

What it would be good for is boot-to-crash regression testing: does the firmware still
initialise with a given set of features enabled. That is worth having for driver work,
where the likely failure is a panic during init rather than a bad waveform.

For DMX correctness the options remain, in order of fidelity: a scope or logic analyser
on the TX line, a second ESP32 running a DMX receiver, or a loopback between two UARTs
on the same board — the only automated check that runs against real silicon with real
timing.
