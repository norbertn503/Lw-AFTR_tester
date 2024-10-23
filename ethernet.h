#include <cstdint>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#ifndef PacketHandler_H
#define PacketHandler_H

class PacketHandler{
    private:
        struct ether_addr *dst_mac, *src_mac;
        const uint16_t ether_type;

};
#endif