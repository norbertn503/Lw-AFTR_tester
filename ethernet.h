#include <cstdint>
#include <rte_ether.h>

#ifndef ETHERNET_H
#define ETHERNET_H

class Ethernet{
    private:
        struct ether_addr *dst_mac, *src_mac;
        const uint16_t ether_type;

};
#endif