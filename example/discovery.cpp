#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <iostream>
#include <thread>

#include "mdns_cpp/mdns.hpp"

#ifdef _WIN32
#include <winsock2.h>
#endif

void onInterruptHandler(int s) {
  std::cout << "Caught signal: " << s << std::endl;
  exit(0);
}

int main() {
  signal(SIGINT, onInterruptHandler);

#ifdef _WIN32
  int iResult;
  WSADATA wsaData;
  // Initialize Winsock
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

  if (iResult != 0) {
    printf("WSAStartup failed: %d\n", iResult);
    return 1;
  }
#endif

  mdns_cpp::mDNS mdns;
  mdns.executeDiscovery();

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}
