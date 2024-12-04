#include "Throughput.h"
#include "includes.h"
#include "defines.h"


char coresList[101];  // buffer for preparing the list of lcores for DPDK init (like a command line argument)
char numChannels[11]; // buffer for printing the number of memory channels into a string for DPDK init (like a command line argument)

Throughput::Throughput(){
  // initialize some data members to default or invalid value //Just in case of not setting them in the configuration file and the Tester did not exit
  forward = 1;                   // default value, forward direction is active
  reverse = 1;                   // default value, reverse direction is active
  promisc = 0;                   // default value, promiscuous mode is inactive
  left_sender_cpu = -1;          // MUST be set in the config file if forward != 0
  right_receiver_cpu = -1;       // MUST be set in the config file if forward != 0
  right_sender_cpu = -1;         // MUST be set in the config file if reverse != 0
  left_receiver_cpu = -1;        // MUST be set in the config file if reverse != 0
  memory_channels = 1;           // default value, this value will be set, if not specified in the config file
  fwd_var_sport = 3;             // default value: use pseudorandom change for the source port numbers in the forward direction
  fwd_var_dport = 3;             // default value: use pseudorandom change for the destination port numbers in the forward direction
  fwd_dport_min = 1;             // default value: as recommended by RFC 4814
  fwd_dport_max = 49151;         // default value: as recommended by RFC 4814
  rev_var_sport = 3;             // default value: use pseudorandom change for the source port numbers in the reverse direction
  rev_var_dport = 3;             // default value: use pseudorandom change for the destination port numbers in the reverse direction
  rev_sport_min = 1024;          // default value: as recommended by RFC 4814
  rev_sport_max = 65535;         // default value: as recommended by RFC 4814
  bg_sport_min = 1024;           // default value: as recommended by RFC 4814
  bg_sport_max = 65535;          // default value: as recommended by RFC 4814
  bg_dport_min = 1;              // default value: as recommended by RFC 4814
  bg_dport_max = 49151;          // default value: as recommended by RFC 4814
  
  

  // some other variables
  //dmr_ipv6 = IN6ADDR_ANY_INIT;  
  //fwUniqueEAComb = NULL;         
  //rvUniqueEAComb = NULL;                                    
  fwCE = NULL;                  
  rvCE = NULL;                  
};


