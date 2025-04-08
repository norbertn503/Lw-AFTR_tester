#include "defines.h"
#include "includes.h"
#include "Throughput.h"
#include "latency.h"

// after reading the parameters for throughput measurement, further two parameters are read
int Latency::readCmdLine(int argc, const char *argv[])
{
  if (Throughput::readCmdLine(argc - 2, argv) < 0)
    return -1;
  if (sscanf(argv[7], "%hu", &first_tagged_delay) != 1 || first_tagged_delay > 3600)
  {
    std::cerr << "Input Error: Delay before timestamps must be between 0 and 3600." << std::endl;
    return -1;
  }
  if (test_duration <= first_tagged_delay)
  {
    std::cerr << "Input Error: Test test_duration MUST be longer than the delay before the first tagged frame." << std::endl;
    return -1;
  }
  if (sscanf(argv[8], "%hu", &num_of_tagged) != 1 || num_of_tagged < 1 || num_of_tagged > 50000)
  {
    std::cerr << "Input Error: Number of tagged frames must be between 1 and 50000." << std::endl;
    return -1;
  }
  if ((test_duration - first_tagged_delay) * frame_rate < num_of_tagged)
  {
    std::cerr << "Input Error: There are not enough test frames in the (test_duration-first_tagged_delay) interval to be tagged." << std::endl;
    return -1;
  }
  return 0;
}

int Latency::senderPoolSize()
{
  return Throughput::senderPoolSize() + num_of_tagged; // tagged frames are also pre-generated
}

struct rte_mbuf *mkLatencyTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
    const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
    const uint32_t *src_ip, uint32_t *dst_ip, unsigned var_sport, unsigned var_dport, uint16_t id)
{
// printf("inside mkTestFrame4: the beginning\n");
    struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
    if (!pkt_mbuf)
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
    length -= RTE_ETHER_CRC_LEN;                                                                                       // exclude CRC from the frame length
    pkt_mbuf->pkt_len = pkt_mbuf->data_len = length;                                                               // set the length in both places
    uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *);                                                          // Access the Test Frame in the message buffer
    rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt);                                                // Ethernet header
    rte_ipv4_hdr *ip_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt + sizeof(rte_ether_hdr));                                      // IPv4 header
    rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr));                     // UDP header
    uint8_t *udp_data = reinterpret_cast<uint8_t *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr)); // UDP data

    mkEthHeader(eth_hdr, dst_mac, src_mac, 0x0800); // contains an IPv4 packet
    int ip_length = length - sizeof(rte_ether_hdr);
    mkIpv4Header(ip_hdr, ip_length, src_ip, dst_ip); // Does not set IPv4 header checksum
    int udp_length = ip_length - sizeof(rte_ipv4_hdr);   // No IP Options are used
    mkUdpHeader(udp_hd, udp_length, var_sport, var_dport);
    int data_length = udp_length - sizeof(rte_udp_hdr);
    mkLatencyData(udp_data, data_length, id);
    udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hd); // UDP checksum is calculated and set
    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);               // IPv4 header checksum is set now
    return pkt_mbuf;
}

struct rte_mbuf *mkLatencyIpv4inIpv6Tun(uint16_t length, rte_mempool *pkt_pool, const char *direction,
    const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
    struct in6_addr *src_ipv6, struct in6_addr *dst_ipv6, unsigned var_sport, unsigned var_dport,
    const uint32_t *src_ipv4, uint32_t *dst_ipv4, uint16_t id)
{
    struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if (!pkt_mbuf)
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
  length -= RTE_ETHER_CRC_LEN;
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length;
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *);
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt);                                                // Ethernet header
  rte_ipv6_hdr *ipv6_hdr = reinterpret_cast<rte_ipv6_hdr *>(pkt + sizeof(rte_ether_hdr));                        // IPv6 header
  rte_ipv4_hdr *ipv4_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr));    // IPv4 header                                      
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr) + sizeof(rte_ipv4_hdr));     // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr)); // UDP data
  
  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x86DD); // contains an IPv6 packet
  int ipv6_length = length - sizeof(rte_ether_hdr);
  mkIpv6Header(ipv6_hdr, ipv6_length, src_ipv6, dst_ipv6, 0x04); //0x04 for IPIP
  int ipv4_length = ipv6_length - sizeof(rte_ipv6_hdr);
  mkIpv4Header(ipv4_hdr, ipv4_length, src_ipv4, dst_ipv4); // Does not set IPv4 header checksum
  int udp_length = ipv4_length - sizeof(rte_ipv4_hdr); // No IP Options are used
  mkUdpHeader(udp_hd, udp_length, var_sport, var_dport);
  int data_length = udp_length - sizeof(rte_udp_hdr);
  mkLatencyData(udp_data, data_length, id);
  udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum(ipv4_hdr, udp_hd); // UDP checksum is calculated and set
  //Kell az IPv4-re külön checksumot számolni?
  ipv4_hdr->hdr_checksum = rte_ipv4_cksum(ipv4_hdr); 
  return pkt_mbuf;
}

void mkLatencyData(uint8_t *data, uint16_t length, uint16_t latency_frame_id)
{
  unsigned i;
  uint8_t identify[8] = {'I', 'd', 'e', 'n', 't', 'i', 'f', 'y'}; // Identificion of the Latency Frames
  uint64_t *id = (uint64_t *)identify;
  *(uint64_t *)data = *id;
  data += 8;
  length -= 8;
  *(uint16_t *)data = latency_frame_id;
  data += 2;
  length -= 2;
  for (i = 0; i < length; i++)
  data[i] = i % 256;
}

senderCommonParametersLatency::senderCommonParametersLatency(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t test_duration_,
    uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, uint32_t number_of_lwB4s_, lwB4_data *lwB4_array_,
    struct in6_addr *dut_ipv6_tunnel_, uint32_t *tester_fw_rec_ipv4_, in6_addr *tester_bg_send_ipv6_, struct in6_addr *tester_bg_rec_ipv6_,
    uint16_t fw_dport_min_, uint16_t fw_dport_max_, uint16_t first_tagged_delay_, uint16_t num_of_tagged_) : senderCommonParameters(ipv6_frame_size_, ipv4_frame_size_, frame_rate_,  test_duration_,
                                                                                                             n_,  m_,  hz_,  start_tsc_,  number_of_lwB4s_,  lwB4_array_, dut_ipv6_tunnel_, 
                                                                                                             tester_fw_rec_ipv4_, tester_bg_send_ipv6_, tester_bg_rec_ipv6_, fw_dport_min_, fw_dport_max_
    )
    {
        first_tagged_delay = first_tagged_delay_;
        num_of_tagged = num_of_tagged_;
    }
    
senderParametersLatency::senderParametersLatency(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
    struct ether_addr *dst_mac_, struct ether_addr *src_mac_, uint16_t bg_fw_sport_min_, uint16_t bg_fw_sport_max_, uint16_t bg_fw_dport_min_,
    uint16_t bg_fw_dport_max_, uint64_t *send_ts_) : senderParameters(cp_, pkt_pool_, eth_id_, direction_, dst_mac_, src_mac_, bg_fw_sport_min_, bg_fw_sport_max_, bg_fw_dport_min_, bg_fw_dport_max_
    )
    {
        send_ts = send_ts_;
    }


receiverParametersLatency::receiverParametersLatency(uint64_t finish_receiving_, uint8_t eth_id_, const char *direction_, uint16_t num_of_tagged_, uint64_t *receive_ts_) : receiverParameters(finish_receiving_, eth_id_, direction_)
{
    num_of_tagged = num_of_tagged_;
    receive_ts = receive_ts_;
}
    