/*
 * Host-side tests for the DMX output channel arithmetic.
 *
 * Build and run with:  npm run test:cpp
 *
 * The header under test deliberately depends on nothing else, so these run on the
 * build machine in milliseconds -- no ESP32, no emulator.
 */
#include "../../wled00/dmx_addressing.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void check(bool ok, const char *what, unsigned got, unsigned want) {
  if (ok) return;
  std::printf("FAIL: %s -> got %u, want %u\n", what, got, want);
  failures++;
}

#define CHECK_EQ(expr, want) check((expr) == (want), #expr, (unsigned)(expr), (unsigned)(want))

int main() {
  // Addresses are 1-based and step by the gap, not by the fixture size.
  CHECK_EQ(dmxChannelAddress(10, 10, 0, 0), 10u);
  CHECK_EQ(dmxChannelAddress(10, 10, 0, 6), 16u);
  CHECK_EQ(dmxChannelAddress(10, 10, 1, 0), 20u);
  CHECK_EQ(dmxChannelAddress(1, 4, 3, 2), 15u);

  // WLED's defaults: start 10, gap 10, 7 channels. Fixture 49 ends at channel 506 and
  // fits; fixture 50 would end at 516 and does not.
  CHECK_EQ(dmxFixturesInUniverse(10, 10, 7), 50u);
  CHECK_EQ(dmxChannelAddress(10, 10, 49, 6), 506u);
  CHECK_EQ(dmxChannelAddress(10, 10, 50, 6), 516u);

  // A tightly packed universe: 128 four-channel fixtures from channel 1.
  CHECK_EQ(dmxFixturesInUniverse(1, 4, 4), 128u);
  CHECK_EQ(dmxChannelAddress(1, 4, 127, 3), 512u);

  // Exactly one fixture filling the universe, and one channel too many.
  CHECK_EQ(dmxFixturesInUniverse(1, 1, 512), 1u);
  CHECK_EQ(dmxFixturesInUniverse(2, 1, 512), 0u);
  CHECK_EQ(dmxFixturesInUniverse(513, 1, 1), 0u);

  // Degenerate inputs must not report room that does not exist.
  CHECK_EQ(dmxFixturesInUniverse(0, 10, 7), 0u);   // channel 0 is the start code
  CHECK_EQ(dmxFixturesInUniverse(10, 10, 0), 0u);  // a fixture with no channels
  CHECK_EQ(dmxFixturesInUniverse(10, 0, 7), 1u);   // no spacing: all on one address

  // Every fixture the bound admits must stay inside the universe. This is the
  // property whose absence let the output loop write past the end of the buffer.
  for (unsigned start = 1; start <= 64; start++) {
    for (unsigned gap = 0; gap <= 40; gap++) {
      for (unsigned channels = 1; channels <= 15; channels++) {
        const unsigned n = dmxFixturesInUniverse(start, gap, channels);
        if (n == 0) continue;
        const unsigned last = dmxChannelAddress(start, gap, n - 1, channels - 1);
        if (last > DMX_UNIVERSE_CHANNELS) {
          std::printf("FAIL: start=%u gap=%u channels=%u admits %u fixtures, last channel %u\n",
                      start, gap, channels, n, last);
          failures++;
        }
      }
    }
  }

  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("dmx_addressing: all checks passed\n");
  return EXIT_SUCCESS;
}
