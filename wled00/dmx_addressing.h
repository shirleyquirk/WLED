#ifndef WLED_DMX_ADDRESSING_H
#define WLED_DMX_ADDRESSING_H

/*
 * Channel arithmetic for DMX output.
 *
 * Deliberately free of hardware access and WLED globals so it can be reasoned about
 * and tested on its own. Fixtures are laid out at a fixed spacing from a start
 * channel, each occupying the same number of channels:
 *
 *   fixture 0 -> start, start+1, ... start+channels-1
 *   fixture 1 -> start+gap, ...
 *
 * Note that the spacing is independent of the fixture size, so fixtures overlap when
 * gap < channels. That is the user's business; what is not is running past the end of
 * the universe, which is what these bounds are for.
 */

/// Usable channels in a DMX512 universe, numbered 1..512.
constexpr unsigned DMX_UNIVERSE_CHANNELS = 512;

/// Address of one channel of one fixture, where channelOffset is 0-based within the
/// fixture and the result is a 1-based DMX channel number.
constexpr unsigned dmxChannelAddress(unsigned start, unsigned gap, unsigned fixture, unsigned channelOffset) {
  return start + gap * fixture + channelOffset;
}

/// How many fixtures fit in one universe with this addressing.
///
/// Addressing beyond this would need channels above 512, which do not exist; writing
/// them used to run off the end of the DMX buffer instead of being dropped.
constexpr unsigned dmxFixturesInUniverse(unsigned start, unsigned gap, unsigned channels) {
  return (start < 1 || channels < 1 || start + channels - 1 > DMX_UNIVERSE_CHANNELS)
           ? 0                                                                  // not even one fits
           : (gap < 1 ? 1                                                       // all stacked on the start channel
                      : (DMX_UNIVERSE_CHANNELS - (start + channels - 1)) / gap + 1);
}

#endif
