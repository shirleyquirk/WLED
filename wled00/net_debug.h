#ifndef WLED_NET_DEBUG_H
#define WLED_NET_DEBUG_H

#include <WString.h>
#include <WiFiUdp.h>
#include <atomic>
#ifdef ARDUINO_ARCH_ESP32
#include <mutex>
#endif

class NetworkDebugPrinter : public Print {
  private:
    /// The socket and the lock that guards it. Debug output is written both from the
    /// main loop and from driver tasks on the other core, and a datagram is built over
    /// several calls, so the sequence below must not interleave.
    struct {
      WiFiUDP udp; // needs to be here otherwise UDP messages get truncated upon destruction
      #ifdef ARDUINO_ARCH_ESP32
      std::mutex lock;
      #endif
    } socket;
    /// Address of netDebugPrintHost, 0 while unresolved. Resolving it needs a working
    /// connection and may block on DNS, so it happens when the network comes up rather
    /// than on every print.
    std::atomic<uint32_t> targetIp{0};
  public:
    /// (Re)resolves netDebugPrintHost. Call once connected, and whenever the host changes.
    void resolveTarget();
    virtual size_t write(uint8_t c);
    virtual size_t write(const uint8_t *buf, size_t s);
};

// use it on your linux/macOS with: nc -p 7868 -u -l -s <network ip>
extern NetworkDebugPrinter NetDebug;

#endif