int Throughput::init(const char *argv0, uint16_t leftport, uint16_t rightport)
{
  const char *rte_argv[6];                                                     // parameters for DPDK EAL init, e.g.: {NULL, "-l", "4,5,6,7", "-n", "2", NULL};
  int rte_argc = static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1; // argc value for DPDK EAL init
  struct rte_eth_conf cfg_port;                                                // for configuring the Ethernet ports
  struct rte_eth_link link_info;                                               // for retrieving link info by rte_eth_link_get()
  int trials;                                                                  // cycle variable for port state checking

  // prepare 'command line' arguments for rte_eal_init
  rte_argv[0] = argv0; // program name
  rte_argv[1] = "-l";  // list of lcores will follow
  // Only lcores for the active directions are to be included (at least one of them MUST be non-zero)
  if (forward && reverse)
  {
    // both directions are active
    snprintf(coresList, 101, "0,%d,%d,%d,%d", left_sender_cpu, right_receiver_cpu, right_sender_cpu, left_receiver_cpu);
  }
  else if (forward)
    snprintf(coresList, 101, "0,%d,%d", left_sender_cpu, right_receiver_cpu); // only forward (left to right) is active
  else
    snprintf(coresList, 101, "0,%d,%d", right_sender_cpu, left_receiver_cpu); // only reverse (right to left) is active
  rte_argv[2] = coresList;
  rte_argv[3] = "-n";
  snprintf(numChannels, 11, "%hhu", memory_channels);
  rte_argv[4] = numChannels;
  rte_argv[5] = 0;

  if (rte_eal_init(rte_argc, const_cast<char **>(rte_argv)) < 0)
  {
    std::cerr << "Error: DPDK RTE initialization failed, Tester exits." << std::endl;
    return -1;
  }

  if (!rte_eth_dev_is_valid_port(leftport))
  {
    std::cerr << "Error: Network port #" << leftport << " provided as Left Port is not available, Tester exits." << std::endl;
    return -1;
  }

  if (!rte_eth_dev_is_valid_port(rightport))
  {
    std::cerr << "Error: Network port #" << rightport << " provided as Right Port is not available, Tester exits." << std::endl;
    return -1;
  }

  // prepare for configuring the Ethernet ports
  memset(&cfg_port, 0, sizeof(cfg_port));   // e.g. no CRC generation offloading, etc. (May be improved later!)
  cfg_port.txmode.mq_mode = ETH_MQ_TX_NONE; // no multi queues
  cfg_port.rxmode.mq_mode = ETH_MQ_RX_NONE; // no multi queues

  if (rte_eth_dev_configure(leftport, 1, 1, &cfg_port) < 0)
  {
    std::cerr << "Error: Cannot configure network port #" << leftport << " provided as Left Port, Tester exits." << std::endl;
    return -1;
  }

  if (rte_eth_dev_configure(rightport, 1, 1, &cfg_port) < 0)
  {
    std::cerr << "Error: Cannot configure network port #" << rightport << " provided as Right Port, Tester exits." << std::endl;
    return -1;
  }

  // Important remark: with no regard whether actual test will be performed in the forward or reverese direcetion,
  // all TX and RX queues MUST be set up properly, otherwise rte_eth_dev_start() will cause segmentation fault.
  // Sender pool size calculation uses 0 instead of num_{left,right}_nets, when no actual frame sending is needed.

  // calculate packet pool sizes and then create the pools
  int left_sender_pool_size = senderPoolSize();
  int right_sender_pool_size = senderPoolSize();
  int receiver_pool_size = PORT_RX_QUEUE_SIZE + 2 * MAX_PKT_BURST + 100; // While one of them is processed, the other one is being filled.

  r = rte_pktmbuf_pool_create("pp_left_sender", left_sender_pool_size, PKTPOOL_CACHE, 0,
                                                 RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(left_sender_cpu));
  if (!pkt_pool_left_sender)
  {
    std::cerr << "Error: Cannot create packet pool for Left Sender, Tester exits." << std::endl;
    return -1;
  }
  pkt_pool_right_receiver = rte_pktmbuf_pool_create("pp_right_receiver", receiver_pool_size, PKTPOOL_CACHE, 0,
                                                    RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(right_receiver_cpu));
  if (!pkt_pool_right_receiver)
  {
    std::cerr << "Error: Cannot create packet pool for Right Receiver, Tester exits." << std::endl;
    return -1;
  }

  pkt_pool_right_sender = rte_pktmbuf_pool_create("pp_right_sender", right_sender_pool_size, PKTPOOL_CACHE, 0,
                                                  RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(right_sender_cpu));
  if (!pkt_pool_right_sender)
  {
    std::cerr << "Error: Cannot create packet pool for Right Sender, Tester exits." << std::endl;
    return -1;
  }
  pkt_pool_left_receiver = rte_pktmbuf_pool_create("pp_left_receiver", receiver_pool_size, PKTPOOL_CACHE, 0,
                                                   RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(left_receiver_cpu));
  if (!pkt_pool_left_receiver)
  {
    std::cerr << "Error: Cannot create packet pool for Left Receiver, Tester exits." << std::endl;
    return -1;
  }

  // set up the TX/RX queues
  if (rte_eth_tx_queue_setup(leftport, 0, PORT_TX_QUEUE_SIZE, rte_eth_dev_socket_id(leftport), NULL) < 0)
  {
    std::cerr << "Error: Cannot setup TX queue for Left Sender, Tester exits." << std::endl;
    return -1;
  }
  if (rte_eth_rx_queue_setup(rightport, 0, PORT_RX_QUEUE_SIZE, rte_eth_dev_socket_id(rightport), NULL, pkt_pool_right_receiver) < 0)
  {
    std::cerr << "Error: Cannot setup RX queue for Right Receiver, Tester exits." << std::endl;
    return -1;
  }
  if (rte_eth_tx_queue_setup(rightport, 0, PORT_TX_QUEUE_SIZE, rte_eth_dev_socket_id(rightport), NULL) < 0)
  {
    std::cerr << "Error: Cannot setup TX queue for Right Sender, Tester exits." << std::endl;
    return -1;
  }
  if (rte_eth_rx_queue_setup(leftport, 0, PORT_RX_QUEUE_SIZE, rte_eth_dev_socket_id(leftport), NULL, pkt_pool_left_receiver) < 0)
  {
    std::cerr << "Error: Cannot setup RX queue for Left Receiver, Tester exits." << std::endl;
    return -1;
  }

  // start the Ethernet ports
  if (rte_eth_dev_start(leftport) < 0)
  {
    std::cerr << "Error: Cannot start network port #" << leftport << " provided as Left Port, Tester exits." << std::endl;
    return -1;
  }
  if (rte_eth_dev_start(rightport) < 0)
  {
    std::cerr << "Error: Cannot start network port #" << rightport << " provided as Right Port, Tester exits." << std::endl;
    return -1;
  }

  if (promisc)
  {
    rte_eth_promiscuous_enable(leftport);
    rte_eth_promiscuous_enable(rightport);
  }

  // check links' states (wait for coming up), try maximum MAX_PORT_TRIALS times
  trials = 0;
  do
  {
    if (trials++ == MAX_PORT_TRIALS)
    {
      std::cerr << "Error: Left Ethernet port is DOWN, Tester exits." << std::endl;
      return -1;
    }
    rte_eth_link_get(leftport, &link_info);
  } while (link_info.link_status == ETH_LINK_DOWN);
  trials = 0;
  do
  {
    if (trials++ == MAX_PORT_TRIALS)
    {
      std::cerr << "Error: Right Ethernet port is DOWN, Tester exits." << std::endl;
      return -1;
    }
    rte_eth_link_get(rightport, &link_info);
  } while (link_info.link_status == ETH_LINK_DOWN);

  // Some sanity checks: NUMA node of the cores and of the NICs are matching or not...
  if (numa_available() == -1)
    std::cout << "Info: This computer does not support NUMA." << std::endl;
  else
  {
    if (numa_num_configured_nodes() == 1)
      std::cout << "Info: Only a single NUMA node is configured, there is no possibilty for mismatch." << std::endl;
    else
    {
      if (forward)
      {
        numaCheck(leftport, "Left", left_sender_cpu, "Left Sender");
        numaCheck(rightport, "Right", right_receiver_cpu, "Right Receiver");
      }
      if (reverse)
      {
        numaCheck(rightport, "Right", right_sender_cpu, "Right Sender");
        numaCheck(leftport, "Left", left_receiver_cpu, "Left Receiver");
      }
    }
  }

  // Some sanity checks: TSCs of the used cores are synchronized or not...
  if (forward)
  {
    check_tsc(left_sender_cpu, "Left Sender");
    check_tsc(right_receiver_cpu, "Right Receiver");
  }
  if (reverse)
  {
    check_tsc(right_sender_cpu, "Right Sender");
    check_tsc(left_receiver_cpu, "Left Receiver");
  }

  // prepare further values for testing
  hz = rte_get_timer_hz();                                                       // number of clock cycles per second
  start_tsc = rte_rdtsc() + hz * START_DELAY / 1000;                             // Each active sender starts sending at this time
  finish_receiving = start_tsc + hz * (test_duration + stream_timeout / 1000.0); // Each receiver stops at this time

  // producing some important values from the BMR configuration parameters for the next tasks (e.g., generating the pseudorandom EA combinations)
  bmr_ipv4_suffix_length = 32 - bmr_ipv4_prefix_length;
  psid_length = bmr_EA_length - bmr_ipv4_suffix_length;
  num_of_port_sets = pow(2.0, psid_length);
  num_of_ports = (uint16_t)(65536.0 / num_of_port_sets); // 65536.0 denotes the total number of port possibilities can be there in the 16-bit udp port number(i.e., 2 ^ 16)
  int num_of_suffixes = pow(2.0, bmr_ipv4_suffix_length)-2; //-2 to exclude the subnet and broadcast addresses
  int max_num_of_CEs = (num_of_suffixes * num_of_port_sets); //maximum possible number of CEs based on the number of EA-bits

  if (num_of_CEs > max_num_of_CEs){
    std::cerr << "Config Error: The number of CEs ("<< num_of_CEs <<") to be simulated exceeds the maximum number that EA-bits allow (" << max_num_of_CEs << ")" << std::endl;
    return -1;
  }
  
  // pre-generate pseudorandom EA-bits combinations 
  //and save them in a NUMA local memory (of the same memory of the sender core for fast access)
  // For this purpose, we used rte_eal_remote_launch() and pack parameters for it

  // prepare the parameters for the randomPermutationGenerator48
    randomPermutationGeneratorParameters48 pars;
    pars.ip4_suffix_length = bmr_ipv4_suffix_length;
    pars.psid_length = psid_length;
    pars.hz = rte_get_timer_hz(); // number of clock cycles per second;
    
    if (forward)
      {
        pars.direction = "forward"; 
        pars.addr_of_arraypointer = &fwUniqueEAComb;
        // start randomPermutationGenerator32
        if ( rte_eal_remote_launch(randomPermutationGenerator48, &pars, left_sender_cpu ) )
          std::cerr << "Error: could not start randomPermutationGenerator48() for pre-generating unique EA-bits combinations at the " << pars.direction << " sender" << std::endl;
        rte_eal_wait_lcore(left_sender_cpu);
      }
    if (reverse)
      {
        pars.direction = "reverse";
        pars.addr_of_arraypointer = &rvUniqueEAComb;
        // start randomPermutationGenerator32
        if ( rte_eal_remote_launch(randomPermutationGenerator48, &pars, right_sender_cpu ) )
          std::cerr << "Error: could not start randomPermutationGenerator48() for pre-generating unique EA-bits combinations at the " << pars.direction << " sender" << std::endl;
        rte_eal_wait_lcore(right_sender_cpu);
      }

  // pre-generate the array of CEs Data (MAP addresses and others) 
  //and save it in a NUMA local memory (of the same memory of the sender core for fast access)
  // For this purpose, we used rte_eal_remote_launch() and pack parameters for it

  CEArrayBuilderParameters param;
  param.bmr_ipv4_suffix_length = bmr_ipv4_suffix_length; 
  param.psid_length =  psid_length;            
  param.num_of_CEs = num_of_CEs;             
  param.bmr_ipv6_prefix = bmr_ipv6_prefix; 
  param.bmr_ipv6_prefix_length = bmr_ipv6_prefix_length;  
  param.bmr_ipv4_prefix = bmr_ipv4_prefix;        
  param.hz = rte_get_timer_hz(); // number of clock cycles per second			
  
  if (forward)
    {
      param.direction = "forward"; 
      param.UniqueEAComb = fwUniqueEAComb;       
      param.addr_of_arraypointer = &fwCE;
      // start randomPermutationGenerator32
        if ( rte_eal_remote_launch(buildCEArray, &param, left_sender_cpu ) )
          std::cerr << "Error: could not start buildCEArray() for pre-generating the array of CEs data at the " << param.direction << " sender" << std::endl;
        rte_eal_wait_lcore(left_sender_cpu);
      }
  if (reverse)
      {
        param.direction = "reverse";
        param.UniqueEAComb = rvUniqueEAComb;       
        param.addr_of_arraypointer = &rvCE;
        // start randomPermutationGenerator32
        if ( rte_eal_remote_launch(buildCEArray, &param, right_sender_cpu ) )
             std::cerr << "Error: could not start buildCEArray() for pre-generating the array of CEs data at the " << param.direction << " sender" << std::endl;
        rte_eal_wait_lcore(right_sender_cpu);
      }

  // Construct the DMR ipv6 address (It will be the destination address in the forward direction in case of the foreground traffic)
 // Based on section 2.2 of RFC 6052, The possible DMR prefix length are 32, 40, 48, 56, 64, and 96.
 //and bits 64 to 71 of the address are reserved and should be 0 for all prefix cases except 96.
 //When using a /96 Network-Specific Prefix, the administrators MUST ensure that the bits 64 to 71 are set to zero.
 //Please refer to the figure of section 2.2 of RFC 6052 for more information.
 rte_memcpy(dmr_ipv6.s6_addr, dmr_ipv6_prefix.s6_addr, 16);

 int num_octets_before_u = (64-dmr_ipv6_prefix_length)/8;
 int num_octets_after_u = 4 - num_octets_before_u;
 if (num_octets_before_u < 0) // /96 prefix. There are no u bits
  for (int i = 0; i < 4; i++)
    dmr_ipv6.s6_addr[15 - i] = (unsigned char)(ntohl(tester_right_ipv4) >> (i * 8));
 else { // /32, /40, /48, /56, or /64 prefix. There are u bits (64-72)
  for (int i = 0; i < num_octets_before_u; i++)
    dmr_ipv6.s6_addr[7 - i] = (unsigned char)(ntohl(tester_right_ipv4) >> ((i + num_octets_after_u) * 8));
  // dmr_ipv6.s6_addr[8] = u bits = 0
  for (int i = 0; i < num_octets_after_u; i++)
    dmr_ipv6.s6_addr[9 + i] = (unsigned char)(ntohl(tester_right_ipv4) >> (((num_octets_after_u - 1) - i) * 8));
 }

  return 0;
}


