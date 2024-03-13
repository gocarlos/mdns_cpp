#pragma once

#include <cstdint>
#include <string>

namespace mdns_cpp {

class ServiceRecord {
 public:
  const char *service;
  const char *hostname;
  uint32_t address_ipv4;
  uint8_t *address_ipv6;
  uint16_t port;
};


struct QueryResult {
  std::string key;
  std::string host;
  std::string ipv4;
  std::string ipv6;
};

}  // namespace mdns_cpp
