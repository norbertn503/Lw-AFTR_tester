#ifndef Throughput_H
#define Throughput_H

#include <cstdint>
#include <rte_mbuf.h>
#include <netinet/in.h>
#include <vector>

struct lwB4_data
{
public:
  uint32_t ipv4_addr;
  uint16_t ipv4_addr_chksum;
  uint32_t psid;
  uint32_t psid_length;
  struct in6_addr b4_ipv6_addr;
  struct in6_addr aftr_tunnel_addr;
  uint32_t min_port;
  uint32_t max_port;
};

class Throughput
{
public:
  // parameters from the configuration file
  //struct in6_addr tester_left_ipv6;  // Tester's left interface IPv6 address (unused for now as we will use the MAP address instead)
    uint32_t tester_fw_rec_ipv4;        // Tester's right interface IPv4 address
    struct in6_addr tester_fw_send_ipv6;

    struct in6_addr tester_bg_send_ipv6; // Tester's right interface IPv6 address (used for sending background traffic)
    struct in6_addr tester_bg_rec_ipv6;
    

    uint8_t tester_fw_mac[6];  // Tester's left interface MAC address, forward direction, from teser to DUT
    uint8_t tester_rv_mac[6]; // Tester's right interface MAC address, reverse direction,
    uint8_t dut_fw_mac[6];     // DUT's left interface MAC address, forward direction,
    uint8_t dut_rv_mac[6];    // DUT's right interface MAC address, reverse direction,

  // encoding: 1: increase, 2: decrease, 3: pseudorandom
    unsigned fwd_var_sport; // control value for variable source port numbers in the forward direction
    unsigned fwd_var_dport; // control value for variable destination port numbers in the forward direction
    unsigned rev_var_sport; // control value for variable source port numbers in the reverse direction
    unsigned rev_var_dport; // control value for variable destination port numbers in the reverse direction

    uint16_t fwd_dport_min; // minumum value for foreground's destination port in the forward direction
    uint16_t fwd_dport_max; // maximum value for foreground's destination port in the forward direction
    uint16_t rev_sport_min; // minumum value for foreground's source port in the reverse direction
    uint16_t rev_sport_max; // maximum value for foreground's source port in the reverse direction

    uint16_t bg_fw_sport_min; // minumum value for background's source port in the forward direction (destination in the reverse direction)
    uint16_t bg_fw_sport_max; // maximum value for background's source port in the forward direction (destination in the reverse diretion)
    uint16_t bg_fw_dport_min; // minumum value for background's destination port in the forward direction (source in the reverse direction)
    uint16_t bg_fw_dport_max; // maximum value for background's destination port in the forward direction (source in the reverse direction)

    int cpu_fw_send;    // lcore for forward direction Sender
    int cpu_fw_receive;  // lcore for forward direction Receiver
    int cpu_rv_send;   // lcore for reverse direction Sender
    int cpu_rv_receive; // lcore for reverse direction Receiver

    uint8_t memory_channels; // Number of memory channnels (for the EAL init.)
    int forward, reverse;    // directions are active if set
    int promisc;             // promiscuous mode is active if set

  // positional parameters from command line
  uint16_t ipv6_frame_size; // size of the frames carrying IPv6 datagrams (including the 4 bytes of the FCS at the end)
  uint16_t ipv4_frame_size; // redundant parameter, automatically set as ipv6_frame_size-20
  uint32_t frame_rate;      // number of frames per second
  uint16_t test_duration;   // test duration (in seconds, 1-3600)
  uint16_t stream_timeout;  // Stream timeout (in milliseconds, 0-60000)
  uint32_t n, m;            // modulo and threshold for controlling background traffic proportion

  // further data members, set by init()
  rte_mempool *pkt_pool_left_sender, *pkt_pool_right_receiver; // packet pools for the forward direction testing
  rte_mempool *pkt_pool_right_sender, *pkt_pool_left_receiver; // packet pools for the reverse direction testing
  uint64_t hz;                                                 // number of clock cycles per second
  uint64_t start_tsc;                                          // sending of the test frames will begin at this time
  uint64_t finish_receiving;                                   // receiving of the test frames will end at this time
  uint64_t frames_to_send;                                     // number of frames to send

  // lw4o6 parameters
  uint32_t psid_length;            // The number of BMR's PSID bits
  uint32_t psid;
  uint16_t num_of_port_sets;      // The number of port sets that can be obtained according to the psid_length
  uint16_t num_of_ports;          // The number of ports in each port set
  uint32_t number_of_lwB4s;             // Number of simulated lwB4s
  struct in6_addr dut_ipv6_tunnel; // The BMR’s Rule IPv6 Prefix of the MAP address
  struct in6_addr dut_fw_ipv6;
  uint32_t lwb4_start_ipv4;
  uint32_t lwb4_end_ipv4;
  lwB4_data *lwB4_array;
  std::vector<lwB4_data> tmp_lwb4data; // for reading the lwB4 data file

