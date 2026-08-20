#include "wled.h"

/*
 * Support for DMX  output via serial (e.g. MAX485).
 * Change the output pin in src/dependencies/ESPDMX.cpp, if needed (ESP8266)
 * Change the output pin in src/dependencies/SparkFunDMX.cpp, if needed (ESP32)
 * ESP8266 Library from:
 * https://github.com/Rickgg/ESP-Dmx
 * ESP32 Library from:
 * https://github.com/sparkfun/SparkFunDMX
 */

#ifdef WLED_ENABLE_DMX_OUTPUT

void handleDMXOutput()
{
  // don't act, when in DMX Proxy mode
  if (e131ProxyUniverse != 0) return;

  uint8_t brightness = strip.getBrightness();

  bool calc_brightness = true;

   // check if no shutter channel is set
   for (unsigned i = 0; i < DMXChannels; i++)
   {
     if (DMXFixtureMap[i] == 5) calc_brightness = false;
   }

  // Uses the amount of LEDs as fixture count, but only as many as this addressing can
  // reach: anything past the end of the universe has nowhere to go.
  const unsigned maxFixtures = dmxFixturesInUniverse(DMXStart, DMXGap, DMXChannels);
  const unsigned len = strip.getLengthTotal();
  const unsigned available = (len > DMXStartLED) ? len - DMXStartLED : 0;
  const unsigned fixtures = (available > maxFixtures) ? maxFixtures : available;
  for (unsigned i = DMXStartLED; i < DMXStartLED + fixtures; i++) {

    uint32_t in = strip.getPixelColor(i);     // get the colors for the individual fixtures as suggested by Aircoookie in issue #462
    byte w = W(in);
    byte r = R(in);
    byte g = G(in);
    byte b = B(in);

    for (unsigned j = 0; j < DMXChannels; j++) {
      const unsigned DMXAddr = dmxChannelAddress(DMXStart, DMXGap, i - DMXStartLED, j);
      switch (DMXFixtureMap[j]) {
        case 0:        // Set this channel to 0. Good way to tell strobe- and fade-functions to fuck right off.
          dmx.write(DMXAddr, 0);
          break;
        case 1:        // Red
          dmx.write(DMXAddr, calc_brightness ? (r * brightness) / 255 : r);
          break;
        case 2:        // Green
          dmx.write(DMXAddr, calc_brightness ? (g * brightness) / 255 : g);
          break;
        case 3:        // Blue
          dmx.write(DMXAddr, calc_brightness ? (b * brightness) / 255 : b);
          break;
        case 4:        // White
          dmx.write(DMXAddr, calc_brightness ? (w * brightness) / 255 : w);
          break;
        case 5:        // Shutter channel. Controls the brightness.
          dmx.write(DMXAddr, brightness);
          break;
        case 6:        // Sets this channel to 255. Like 0, but more wholesome.
          dmx.write(DMXAddr, 255);
          break;
      }
    }
  }

  dmx.update();        // update the DMX bus
}

void initDMXOutput() {
 #if defined(ESP8266) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C5) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32C61) || defined(CONFIG_IDF_TARGET_ESP32S2)
  dmx.init(512);        // initialize with bus length
 #else
  dmx.initWrite(512);  // initialize with bus length
 #endif
}
#else
void initDMXOutput(){}
void handleDMXOutput() {}
#endif