struct rte_mbuf *mkTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, uint32_t *dst_ip, unsigned var_sport, unsigned var_dport)
{
  // printf("inside mkTestFrame4: the beginning\n");
  struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if (!pkt_mbuf)
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
  length -= ETHER_CRC_LEN;                                                                                       // exclude CRC from the frame length
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
  mkData(udp_data, data_length);
  udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hd); // UDP checksum is calculated and set
  ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);               // IPv4 header checksum is set now
  return pkt_mbuf;
}

void mkEthHeader(struct rte_ether_hdr *eth, const struct ether_addr *dst_mac, const struct ether_addr *src_mac, const uint16_t ether_type)
{
  rte_memcpy(&eth->dst_addr, dst_mac, sizeof(struct rte_ether_hdr));
  rte_memcpy(&eth->src_addr, src_mac, sizeof(struct rte_ether_hdr));
  eth->ether_type = htons(ether_type);
}

void mkIpv4Header(struct rte_ipv4_hdr *ip, uint16_t length, const uint32_t *src_ip, uint32_t *dst_ip)
{
  ip->version_ihl = 0x45; // Version: 4, IHL: 20/4=5
  ip->type_of_service = 0;
  ip->total_length = htons(length);
  ip->packet_id = 0;
  ip->fragment_offset = 0;
  ip->time_to_live = 0x0A;
  ip->next_proto_id = 0x11; // UDP
  ip->hdr_checksum = 0;
  rte_memcpy(&ip->src_addr, src_ip, 4);
  rte_memcpy(&ip->dst_addr, dst_ip, 4);
  // May NOT be set now, only after the UDP header checksum calculation: ip->hdr_checksum = rte_ipv4_cksum(ip);
}

