#include "mdns_cpp/mdns.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <thread>

#include "mdns.h"
#include "mdns_cpp/logger.hpp"
#include "mdns_cpp/macros.hpp"
#include "mdns_cpp/utils.hpp"

#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#endif
#include <string.h>

namespace mdns_cpp {

static mdns_record_txt_t txtbuffer[128];

int mDNS::openServiceSockets(int *sockets, int max_sockets) {
  // When receiving, each socket can receive data from all network interfaces
  // Thus we only need to open one socket for each address family
  int num_sockets = 0;

  // Call the client socket function to enumerate and get local addresses,
  // but not open the actual sockets
  openClientSockets(0, 0, 0);

  if (num_sockets < max_sockets) {
    sockaddr_in sock_addr{};
    sock_addr.sin_family = AF_INET;
#ifdef _WIN32
    sock_addr.sin_addr = in4addr_any;
#else
    sock_addr.sin_addr.s_addr = INADDR_ANY;
#endif
    sock_addr.sin_port = htons(MDNS_PORT);
#ifdef __APPLE__
    sock_addr.sin_len = sizeof(struct sockaddr_in);
#endif
    const int sock = mdns_socket_open_ipv4(&sock_addr);
    if (sock >= 0) {
      sockets[num_sockets++] = sock;
    }
  }

  if (num_sockets < max_sockets) {
    sockaddr_in6 sock_addr{};
    sock_addr.sin6_family = AF_INET6;
    sock_addr.sin6_addr = in6addr_any;
    sock_addr.sin6_port = htons(MDNS_PORT);
#ifdef __APPLE__
    sock_addr.sin6_len = sizeof(struct sockaddr_in6);
#endif
    int sock = mdns_socket_open_ipv6(&sock_addr);
    if (sock >= 0) sockets[num_sockets++] = sock;
  }

  return num_sockets;
}

int mDNS::openClientSockets(int *sockets, int max_sockets, int port) {
  // When sending, each socket can only send to one network interface
  // Thus we need to open one socket for each interface and address family
  int num_sockets = 0;

#ifdef _WIN32

  IP_ADAPTER_ADDRESSES *adapter_address = nullptr;
  ULONG address_size = 8000;
  unsigned int ret{};
  unsigned int num_retries = 4;
  do {
    adapter_address = (IP_ADAPTER_ADDRESSES *)malloc(address_size);
    ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, 0, adapter_address,
                               &address_size);
    if (ret == ERROR_BUFFER_OVERFLOW) {
      free(adapter_address);
      adapter_address = 0;
    } else {
      break;
    }
  } while (num_retries-- > 0);

  if (!adapter_address || (ret != NO_ERROR)) {
    free(adapter_address);
    LogMessage() << "Failed to get network adapter addresses\n";
    return num_sockets;
  }

