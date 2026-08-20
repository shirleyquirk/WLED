#include "wled.h"

#ifdef WLED_DEBUG_HOST

#ifdef ARDUINO_ARCH_ESP32
  #define NET_DEBUG_LOCK() const std::lock_guard<std::mutex> lock(udpLock)
#else
  #define NET_DEBUG_LOCK() do {} while (0)  // single threaded, nothing to guard against
#endif

bool NetworkDebugPrinter::resolveHost() {
  if (debugPrintHostIP) return true;
  if (debugPrintHostIP.fromString(netDebugPrintHost)) return true;

  #ifdef ESP8266
    WiFi.hostByName(netDebugPrintHost, debugPrintHostIP, 750);
  #else
    #ifdef WLED_USE_ETHERNET
      ETH.hostByName(netDebugPrintHost, debugPrintHostIP);
    #else
      WiFi.hostByName(netDebugPrintHost, debugPrintHostIP);
    #endif
  #endif

  return (bool)debugPrintHostIP;
}

void NetworkDebugPrinter::invalidateHost() {
  NET_DEBUG_LOCK();
  debugPrintHostIP = IPAddress();
}

size_t NetworkDebugPrinter::write(uint8_t c) {
  if (!WLED_CONNECTED || !netDebugEnabled) return 0;

  NET_DEBUG_LOCK();
  if (!resolveHost()) return 0;

  debugUdp.beginPacket(debugPrintHostIP, netDebugPrintPort);
  debugUdp.write(c);
  debugUdp.endPacket();
  return 1;
}

size_t NetworkDebugPrinter::write(const uint8_t *buf, size_t size) {
  if (!WLED_CONNECTED || buf == nullptr || !netDebugEnabled) return 0;

  NET_DEBUG_LOCK();
  if (!resolveHost()) return 0;

  debugUdp.beginPacket(debugPrintHostIP, netDebugPrintPort);
  size = debugUdp.write(buf, size);
  debugUdp.endPacket();
  return size;
}

NetworkDebugPrinter NetDebug;

#endif
