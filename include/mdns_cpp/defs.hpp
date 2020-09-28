#pragma once

#include <cstdint>

namespace mdns_cpp {

class ServiceRecord {
 public:
  const char *service;
  const char *hostname;
  uint32_t address_ipv4;
  uint8_t *address_ipv6;
  uint16_t port;
};

}  // namespace mdns_cpp