  int first_ipv4 = 1;
  int first_ipv6 = 1;
  for (PIP_ADAPTER_ADDRESSES adapter = adapter_address; adapter; adapter = adapter->Next) {
    if (adapter->TunnelType == TUNNEL_TYPE_TEREDO) {
      continue;
    }
    if (adapter->OperStatus != IfOperStatusUp) {
      continue;
    }

    for (IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
      if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
        struct sockaddr_in *saddr = (struct sockaddr_in *)unicast->Address.lpSockaddr;
        if ((saddr->sin_addr.S_un.S_un_b.s_b1 != 127) || (saddr->sin_addr.S_un.S_un_b.s_b2 != 0) ||
            (saddr->sin_addr.S_un.S_un_b.s_b3 != 0) || (saddr->sin_addr.S_un.S_un_b.s_b4 != 1)) {
          int log_addr = 0;
          if (first_ipv4) {
            service_address_ipv4_ = saddr->sin_addr.S_un.S_addr;
            first_ipv4 = 0;
            log_addr = 1;
          }
          has_ipv4_ = 1;
          if (num_sockets < max_sockets) {
            saddr->sin_port = htons((unsigned short)port);
            int sock = mdns_socket_open_ipv4(saddr);
            if (sock >= 0) {
              sockets[num_sockets++] = sock;
              log_addr = 1;
            } else {
              log_addr = 0;
            }
          }
          if (log_addr) {
            char buffer[128];
            const auto addr = ipv4AddressToString(buffer, sizeof(buffer), saddr, sizeof(struct sockaddr_in));
            MDNS_LOG << "Local IPv4 address: " << addr << "\n";
          }
        }
      } else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
        struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)unicast->Address.lpSockaddr;
        static constexpr unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        static constexpr unsigned char localhost_mapped[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
        if ((unicast->DadState == NldsPreferred) && memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
            memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
          int log_addr = 0;
          if (first_ipv6) {
            memcpy(service_address_ipv6_, &saddr->sin6_addr, 16);
            first_ipv6 = 0;
            log_addr = 1;
          }
          has_ipv6_ = 1;
          if (num_sockets < max_sockets) {
            saddr->sin6_port = htons((unsigned short)port);
            int sock = mdns_socket_open_ipv6(saddr);
            if (sock >= 0) {
              sockets[num_sockets++] = sock;
              log_addr = 1;
            } else {
              log_addr = 0;
            }
          }
          if (log_addr) {
            char buffer[128];
            const auto addr = ipv6AddressToString(buffer, sizeof(buffer), saddr, sizeof(struct sockaddr_in6));
            MDNS_LOG << "Local IPv6 address: " << addr << "\n";
          }
        }
      }
    }
  }

  free(adapter_address);

#else

  struct ifaddrs *ifaddr = nullptr;
  struct ifaddrs *ifa = nullptr;

  if (getifaddrs(&ifaddr) < 0) {
    MDNS_LOG << "Unable to get interface addresses\n";
  }

  int first_ipv4 = 1;
  int first_ipv6 = 1;
  for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr) {
      continue;
    }

    if (ifa->ifa_addr->sa_family == AF_INET) {
      struct sockaddr_in *saddr = (struct sockaddr_in *)ifa->ifa_addr;
      if (saddr->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
        int log_addr = 0;
        if (first_ipv4) {
          service_address_ipv4_ = saddr->sin_addr.s_addr;
          first_ipv4 = 0;
          log_addr = 1;
        }
        has_ipv4_ = 1;
        if (num_sockets < max_sockets) {
          saddr->sin_port = htons(port);
          int sock = mdns_socket_open_ipv4(saddr);
          if (sock >= 0) {
            sockets[num_sockets++] = sock;
            log_addr = 1;
          } else {
            log_addr = 0;
          }
        }
        if (log_addr) {
          char buffer[128];
          const auto addr = ipv4AddressToString(buffer, sizeof(buffer), saddr, sizeof(struct sockaddr_in));
          MDNS_LOG << "Local IPv4 address: " << addr << "\n";
        }
      }
    } else if (ifa->ifa_addr->sa_family == AF_INET6) {
      struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)ifa->ifa_addr;
      static constexpr unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
      static constexpr unsigned char localhost_mapped[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
      if (memcmp(saddr->sin6_addr.s6_addr, localhost, 16) && memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
        int log_addr = 0;
        if (first_ipv6) {
          memcpy(service_address_ipv6_, &saddr->sin6_addr, 16);
          first_ipv6 = 0;
          log_addr = 1;
        }
        has_ipv6_ = 1;
        if (num_sockets < max_sockets) {
          saddr->sin6_port = htons(port);
          int sock = mdns_socket_open_ipv6(saddr);
          if (sock >= 0) {
            sockets[num_sockets++] = sock;
            log_addr = 1;
          } else {
            log_addr = 0;
          }
        }
        if (log_addr) {
          char buffer[128] = {};
          const auto addr = ipv6AddressToString(buffer, sizeof(buffer), saddr, sizeof(struct sockaddr_in6));
          MDNS_LOG << "Local IPv6 address: " << addr << "\n";
        }
      }
    }
  }

  freeifaddrs(ifaddr);

