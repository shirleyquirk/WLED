#include "wled.h"

#if defined(WLED_ENABLE_DMX_OUTPUT) || defined(WLED_ENABLE_DMX_INPUT)

#include "dmx.h"

DmxDriver Dmx;

/// DMX512 allows about 44 full frames per second. Transmitting from the main loop as
/// fast as it happens to spin achieves nothing and starves everything else, so pace it.
static constexpr unsigned long DMX_SEND_INTERVAL_MS = 25;

// ---------------------------------------------------------------------------------
// Pins
// ---------------------------------------------------------------------------------

bool DmxDriver::claimPins()
{
  // Not used as GPIO by us - the UART owns them - so isOutput is false throughout.
  managed_pin_type pins[3];
  unsigned n = 0;
  if (txPin >= 0) pins[n++] = {txPin, false};
  if (rxPin >= 0) pins[n++] = {rxPin, false};
  if (enPin >= 0) pins[n++] = {enPin, false};
  if (n == 0) return false;

  if (!PinManager::allocateMultiplePins(pins, n, PinOwner::DMX)) {
    DEBUG_PRINTF_P(PSTR("DMX: pins already in use (tx=%d rx=%d en=%d)\n"), txPin, rxPin, enPin);
    return false;
  }
  return true;
}

void DmxDriver::releasePins()
{
  if (txPin >= 0) PinManager::deallocatePin(txPin, PinOwner::DMX);
  if (rxPin >= 0) PinManager::deallocatePin(rxPin, PinOwner::DMX);
  if (enPin >= 0) PinManager::deallocatePin(enPin, PinOwner::DMX);
  txPin = rxPin = enPin = -1;
}

// ---------------------------------------------------------------------------------
// Frame contents
// ---------------------------------------------------------------------------------

void DmxDriver::writeChannels(unsigned firstChannel, const uint8_t *values, size_t count)
{
  if (values == nullptr || firstChannel < 1) return;
#ifndef ESP8266
  const std::lock_guard<std::mutex> guard(frameLock);
  for (size_t i = 0; i < count && firstChannel + i <= DMX_UNIVERSE_CHANNELS; i++) {
    frame[firstChannel + i] = values[i];
  }
#else
  for (size_t i = 0; i < count && firstChannel + i <= DMX_UNIVERSE_CHANNELS; i++) {
    serial.write(firstChannel + i, values[i]);
  }
#endif
}

#ifdef WLED_ENABLE_DMX_OUTPUT
void DmxDriver::renderFixtures()
{
  const uint8_t brightness = strip.getBrightness();

  // A shutter channel carries the brightness itself, so the colours must not be scaled
  // by it as well.
  bool scaleByBrightness = true;
  for (unsigned i = 0; i < DMXChannels; i++) {
    if (DMXFixtureMap[i] == 5) scaleByBrightness = false;
  }

  // One fixture per LED, but only as many as this addressing can reach: the rest would
  // need channels past the end of the universe.
  const unsigned maxFixtures = dmxFixturesInUniverse(DMXStart, DMXGap, DMXChannels);
  const unsigned len = strip.getLengthTotal();
  const unsigned available = (len > DMXStartLED) ? len - DMXStartLED : 0;
  const unsigned fixtures = (available > maxFixtures) ? maxFixtures : available;

  for (unsigned f = 0; f < fixtures; f++) {
    const uint32_t c = strip.getPixelColor(DMXStartLED + f);
    const uint8_t w = W(c), r = R(c), g = G(c), b = B(c);

    for (unsigned j = 0; j < DMXChannels; j++) {
      const unsigned addr = dmxChannelAddress(DMXStart, DMXGap, f, j);
      uint8_t value;
      switch (DMXFixtureMap[j]) {
        case 1:  value = scaleByBrightness ? (r * brightness) / 255 : r; break;  // red
        case 2:  value = scaleByBrightness ? (g * brightness) / 255 : g; break;  // green
        case 3:  value = scaleByBrightness ? (b * brightness) / 255 : b; break;  // blue
        case 4:  value = scaleByBrightness ? (w * brightness) / 255 : w; break;  // white
        case 5:  value = brightness; break;                                      // shutter
        case 6:  value = 255; break;
        case 0:                                                                  // hold at 0
        default: value = 0; break;
      }
      writeChannels(addr, &value, 1);
    }
  }
}
#endif

void DmxDriver::sendFrame()
{
  if (!running || dir != DmxDirection::Output) return;

  const unsigned long now = millis();
  if (now - lastSend < DMX_SEND_INTERVAL_MS) return;
  lastSend = now;

#ifndef ESP8266
  if (!dmx_wait_sent(uartPort, 0)) return;  // still transmitting the previous frame
  {
    const std::lock_guard<std::mutex> guard(frameLock);
    frame[0] = 0;  // start code for a standard dimmer frame
    dmx_write(uartPort, frame, DMX_FRAME_SIZE);
  }
  dmx_send(uartPort);
#else
  serial.update();
#endif
}

