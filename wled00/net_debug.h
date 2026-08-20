#ifndef WLED_NET_DEBUG_H
#define WLED_NET_DEBUG_H

#include <WString.h>
#include <WiFiUdp.h>
#ifdef ARDUINO_ARCH_ESP32
#include <mutex>
#endif

class NetworkDebugPrinter : public Print {
  private:
    WiFiUDP debugUdp; // needs to be here otherwise UDP messages get truncated upon destruction
    IPAddress debugPrintHostIP;
#ifdef ARDUINO_ARCH_ESP32
    // Drivers such as DMX log from their own task while the main loop logs from another
    // core. Without this lock the two interleave inside a single UDP datagram.
    std::mutex udpLock;
#endif
    /// Resolves netDebugPrintHost into debugPrintHostIP (cached). @return false if unresolved.
    bool resolveHost();
  public:
    virtual size_t write(uint8_t c);
    virtual size_t write(const uint8_t *buf, size_t s);
    /// Drops the cached IP; call after netDebugPrintHost has been changed at runtime.
    void invalidateHost();
};

// use it on your linux/macOS with: nc -p 7868 -u -l -s <network ip>
extern NetworkDebugPrinter NetDebug;

#endif
