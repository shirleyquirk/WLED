#include "wled.h"

#ifdef WLED_DEBUG_HOST

void NetworkDebugPrinter::resolveTarget() {
  IPAddress ip;
  if (!ip.fromString(netDebugPrintHost)) { // not a literal address, ask DNS
    #ifdef ESP8266
      WiFi.hostByName(netDebugPrintHost, ip, 750);
    #elif defined(WLED_USE_ETHERNET)
      ETH.hostByName(netDebugPrintHost, ip);
    #else
      WiFi.hostByName(netDebugPrintHost, ip);
    #endif
  }
  targetIp = uint32_t(ip); // stays 0 if neither worked, disabling output
}

size_t NetworkDebugPrinter::write(uint8_t c) {
  return write(&c, 1);
}

size_t NetworkDebugPrinter::write(const uint8_t *buf, size_t size) {
  const uint32_t ip = targetIp;
  if (!WLED_CONNECTED || buf == nullptr || !netDebugEnabled || !ip) return 0;

  #ifdef ARDUINO_ARCH_ESP32
  const std::lock_guard<std::mutex> guard(socket.lock);
  #endif
  socket.udp.beginPacket(IPAddress(ip), netDebugPrintPort);
  size = socket.udp.write(buf, size);
  socket.udp.endPacket();
  return size;
}

NetworkDebugPrinter NetDebug;

#endif
