# DMX in this fork

One driver, `DmxDriver` in `wled00/dmx.{h,cpp}`, owning one UART port with a direction
set at runtime (`Off` / `Output` / `Input`). It replaced two separate drivers that
shared nothing but a UART they both defaulted to.

Backends: `esp_dmx` on every ESP32 variant the library supports; `ESPDMX` on ESP8266,
which `esp_dmx` cannot serve (`library.json` says `platforms: espressif32`).

## Platform support — what is tested and what is not

| Part | Status |
| --- | --- |
| ESP32-S3 | Builds; **hardware-tested only as far as noted below** |
| ESP32 (classic) | Builds. **Untested on hardware** |
| ESP32-S2, ESP32-C3 | Builds. **Untested on hardware, and newly plausible** — see below |
| ESP8266 | Builds via `ESPDMX`. **Untested, and unlikely to be tested here** |
| ESP32-C5 / C6 / P4 | `#error` — `esp_dmx` does not know their UART registers yet |

S2 and C3 are the interesting entry. They were previously forced onto the ESP8266
backend, but not because of any `esp_dmx` limitation: `SparkFunDMX` hard-required
`HardwareSerial(2)` and `#error`ed on parts with only two UARTs. `esp_dmx` adapts to
two-UART parts (`DMX_NUM_2` is behind `#if SOC_UART_NUM > 2`), so those parts should
now get the real driver on port 1. Nobody has confirmed that against silicon. Treat the
table as "compiles and should work" rather than "works".

## Sending and receiving at the same time

Standard DMX512 is one transmitter per link and a fixture is either a controller or a
responder, RDM turnaround aside. That is a protocol convention, not a hardware limit,
and for testing or bespoke work it can be ignored — with these caveats.

**The UART does not care.** ESP32 UARTs are full duplex: independent TX and RX paths
and FIFOs. Transmitting a break while receiving is fine, because the break is a
condition on our TX line and has nothing to do with the RX path.

**A single `esp_dmx` port does care.** The library is explicitly half duplex:
`dmx_send_num()` and `dmx_receive_num()` both take the same per-driver recursive mutex
(`driver->mux`, `io.c:307` and `io.c:128`), and the driver flips the RTS/DE line between
directions (`io.c:88`, `io.c:155`, `io.c:390`, plus the ISR at `hal/uart.c:318`). Two
directions on one port therefore serialise, by design. This is not something to work
around; it is what makes RDM turnaround correct.

**Two ports do not care either.** `driver->mux` is created per instance in
`dmx_driver_install()`, so two driver instances on two UARTs share no lock, no
peripheral and no state. Simultaneous send and receive is then simply two independent
drivers, and needs no changes to `esp_dmx`.

**The wiring decides the rest.** On a conventional single twisted pair with one
transceiver, transmitting while receiving is bus contention and will corrupt both
directions — the DE/!RE line exists precisely to prevent it. But nothing forces a single
pair. Two transceivers on two pairs is full duplex with no contention, and 5-pin DMX
cable already carries a second, normally unused pair for exactly this sort of thing. A
bench rig with no transceiver at all — driving a receiver input directly and reading a
driver output directly — is already electrically full duplex, because TX and RX are
separate wires.

### What that means for this code

Simultaneous TX and RX is a **two-instance** feature, not a two-direction-per-port
feature. It is not implemented, and nothing here needs it yet, but the design does not
stand in its way:

- `DmxDriver` holds all its per-port state — port, pins, frame buffer, lock, connection
  flags, task handle — as members, and passes `this` as the RDM callback context. Two
  instances would not tread on each other.
- Two things would need attention first. The RDM personality table
  (`dmxPersonalities` in `dmx.cpp`) is file-static and shared, which is only a problem
  for two *receiving* instances; and the configuration globals (`dmxPort`, `dmxTxPin`,
  and friends) describe exactly one port, so they would need to become per-instance.

The cheapest use for it is self-testing: one board transmitting on one port and
receiving on another, wired together, verifying its own framing without a fixture or a
dongle. See `docs/dmx-test-rig.md`.