// creates a UDP header
void mkUdpHeader(struct rte_udp_hdr *udp, uint16_t length, unsigned var_sport, unsigned var_dport)
{
  udp->src_port = htons(var_sport ? 0 : 0xC020); // set to 0 if source port number will change, otherwise RFC 2544 Test Frame format
  udp->dst_port = htons(var_dport ? 0 : 0x0007); // set to 0 if destination port number will change, otherwise RFC 2544 Test Frame format
  udp->dgram_len = htons(length);
  udp->dgram_cksum = 0; // Checksum is set to 0 now.
  // UDP checksum is calculated later.
}

// creates and IPv6 header
void mkIpv6Header(struct rte_ipv6_hdr *ip, uint16_t length, struct in6_addr *src_ip, struct in6_addr *dst_ip)
{
  ip->vtc_flow = htonl(0x60000000); // Version: 6, Traffic class: 0, Flow label: 0
  ip->payload_len = htons(length - sizeof(rte_ipv6_hdr));
  ip->proto = 0x11; // UDP
  ip->hop_limits = 0x0A;
  rte_mov16((uint8_t *)&ip->src_addr, (uint8_t *)src_ip);
  rte_mov16((uint8_t *)&ip->dst_addr, (uint8_t *)dst_ip);
}

// creates an IPv6 Test Frame using several helper functions
struct rte_mbuf *mkIpv4inIpv6Tun(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              struct in6_addr *src_ipv6, struct in6_addr *dst_ipv6, unsigned var_sport, unsigned var_dport, 
                              const uint32_t *src_ipv4, uint32_t *dst_ipv4)
{
  struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if (!pkt_mbuf)
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
  length -= ETHER_CRC_LEN;                                                                                       // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length;                                                               // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *);                                                          // Access the Test Frame in the message buffer
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt);                                                // Ethernet header
  rte_ipv4_hdr *ipv4_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt + sizeof(rte_ether_hdr));                                     // IPv4 header
  rte_ipv6_hdr *ipv6_hdr = reinterpret_cast<rte_ipv6_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr));                                      // IPv6 header
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_ipv6_hdr));                     // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_ipv6_hdr) + sizeof(rte_udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x86DD); // contains an IPv6 packet
  int ipv4_length = length - sizeof(rte_ether_hdr);
  mkIpv4Header(ipv4_hdr, ipv4_length, src_ipv4, dst_ipv4); // Does not set IPv4 header checksum
  int ipv6_length = ipv4_length - sizeof(rte_ipv4_hdr);
  mkIpv6Header(ipv6_hdr, ipv6_length, src_ipv6, dst_ipv6);
  int udp_length = ipv6_length - sizeof(rte_ipv6_hdr); // No IP Options are used
  mkUdpHeader(udp_hd, udp_length, var_sport, var_dport);
  int data_length = udp_length - sizeof(rte_udp_hdr);
  mkData(udp_data, data_length);
  udp_hd->dgram_cksum = rte_ipv6_udptcp_cksum(ipv6_hdr, udp_hd); // UDP checksum is calculated and set
  //Kell az IPv4-re külön checksumot számolni?
  return pkt_mbuf;
}

