#ifndef WLED_DMX_H
#define WLED_DMX_H

#include <cstddef>
#include <cstdint>

/*
 * The DMX interface.
 *
 * A DMX link is one half-duplex pair with a single transmitter, so a port either sends
 * or receives - never both. RDM replies from a receiver are the driver's own bus
 * turnaround, not a second direction. One driver with a direction therefore covers
 * everything; the previous split into separate input and output drivers only meant two
 * pieces of code competing for the same UART, which they both defaulted to.
 */
enum class DmxDirection : uint8_t {
  Off    = 0,
  Output = 1,  ///< drive fixtures from the LED buffer, or proxy an E1.31 universe
  Input  = 2,  ///< be a fixture: receive DMX, respond to RDM
};

#if defined(WLED_ENABLE_DMX_OUTPUT) || defined(WLED_ENABLE_DMX_INPUT)

#ifdef ESP8266
  // esp_dmx is espressif32-only (library.json), so the 8266 keeps the old bit-banged
  // serial backend. Output only - it cannot receive.
  #include "src/dependencies/dmx/ESPDMX.h"
#else
  #include <esp_dmx.h>
  #include <atomic>
  #include <mutex>
#endif

/// Slots in a DMX frame: 512 channels plus slot 0, which carries the start code.
static constexpr size_t DMX_FRAME_SIZE = 513;

class DmxDriver {
  public:
    /// Claims pins and starts the driver in the configured direction. Safe to call
    /// again; it tears down any previous incarnation first.
    void init();
    /// Per-loop work: sends a frame when transmitting, applies one when receiving.
    void loop();

    /// The esp_dmx driver is not in IRAM, so it must be stopped around writes that
    /// disable the flash cache. No-ops when not receiving.
    void enable();
    void disable();

    bool isRunning() const     { return running; }
    DmxDirection direction() const { return dir; }
    int port() const           { return uartPort; }
    /// Receiving: a source is currently sending to us. Transmitting: always false.
    bool isConnected() const;

    /// Raw channel access for the E1.31 proxy, which forwards a whole universe
    /// untouched. Channels are 1-based; writes outside the universe are dropped.
    void writeChannels(unsigned firstChannel, const uint8_t *values, size_t count);
    /// Queues the current frame for transmission. Rate-limited internally.
    void sendFrame();

  private:
    void renderFixtures();     ///< LED buffer -> frame, per the DMXStart/Gap/Channels map
    void applyReceived();      ///< received frame -> segments, via handleDMXData()
    bool claimPins();
    void releasePins();

    DmxDirection dir = DmxDirection::Off;
    int uartPort = -1;
    int8_t txPin = -1, rxPin = -1, enPin = -1;
    bool running = false;
    unsigned long lastSend = 0;

#ifndef ESP8266
    bool startDriver();
    void receiveLoop();        ///< body of the receive task; never returns
    bool isIdentifyOn() const;
    void syncRdmConfig();      ///< push web-UI changes into the RDM responder
    void showIdentify();

    friend void dmxReceiveTask(void *context);
    friend void rdmAddressChangedCb(dmx_port_t, rdm_header_t *, rdm_header_t *, void *);
    friend void rdmPersonalityChangedCb(dmx_port_t, rdm_header_t *, rdm_header_t *, void *);

    uint8_t frame[DMX_FRAME_SIZE] = {0};   ///< slot 0 is the start code
    std::mutex frameLock;                  ///< guards frame between the task and loop()
    std::atomic<bool> connected{false};
    std::atomic<bool> identify{false};
    TaskHandle_t task = nullptr;
#else
    DMXESPSerial serial;
#endif
};

extern DmxDriver Dmx;

#endif // WLED_ENABLE_DMX_OUTPUT || WLED_ENABLE_DMX_INPUT
#endif // WLED_DMX_H