  // helper functions (see their description at their definition)
  int findKey(const char *line, const char *key);
  int readConfigFile(const char *filename);
  int readlwB4Data(const char *filename);
  int readCmdLine(int argc, const char *argv[]);
  int init(const char *argv0, uint16_t leftport, uint16_t rightport);
  virtual int senderPoolSize();
  void numaCheck(uint16_t port, const char *port_side, int cpu, const char *cpu_name);
  //void buildMapArray();

  // perform throughput measurement
  void measure(uint16_t leftport, uint16_t rightport);

  Throughput();
};

// send test frame
int send(void *par);

// receive and count test frames
int receive(void *par);

// receive and count test frames
int receive(void *par);

struct rte_mbuf *mkTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct rte_ether_addr *src_mac,
                              const uint32_t *src_ip, uint32_t *dst_ip, unsigned var_sport, unsigned var_dport);
void mkEthHeader(struct rte_ether_hdr *eth, const struct ether_addr *dst_mac, const struct ether_addr *src_mac, const uint16_t ether_type);
void mkIpv4Header(struct rte_ipv4_hdr *ip, uint16_t length, const uint32_t *src_ip, uint32_t *dst_ip);
void mkUdpHeader(struct rte_udp_hdr *udp, uint16_t length, unsigned var_sport, unsigned var_dport);
void mkData(uint8_t *data, uint16_t length);
void numaCheck(uint16_t port, const char *port_side, int cpu, const char *cpu_name);

struct rte_mbuf *mkTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct rte_ether_addr *src_mac,
                              struct in6_addr *src_ip, struct in6_addr *dst_ip, unsigned var_sport, unsigned var_dport);
void mkIpv6Header(struct rte_ipv6_hdr *ip, uint16_t length, struct in6_addr *src_ip, struct in6_addr *dst_ip);
struct rte_mbuf *mkIpv4inIpv6Tun(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              struct in6_addr *src_ipv6, struct in6_addr *dst_ipv6, unsigned var_sport, unsigned var_dport,
                              const uint32_t *src_ipv4, uint32_t *dst_ipv4);

// to store identical parameters for both senders
class senderCommonParameters
{
public:
  uint16_t ipv6_frame_size; 
  uint16_t ipv4_frame_size; 
  uint32_t frame_rate;      
  uint16_t test_duration;   
  uint32_t n, m;            
  uint64_t hz;              
  uint64_t start_tsc;       
  uint64_t frames_to_send;  
  uint32_t number_of_lwB4s;
  lwB4_data *lwB4_array;

  uint32_t *tester_fw_rec_ipv4;
  struct in6_addr *dut_ipv6_tunnel; //
  struct in6_addr *tester_bg_send_ipv6; 
  struct in6_addr *tester_bg_rec_ipv6;
  
  uint16_t fw_dport_min; 
  uint16_t fw_dport_max;

  senderCommonParameters(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t test_duration_,
                        uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, uint32_t number_of_lwB4s_, lwB4_data *lwB4_array_,
                        struct in6_addr *dut_ipv6_tunnel_, uint32_t *tester_fw_rec_ipv4_, in6_addr *tester_bg_send_ipv6_, struct in6_addr *tester_bg_rec_ipv6_,
                        uint16_t fw_dport_min_, uint16_t fw_dport_max_
                        );
  senderCommonParameters();
};

class senderParameters
{
public:
  class senderCommonParameters *cp; // a pointer to the common parameters
  rte_mempool *pkt_pool; // sender's packet pool
  uint8_t eth_id; // ethernet ID
  const char *direction; // test direction (forward or reverse)
  struct ether_addr *dst_mac, *src_mac; // destination and source mac addresses
  
  uint16_t bg_fw_sport_min; 
  uint16_t bg_fw_sport_max;
  uint16_t bg_fw_dport_min; 
  uint16_t bg_fw_dport_max; 

  //unsigned var_sport, var_dport; // how source and destination port numbers vary? 1:increase, 2:decrease, or 3:pseudorandomly change
  //suint16_t preconfigured_port_min, preconfigured_port_max; // The preconfigured range of ports (i.e., destination in case of forward and source in case of reverse)
  
  senderParameters(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
                   struct ether_addr *dst_mac_, struct ether_addr *src_mac_, uint16_t bg_fw_sport_min_, uint16_t bg_fw_sport_max_, uint16_t bg_fw_dport_min_,
                   uint16_t bg_fw_dport_max_
                   );
  
  senderParameters();
};

// to store parameters for each receiver
class receiverParameters
{
public:
  uint64_t finish_receiving; // this one is common, but it was not worth dealing with it.
  uint8_t eth_id;
  const char *direction;

  receiverParameters(uint64_t finish_receiving_, uint8_t eth_id_, const char *direction_);
  receiverParameters();
};
#endif