// ---------------------------------------------------------------------------------
// ESP8266: output only, on the old bit-banged backend
// ---------------------------------------------------------------------------------
#ifdef ESP8266

void DmxDriver::init()
{
  dir = DmxDirection::Off;
  running = false;
  #ifdef WLED_ENABLE_DMX_OUTPUT
  serial.init(DMX_UNIVERSE_CHANNELS);
  dir = DmxDirection::Output;
  running = true;
  DEBUG_PRINTLN(F("DMX: output started (ESP8266 serial backend)"));
  #endif
}

void DmxDriver::loop()
{
  #ifdef WLED_ENABLE_DMX_OUTPUT
  if (running && e131ProxyUniverse == 0) renderFixtures();
  sendFrame();
  #endif
}

void DmxDriver::enable() {}
void DmxDriver::disable() {}
bool DmxDriver::isConnected() const { return false; }

#else
// ---------------------------------------------------------------------------------
// ESP32 family: esp_dmx, both directions
// ---------------------------------------------------------------------------------

#include <rdm/responder.h>

/// RDM personalities, indexed from 1 to match DMXMode.
static dmx_personality_t dmxPersonalities[10];

static void buildPersonalities()
{
  const int leds = strip.getLengthTotal();
  const int segs = strip.getSegmentsNum();
  struct { const char *name; int footprint; } defs[] = {
    {"SINGLE_RGB",       3},
    {"SINGLE_DRGB",      4},
    {"EFFECT",          15},
    {"MULTIPLE_RGB",    std::min(512, leds * 3)},
    {"MULTIPLE_DRGB",   std::min(512, leds * 3 + 1)},
    {"MULTIPLE_RGBW",   std::min(512, leds * 4)},
    {"EFFECT_W",        18},
    {"EFFECT_SEGMENT",  std::min(512, segs * 15)},
    {"EFFECT_SEGMENT_W",std::min(512, segs * 18)},
    {"PRESET",           1},
  };
  for (unsigned i = 0; i < 10; i++) {
    strncpy(dmxPersonalities[i].description, defs[i].name, 32);
    dmxPersonalities[i].footprint = defs[i].footprint;
  }
}

void rdmPersonalityChangedCb(dmx_port_t, rdm_header_t *, rdm_header_t *response_header, void *context)
{
  DmxDriver *d = static_cast<DmxDriver *>(context);
  if (!d || response_header->cc != RDM_CC_SET_COMMAND_RESPONSE) return;
  const uint8_t personality = dmx_get_current_personality(d->uartPort);
  DMXMode = std::min(DMX_MODE_PRESET, std::max(DMX_MODE_SINGLE_RGB, int(personality)));
  configNeedsWrite = true;
  DEBUG_PRINTF_P(PSTR("DMX: RDM set personality %d\n"), DMXMode);
}

void rdmAddressChangedCb(dmx_port_t, rdm_header_t *, rdm_header_t *response_header, void *context)
{
  DmxDriver *d = static_cast<DmxDriver *>(context);
  if (!d || response_header->cc != RDM_CC_SET_COMMAND_RESPONSE) return;
  DMXAddress = std::min(512, int(dmx_get_start_address(d->uartPort)));
  configNeedsWrite = true;
  DEBUG_PRINTF_P(PSTR("DMX: RDM set start address %d\n"), DMXAddress);
}

void dmxReceiveTask(void *context)
{
  DmxDriver *d = static_cast<DmxDriver *>(context);
  if (d) d->receiveLoop();
  vTaskDelete(nullptr);
}

bool DmxDriver::startDriver()
{
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  config.model_id = 0;
  config.product_category = RDM_PRODUCT_CATEGORY_FIXTURE;
  config.software_version_id = uint32_t(VERSION);
  static const std::string versionLabel = "WLED_V" + std::to_string(VERSION);
#if ESP_IDF_VERSION_MAJOR < 5
  strncpy(config.software_version_label, versionLabel.c_str(), 32);
  config.software_version_label[32] = '\0';
#else
  config.software_version_label = versionLabel.c_str();
#endif

  // Only a receiver is an addressable fixture; a transmitter has no DMX address and
  // needs no personalities.
  const bool asFixture = (dir == DmxDirection::Input);
  if (asFixture) buildPersonalities();

  if (!dmx_driver_install(uartPort, &config, asFixture ? dmxPersonalities : nullptr,
                          asFixture ? 10 : 0)) {
    DEBUG_PRINTF_P(PSTR("DMX: failed to install driver on port %d\n"), uartPort);
    return false;
  }
  dmx_set_pin(uartPort, txPin, rxPin, enPin);

  if (asFixture) {
    dmx_set_start_address(uartPort, DMXAddress);
    dmx_set_current_personality(uartPort, DMXMode);
    rdm_register_dmx_start_address(uartPort, rdmAddressChangedCb, this);
    rdm_register_dmx_personality(uartPort, 10, rdmPersonalityChangedCb, this);
  }
  return true;
}

