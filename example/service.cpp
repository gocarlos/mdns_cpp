#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <iostream>
#include <thread>

#include "mdns_cpp/mdns.hpp"

void onInterruptHandler(int s) {
  std::cout << "Caught signal: " << s << std::endl;
  exit(0);
}

int main() {
  signal(SIGINT, onInterruptHandler);

  mdns_cpp::mDNS mdns;

  mdns.setServiceHostname("AirForce1");

  mdns.startService();

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}