struct rte_mbuf *mkTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              struct in6_addr *src_ip, struct in6_addr *dst_ip, unsigned var_sport, unsigned var_dport)
{
  struct rte_mbuf *pkt_mbuf = rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if (!pkt_mbuf)
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", direction);
  length -= ETHER_CRC_LEN;                                                                                       // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length;                                                               // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *);                                                          // Access the Test Frame in the message buffer
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt);                                                // Ethernet header
  rte_ipv6_hdr *ip_hdr = reinterpret_cast<rte_ipv6_hdr *>(pkt + sizeof(rte_ether_hdr));                                      // IPv6 header
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr));                     // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr) + sizeof(rte_udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x86DD); // contains an IPv6 packet
  int ip_length = length - sizeof(rte_ether_hdr);
  mkIpv6Header(ip_hdr, ip_length, src_ip, dst_ip);
  int udp_length = ip_length - sizeof(rte_ipv6_hdr); // No IP Options are used
  mkUdpHeader(udp_hd, udp_length, var_sport, var_dport);
  int data_length = udp_length - sizeof(rte_udp_hdr);
  mkData(udp_data, data_length);
  udp_hd->dgram_cksum = rte_ipv6_udptcp_cksum(ip_hdr, udp_hd); // UDP checksum is calculated and set
  return pkt_mbuf;
}

// fills the data field of the Test Frame
void mkData(uint8_t *data, uint16_t length)
{
  unsigned i;
  uint8_t identify[8] = {'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y'}; // Identification of the Test Frames
  uint64_t *id = (uint64_t *)identify;
  *(uint64_t *)data = *id;
  data += 8;
  length -= 8;
  for (i = 0; i < length; i++)
    data[i] = i % 256;
}

