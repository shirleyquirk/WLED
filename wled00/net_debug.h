#ifndef WLED_NET_DEBUG_H
#define WLED_NET_DEBUG_H

#include <WString.h>
#include <WiFiUdp.h>
#include <atomic>

class NetworkDebugPrinter : public Print {
  private:
    WiFiUDP debugUdp; // needs to be here otherwise UDP messages get truncated upon destruction
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