#endif

  return num_sockets;
}

static int query_callback(int sock, const struct sockaddr *from, size_t addrlen, mdns_entry_type_t entry,
                          uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data,
                          size_t size, size_t name_offset, size_t name_length, size_t record_offset,
                          size_t record_length, void *user_data) {
  (void)sizeof(sock);
  (void)sizeof(query_id);
  (void)sizeof(name_length);
  (void)sizeof(user_data);

  QueryResult *res = static_cast<QueryResult*>(user_data);

  static char addrbuffer[64]{};
  static char namebuffer[256]{};
  static char entrybuffer[256]{};

  const auto fromaddrstr = ipAddressToString(addrbuffer, sizeof(addrbuffer), from, addrlen);
  const char *entrytype =
      (entry == MDNS_ENTRYTYPE_ANSWER) ? "answer" : ((entry == MDNS_ENTRYTYPE_AUTHORITY) ? "authority" : "additional");
  mdns_string_t entrystr = mdns_string_extract(data, size, &name_offset, entrybuffer, sizeof(entrybuffer));

  const int str_capacity = 1000;
  char str_buffer[str_capacity]={};

  if (rtype == MDNS_RECORDTYPE_PTR) {
    mdns_string_t namestr =
        mdns_record_parse_ptr(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
        res->host = std::string(namestr.str, namestr.str + namestr.length);
    snprintf(str_buffer, str_capacity, "%s : %s %.*s PTR %.*s rclass 0x%x ttl %u length %d\n", fromaddrstr.data(),
             entrytype, MDNS_STRING_FORMAT(entrystr), MDNS_STRING_FORMAT(namestr), rclass, ttl, (int)record_length);
  } else if (rtype == MDNS_RECORDTYPE_SRV) {
    mdns_record_srv_t srv =
        mdns_record_parse_srv(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
    snprintf(str_buffer, str_capacity,"%s : %s %.*s SRV %.*s priority %d weight %d port %d\n", fromaddrstr.data(), entrytype,
           MDNS_STRING_FORMAT(entrystr), MDNS_STRING_FORMAT(srv.name), srv.priority, srv.weight, srv.port);
  } else if (rtype == MDNS_RECORDTYPE_A) {
    struct sockaddr_in addr;
    mdns_record_parse_a(data, size, record_offset, record_length, &addr);
    const auto addrstr = ipv4AddressToString(namebuffer, sizeof(namebuffer), &addr, sizeof(addr));
    snprintf(str_buffer, str_capacity,"%s : %s %.*s A %s\n", fromaddrstr.data(), entrytype, MDNS_STRING_FORMAT(entrystr), addrstr.data());
    res->ipv4 = namebuffer;
  } else if (rtype == MDNS_RECORDTYPE_AAAA) {
    struct sockaddr_in6 addr;
    mdns_record_parse_aaaa(data, size, record_offset, record_length, &addr);
    const auto addrstr = ipv6AddressToString(namebuffer, sizeof(namebuffer), &addr, sizeof(addr));
    snprintf(str_buffer, str_capacity,"%s : %s %.*s AAAA %s\n", fromaddrstr.data(), entrytype, MDNS_STRING_FORMAT(entrystr), addrstr.data());
    res->ipv6 = namebuffer;
  } else if (rtype == MDNS_RECORDTYPE_TXT) {
    size_t parsed = mdns_record_parse_txt(data, size, record_offset, record_length, txtbuffer,
                                          sizeof(txtbuffer) / sizeof(mdns_record_txt_t));
    for (size_t itxt = 0; itxt < parsed; ++itxt) {
      if (txtbuffer[itxt].value.length) {
        snprintf(str_buffer, str_capacity,"%s : %s %.*s TXT %.*s = %.*s\n", fromaddrstr.data(), entrytype, MDNS_STRING_FORMAT(entrystr),
               MDNS_STRING_FORMAT(txtbuffer[itxt].key), MDNS_STRING_FORMAT(txtbuffer[itxt].value));
      } else {
        snprintf(str_buffer, str_capacity,"%s : %s %.*s TXT %.*s\n", fromaddrstr.data(), entrytype, MDNS_STRING_FORMAT(entrystr),
               MDNS_STRING_FORMAT(txtbuffer[itxt].key));
      }
    }
  } else {
    snprintf(str_buffer, str_capacity,"%s : %s %.*s type %u rclass 0x%x ttl %u length %d\n", fromaddrstr.data(), entrytype,
           MDNS_STRING_FORMAT(entrystr), rtype, rclass, ttl, (int)record_length);
  }
  MDNS_LOG << std::string(str_buffer);

  return 0;
}

int service_callback(int sock, const struct sockaddr *from, size_t addrlen, mdns_entry_type entry, uint16_t query_id,
                     uint16_t rtype, uint16_t rclass, uint32_t ttl, const void *data, size_t size, size_t name_offset,
                     size_t name_length, size_t record_offset, size_t record_length, void *user_data) {
  (void)sizeof(name_offset);
  (void)sizeof(name_length);
  (void)sizeof(ttl);

  if (static_cast<int>(entry) != MDNS_ENTRYTYPE_QUESTION) {
    return 0;
  }

  char addrbuffer[64] = {0};
  char namebuffer[256] = {0};

  const auto fromaddrstr = ipAddressToString(addrbuffer, sizeof(addrbuffer), from, addrlen);
  if (rtype == static_cast<uint16_t>(mdns_record_type::MDNS_RECORDTYPE_PTR)) {
    const mdns_string_t service =
        mdns_record_parse_ptr(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
    MDNS_LOG << fromaddrstr << " : question PTR " << std::string(service.str, service.length) << "\n";

    const char dns_sd[] = "_services._dns-sd._udp.local.";
    const ServiceRecord *service_record = (const ServiceRecord *)user_data;
    const size_t service_length = strlen(service_record->service);
    char sendbuffer[256] = {0};

    if ((service.length == (sizeof(dns_sd) - 1)) && (strncmp(service.str, dns_sd, sizeof(dns_sd) - 1) == 0)) {
      MDNS_LOG << "  --> answer " << service_record->service << " \n";
      mdns_discovery_answer(sock, from, addrlen, sendbuffer, sizeof(sendbuffer), service_record->service,
                            service_length);
    } else if ((service.length == service_length) &&
               (strncmp(service.str, service_record->service, service_length) == 0)) {
      uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
      MDNS_LOG << "  --> answer " << service_record->hostname << "." << service_record->service << " port "
               << service_record->port << " (" << (unicast ? "unicast" : "multicast") << ")\n";
      if (!unicast) addrlen = 0;
      char txt_record[] = "asdf=1";
      mdns_query_answer(sock, from, addrlen, sendbuffer, sizeof(sendbuffer), query_id, service_record->service,
                        service_length, service_record->hostname, strlen(service_record->hostname),
                        service_record->address_ipv4, service_record->address_ipv6, (uint16_t)service_record->port,
                        txt_record, sizeof(txt_record));
    }
  } else if (rtype == static_cast<uint16_t>(mdns_record_type::MDNS_RECORDTYPE_SRV)) {
    mdns_record_srv_t service =
        mdns_record_parse_srv(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
    MDNS_LOG << fromaddrstr << " : question SRV  " << std::string(service.name.str, service.name.length) << "\n";
#if 0
		if ((service.length == service_length) &&
		    (strncmp(service.str, service_record->service, service_length) == 0)) {
			uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
			printf("  --> answer %s.%s port %d (%s)\n", service_record->hostname,
			       service_record->service, service_record->port,
			       (unicast ? "unicast" : "multicast"));
			if (!unicast)
				addrlen = 0;
			char txt_record[] = "test=1";
			mdns_query_answer(sock, from, addrlen, sendbuffer, sizeof(sendbuffer), query_id,
			                  service_record->service, service_length, service_record->hostname,
			                  strlen(service_record->hostname), service_record->address_ipv4,
			                  service_record->address_ipv6, (uint16_t)service_record->port,
			                  txt_record, sizeof(txt_record));
		}
#endif
  }
  return 0;
}

mDNS::~mDNS() { stopService(); }

void mDNS::startService() {
  if (running_) {
    stopService();
  }

  running_ = true;
  worker_thread_ = std::thread([this]() { this->runMainLoop(); });
}

void mDNS::stopService() {
  running_ = false;
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

bool mDNS::isServiceRunning() { return running_; }

void mDNS::setServiceHostname(const std::string &hostname) { hostname_ = hostname; }

void mDNS::setServicePort(std::uint16_t port) { port_ = port; }

void mDNS::setServiceName(const std::string &name) { name_ = name; }

void mDNS::setServiceTxtRecord(const std::string &txt_record) { txt_record_ = txt_record; }

void mDNS::runMainLoop() {
  constexpr size_t number_of_sockets = 32;
  int sockets[number_of_sockets];
  const int num_sockets = openServiceSockets(sockets, sizeof(sockets) / sizeof(sockets[0]));
  if (num_sockets <= 0) {
    const auto msg = "Error: Failed to open any client sockets";
    MDNS_LOG << msg << "\n";
    throw std::runtime_error(msg);
  }

  MDNS_LOG << "Opened " << std::to_string(num_sockets) << " socket" << (num_sockets ? "s" : "")
           << " for mDNS service\n";
  MDNS_LOG << "Service mDNS: " << name_ << ":" << port_ << "\n";
  MDNS_LOG << "Hostname: " << hostname_.data() << "\n";

  constexpr size_t capacity = 2048u;
  std::shared_ptr<void> buffer(malloc(capacity), free);
  ServiceRecord service_record{};
  service_record.service = name_.data();
  service_record.hostname = hostname_.data();
  service_record.address_ipv4 = has_ipv4_ ? service_address_ipv4_ : 0;
  service_record.address_ipv6 = has_ipv6_ ? service_address_ipv6_ : 0;
  service_record.port = port_;

  // This is a crude implementation that checks for incoming queries
  while (running_) {
    int nfds = 0;
    fd_set readfs{};
    FD_ZERO(&readfs);
    for (int isock = 0; isock < num_sockets; ++isock) {
      if (sockets[isock] >= nfds) nfds = sockets[isock] + 1;
      FD_SET(sockets[isock], &readfs);
    }

    if (select(nfds, &readfs, 0, 0, 0) >= 0) {
      for (int isock = 0; isock < num_sockets; ++isock) {
        if (FD_ISSET(sockets[isock], &readfs)) {
          mdns_socket_listen(sockets[isock], buffer.get(), capacity, service_callback, &service_record);
        }
        FD_SET(sockets[isock], &readfs);
      }
    } else {
      break;
    }
  }

  for (int isock = 0; isock < num_sockets; ++isock) {
    mdns_socket_close(sockets[isock]);
  }
  MDNS_LOG << "Closed socket " << (num_sockets ? "s" : "") << "\n";
}

std::list<std::shared_ptr<QueryResult>> mDNS::executeQuery(const std::string &service) {
  int sockets[32];
  int query_id[32];
  int num_sockets = openClientSockets(sockets, sizeof(sockets) / sizeof(sockets[0]), 0);

  if (num_sockets <= 0) {
    const auto msg = "Failed to open any client sockets";
    MDNS_LOG << msg << "\n";
    throw std::runtime_error(msg);
  }
  MDNS_LOG << "Opened " << num_sockets << " socket" << (num_sockets ? "s" : "") << " for mDNS query\n";

  size_t capacity = 2048;
  void *buffer = malloc(capacity);
  size_t records;

  MDNS_LOG << "Sending mDNS query: " << service << "\n";
  for (int isock = 0; isock < num_sockets; ++isock) {
    query_id[isock] = mdns_query_send(sockets[isock], MDNS_RECORDTYPE_PTR, service.data(), strlen(service.data()),
                                      buffer, capacity, 0);
    if (query_id[isock] < 0) {
      MDNS_LOG << "Failed to send mDNS query: " << strerror(errno) << "\n";
    }
  }

  // This is a simple implementation that loops for 5 seconds or as long as we
  // get replies
  std::list<std::shared_ptr<QueryResult>> result;

  int res{};
  MDNS_LOG << "Reading mDNS query replies\n";
  do {
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    int nfds = 0;
    fd_set readfs;
    FD_ZERO(&readfs);
    for (int isock = 0; isock < num_sockets; ++isock) {
      if (sockets[isock] >= nfds) nfds = sockets[isock] + 1;
      FD_SET(sockets[isock], &readfs);
    }

    records = 0;
    res = select(nfds, &readfs, 0, 0, &timeout);
    if (res > 0) {
      for (int isock = 0; isock < num_sockets; ++isock) {
        if (FD_ISSET(sockets[isock], &readfs)) {
          std::shared_ptr<QueryResult> query_result = std::make_shared<QueryResult>();

          records +=
              mdns_query_recv(sockets[isock], buffer, capacity, query_callback, query_result.get(), query_id[isock]);

          result.push_back(query_result);
        }
        FD_SET(sockets[isock], &readfs);
      }
    }
  } while (res > 0);

  free(buffer);

  for (int isock = 0; isock < num_sockets; ++isock) {
    mdns_socket_close(sockets[isock]);
  }
  MDNS_LOG << "Closed socket" << (num_sockets ? "s" : "") << "\n";

  return result;
}

void mDNS::executeDiscovery() {
  int sockets[32];
  int num_sockets = openClientSockets(sockets, sizeof(sockets) / sizeof(sockets[0]), 0);
  if (num_sockets <= 0) {
    const auto msg = "Failed to open any client sockets";
    MDNS_LOG << msg << "\n";
    throw std::runtime_error(msg);
  }

  MDNS_LOG << "Opened " << num_sockets << " socket" << (num_sockets ? "s" : "") << " for DNS-SD\n";
  MDNS_LOG << "Sending DNS-SD discovery\n";
  for (int isock = 0; isock < num_sockets; ++isock) {
    if (mdns_discovery_send(sockets[isock])) {
      MDNS_LOG << "Failed to send DNS-DS discovery: " << strerror(errno) << " \n";
    }
  }

  size_t capacity = 2048;
  void *buffer = malloc(capacity);
  void *user_data = 0;
  size_t records;

  // This is a simple implementation that loops for 5 seconds or as long as we
  // get replies
  int res;
  MDNS_LOG << "Reading DNS-SD replies\n";
  do {
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    int nfds = 0;
    fd_set readfs;
    FD_ZERO(&readfs);
    for (int isock = 0; isock < num_sockets; ++isock) {
      if (sockets[isock] >= nfds) nfds = sockets[isock] + 1;
      FD_SET(sockets[isock], &readfs);
    }

    records = 0;
    res = select(nfds, &readfs, 0, 0, &timeout);
    if (res > 0) {
      for (int isock = 0; isock < num_sockets; ++isock) {
        if (FD_ISSET(sockets[isock], &readfs)) {
          auto query_result = std::make_shared<QueryResult>();
          records += mdns_discovery_recv(sockets[isock], buffer, capacity, query_callback, query_result.get());
        }
      }
    }
  } while (res > 0);

  free(buffer);

  for (int isock = 0; isock < num_sockets; ++isock) {
    mdns_socket_close(sockets[isock]);
  }
  MDNS_LOG << "Closed socket" << (num_sockets ? "s" : "") << "\n";
}

}  // namespace mdns_cpp
