# Plan: hardware test rig for DMX

Status: proposed. Roughly a weekend to build; would give continuous integration and a
real dev loop for DMX work, which is currently the missing piece — emulation cannot
validate DMX because the protocol is defined by timing and QEMU's UART is a character
device (see `tools/dev/README.md`).

## What it buys

- Confirms the platform table in `docs/dmx.md`, most of which currently says "should
  work". S2 and C3 in particular are newly plausible and entirely unverified.
- Verifies output *timing*, not just channel values: break ≥88 µs, mark-after-break
  ≥8 µs, 250 kbaud, 8N2. Nothing in the current toolchain can check any of that.
- Catches regressions in a driver that has one user and no tests.
- Removes the human from the loop: today every change needs someone to flash a board
  and look at a light.

## Hardware

**Host.** A Linux machine or SBC that stays on: runs the CI agent, holds USB to every
target, drives the dongle. A powered hub with per-port switching is worth it — wedged
ESP32s are recovered by power-cycling the port rather than by walking over to it.

**DMX dongle.** Needs to both send and receive, which rules out the transmit-only
bitbang dongles. An Enttec DMX USB Pro or equivalent is the usual choice; RDM support
is a bonus, since the input path is an RDM responder and nothing exercises that today.

**Targets.** One board per row of the platform table: ESP32, S3, S2, C3, and an 8266 if
8266 support is to be kept honest. Each with its own RS-485 transceiver — 3.3 V parts
(MAX3485 and friends), and isolated modules if the rig ever shares space with mains.

**Capture.** A multi-channel logic analyser is what makes timing assertions possible.
`sigrok` has a DMX512 protocol decoder, and `sigrok-cli` runs headless, so break and MAB
lengths become assertions rather than eyeballing.

## Topology

The subtlety is that output tests and input tests want opposite things.

**Input tests** are easy: the dongle is the controller, every target listens. One shared
universe, each target on a different start address, all asserted from one frame.

**Output tests conflict on a shared bus** — only one device may transmit at a time, so
targets cannot all drive one universe simultaneously. Three ways out, in increasing
order of cost:

1. Test one target at a time. Simplest, slowest, and fine to start with.
2. An analogue mux or relay board selecting which target's transceiver is on the bus.
3. **Give each target its own return line into a separate logic-analyser channel.**
   No contention at all, and it makes the LA the arbiter of both value and timing.

Option 3 is the one worth building toward: a shared downstream bus from the dongle for
input tests, plus a per-target upstream line into the capture device for output tests.

## Software

A single orchestration script per test run:

1. Flash the target: `pio run -e <env> -t upload --upload-port /dev/ttyUSB<n>`
2. Wait for boot, then configure over the JSON API — direction, pins, port, start
   address — so no test depends on what happened to be in `cfg.json`
3. Drive the dongle (OLA, or talk to the Pro's serial protocol directly)
4. Assert:
   - **input**: send a known frame, read back `/json/state` and `/json/dmx`
   - **output**: set known pixel values, capture with `sigrok-cli`, decode DMX512,
     assert channel contents *and* break/MAB timing
5. Report

Wire it to a self-hosted GitHub Actions runner once the script is reliable standalone.
Everything up to step 5 is useful long before CI is involved.

## Milestones

**0. Loopback, no rig at all.** One S3, TX on one port wired to RX on another, firmware
transmitting and receiving its own frames. Needs the two-instance work described in
`docs/dmx.md`, but costs one jumper wire and validates framing end to end. Do this
first — it may catch enough to change what the rig needs to be.

**1. One target, input only.** Host + dongle + one S3. Prove flash → configure → send →
assert. This is the whole loop in miniature; everything after is scale.

**2. Output capture.** Add the logic analyser and the DMX512 decode. First point at
which timing is actually verified.

**3. Second target, then the matrix.** Generalise the script over a target list. This is
where the platform table stops being guesswork.

**4. CI.** Self-hosted runner, triggered per push to DMX-touching paths.

## Open questions

- Does the dongle's receive path cope with our break timing, or does it need its own
  quirks? Worth finding out at milestone 1 rather than 3.
- Termination: 120 Ω at the far end of the shared bus, and does the star topology in
  option 3 need more care than a daisy chain?
- Is 8266 worth a slot on the bench at all, given it cannot be tested here anyway and
  the backend is unchanged from upstream?