void DmxDriver::init()
{
  if (running) {
    if (task) { vTaskDelete(task); task = nullptr; }
    dmx_driver_delete(uartPort);
    releasePins();
    running = false;
  }

  dir = DmxDirection(dmxDirection);
  if (dir == DmxDirection::Off) return;

#ifndef WLED_ENABLE_DMX_OUTPUT
  if (dir == DmxDirection::Output) { DEBUG_PRINTLN(F("DMX: output not in this build")); return; }
#endif
#ifndef WLED_ENABLE_DMX_INPUT
  if (dir == DmxDirection::Input)  { DEBUG_PRINTLN(F("DMX: input not in this build"));  return; }
#endif

  if (dmxPort < 1 || dmxPort >= SOC_UART_NUM) {
    DEBUG_PRINTF_P(PSTR("DMX: invalid port %d\n"), dmxPort);  // port 0 is the console
    return;
  }
  uartPort = dmxPort;

  // Transmitting needs TX. Receiving needs RX, and TX too if it is to answer RDM.
  // The enable pin is optional: it drives a transceiver's direction, and there is not
  // always a transceiver to drive.
  txPin = (int8_t)dmxTxPin;
  rxPin = (int8_t)dmxRxPin;
  enPin = (int8_t)dmxEnPin;
  if (dir == DmxDirection::Output && txPin < 0) { DEBUG_PRINTLN(F("DMX: no TX pin set")); return; }
  if (dir == DmxDirection::Input  && rxPin < 0) { DEBUG_PRINTLN(F("DMX: no RX pin set")); return; }

  if (!claimPins()) return;
  if (!startDriver()) { releasePins(); return; }
  running = true;

  if (dir == DmxDirection::Input) {
    // Off the main core: receiving must not be held up by rendering.
    xTaskCreatePinnedToCore(dmxReceiveTask, "DMX_RCV", 10240, this, 2, &task, 0);
    if (!task) DEBUG_PRINTLN(F("DMX: failed to create receive task"));
  }
  DEBUG_PRINTF_P(PSTR("DMX: %s on port %d (tx=%d rx=%d en=%d)\n"),
                 dir == DmxDirection::Output ? "sending" : "receiving",
                 uartPort, txPin, rxPin, enPin);
}

void DmxDriver::receiveLoop()
{
  while (true) {
    syncRdmConfig();

    dmx_packet_t packet;
    if (dmx_receive(uartPort, &packet, DMX_TIMEOUT_TICK)) {
      if (packet.err) { connected = false; continue; }
      if (!connected) DEBUG_PRINTLN(F("DMX: source connected"));
      connected = true;
      identify = isIdentifyOn();
      if (!packet.is_rdm) {
        const std::lock_guard<std::mutex> guard(frameLock);
        dmx_read(uartPort, frame, packet.size);
      }
    } else {
      if (connected) DEBUG_PRINTLN(F("DMX: source disconnected"));
      connected = false;
    }
  }
}

void DmxDriver::syncRdmConfig()
{
  // The web UI writes the globals directly; mirror any change into the responder.
  if (dmx_get_current_personality(uartPort) != DMXMode) {
    dmx_set_current_personality(uartPort, DMXMode);
  }
  if (dmx_get_start_address(uartPort) != DMXAddress) {
    dmx_set_start_address(uartPort, DMXAddress);
  }
}

bool DmxDriver::isIdentifyOn() const
{
  bool on = false;
  const bool got = rdm_get_identify_device(uartPort, &on);
  return on && got;
}

void DmxDriver::showIdentify()
{
  const unsigned n = strip.getLengthTotal();
  for (unsigned i = 0; i < n; i++) strip.setPixelColor(i, 255, 255, 255, 255);
  strip.setBrightness(255, true);
  strip.show();
}

void DmxDriver::applyReceived()
{
  if (identify) { showIdentify(); return; }
  if (!connected) return;
  const std::lock_guard<std::mutex> guard(frameLock);
  handleDMXData(1, 512, frame, REALTIME_MODE_DMX, 0);
}

void DmxDriver::loop()
{
  if (!running) return;
  switch (dir) {
    case DmxDirection::Output:
      #ifdef WLED_ENABLE_DMX_OUTPUT
      // In proxy mode the frame is filled by the E1.31 handler instead.
      if (e131ProxyUniverse == 0) renderFixtures();
      sendFrame();
      #endif
      break;
    case DmxDirection::Input:
      #ifdef WLED_ENABLE_DMX_INPUT
      applyReceived();
      #endif
      break;
    default: break;
  }
}

void DmxDriver::enable()  { if (running && dir == DmxDirection::Input) dmx_driver_enable(uartPort); }
void DmxDriver::disable() { if (running && dir == DmxDirection::Input) dmx_driver_disable(uartPort); }
bool DmxDriver::isConnected() const { return connected; }

#endif // ESP8266
#endif // WLED_ENABLE_DMX_OUTPUT || WLED_ENABLE_DMX_INPUT
