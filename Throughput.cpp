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
  cpu_fw_send = -1;          // MUST be set in the config file if forward != 0
  cpu_rv_receive = -1;       // MUST be set in the config file if forward != 0
  cpu_rv_send = -1;         // MUST be set in the config file if reverse != 0
  cpu_fw_receive = -1;        // MUST be set in the config file if reverse != 0
  memory_channels = 1;           // default value, this value will be set, if not specified in the config file
  fwd_var_sport = 3;             // default value: use pseudorandom change for the source port numbers in the forward direction
  fwd_var_dport = 3;             // default value: use pseudorandom change for the destination port numbers in the forward direction
  rev_var_sport = 3;             // default value: use pseudorandom change for the source port numbers in the reverse direction
  rev_var_dport = 3;             // default value: use pseudorandom change for the destination port numbers in the reverse direction
  fwd_dport_min = 1;             // default value: as recommended by RFC 4814
  fwd_dport_max = 49151;         // default value: as recommended by RFC 4814
  rev_sport_min = 1024;          // default value: as recommended by RFC 4814
  rev_sport_max = 65535;         // default value: as recommended by RFC 4814
  bg_fw_dport_min = 1;              // default value: as recommended by RFC 4814
  bg_fw_dport_max = 49151;          // default value: as recommended by RFC 4814
  bg_rv_sport_min = 1024;           // default value: as recommended by RFC 4814
  bg_rv_sport_max = 65535;          // default value: as recommended by RFC 4814
  
  

  // some other variables
  //dmr_ipv6 = IN6ADDR_ANY_INIT;  
  //fwUniqueEAComb = NULL;         
  //rvUniqueEAComb = NULL;                                    
  //fwCE = NULL;                  
  //rvCE = NULL;                  
};

// reports the TSC of the core (in the variable pointed by the input parameter), on which it is running
int report_tsc(void *par)
{
  *(uint64_t *)par = rte_rdtsc();
  return 0;
}

// checks if the TSC of the given lcore is synchronized with that of the main core
// Note that TSCs of different pysical CPUs may be different, which would prevent siitperf from working correctly!
void check_tsc(int cpu, const char *cpu_name) {
  uint64_t tsc_before, tsc_reported, tsc_after;

  tsc_before = rte_rdtsc();
  if ( rte_eal_remote_launch(report_tsc, &tsc_reported, cpu) )
    rte_exit(EXIT_FAILURE, "Error: could not start TSC checker on core #%i for %s!\n", cpu, cpu_name);
  rte_eal_wait_lcore(cpu);
  tsc_after = rte_rdtsc();
  if ( tsc_reported < tsc_before || tsc_reported > tsc_after )
    rte_exit(EXIT_FAILURE, "Error: TSC of core #%i for %s is not synchronized with that of the main core!\n", cpu, cpu_name);
}

class randomPermutationGeneratorParameters48 {
  public:
    uint64_t hz;
    const char *direction;
    uint8_t ip4_suffix_length;
    uint8_t psid_length;
};


// finds a 'key' (name of a parameter) in the 'line' string
// '#' means comment, leading spaces and tabs are skipped
// return: the starting position of the key, if found; -1 otherwise
int Throughput::findKey(const char *line, const char *key) {
  int line_len, key_len; // the lenght of the line and of the key
  int pos; // current position in the line

  line_len=strlen(line);
  key_len=strlen(key);
  for ( pos=0; pos<line_len-key_len; pos++ ) {
    if ( line[pos] == '#' ) // comment
      return -1;
    if ( line[pos] == ' ' || line[pos] == '\t' )
      continue;
    if ( strncmp(line+pos,key,key_len) == 0 )
      return pos+strlen(key);
  }
  return -1;
}

// skips leading spaces and tabs, and cuts off tail starting by a space, tab or new line character
// it is needed, because inet_pton cannot read if there is e.g. a trailing '\n'
// WARNING: the input buffer is changed!
char *prune(char *s) {
  int len, i;
 
  // skip leading spaces and tabs 
  while ( *s==' ' || *s=='\t' )
    s++;

  // trim string, if space, tab or new line occurs
  len=strlen(s);
  for ( i=0; i<len; i++ )
    if ( s[i]==' ' || s[i]=='\t' || s[i]=='\n' ) {
      s[i]=(char)0;
      break;
    }
  return s;
}

// checks if there is some non comment information in the line
int nonComment(const char *line) {
  int i;

  for ( i=0; i<LINELEN; i++ ) {
    if ( line[i]=='#' || line[i]=='\n' )
      return 0; // line is comment or empty 
    else if ( line[i]==' ' || line[i]=='\t' )
      continue; // skip space or tab, see next char
    else
      return 1; // there is some other character
  }
  // below code should be unreachable
  return 1;
}

int Throughput::readConfigFile(const char *filename) {
  FILE *f; 	// file descriptor
  char line[LINELEN+1]; // buffer for reading a line of the input file
  int pos; 	// position in the line after the key (parameter name) was found
  uint8_t *m; 	// pointer to the MAC address being read
  int line_no;	// line number for error message

  f=fopen(filename,"r");
  if ( f == NULL ) {
    std::cerr << "Input Error: Can't open file '" << filename << "'." << std::endl;
    return -1;
  }
  
  for ( line_no=1; fgets(line, LINELEN+1, f); line_no++ ) {
    if ( (pos = findKey(line, "Tester-BG-Send-IPv6")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&tester_bg_send_ipv6)) != 1 ) {
        std::cerr << "Input Error: Bad 'Tester-BG-Send-IPv6' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Tester-BG-Receive-IPv6")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&tester_bg_rec_ipv6)) != 1 ) {
        std::cerr << "Input Error: Bad 'Tester-BG-Receive-IPv6' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Tester-FW-Receive-IPv4")) >= 0 ) {
      if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&tester_fw_rec_ipv4)) != 1 ) {
        std::cerr << "Input Error: Bad 'Tester-FW-Receive-IPv4' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Tester-FW-Send-IPv6")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&tester_fw_send_ipv6)) != 1 ) {
         std::cerr << "Input Error: Bad 'Tester-FW-Send-IPv6' address." << std::endl;
        return -1;
      } 
    } else if ( (pos = findKey(line, "TESTER-FW-MAC")) >= 0 ) {
      m=tester_fw_mac;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'TESTER-FW-MAC' address." << std::endl;
        return -1;
      } 
    } else if ( (pos = findKey(line, "TESTER-RV-MAC")) >= 0 ) {
      m=tester_rv_mac;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'TESTER-RV-MAC' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "DUT-FW-MAC")) >= 0 ) {
      m=dut_fw_mac;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'DUT-FW-MAC' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "DUT-RV-MAC")) >= 0 ) {
      m=dut_rv_mac;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'DUT-RV-MAC' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Forward")) >= 0 ) {
      sscanf(line+pos, "%d", &forward);
      if (!(forward == 0 || forward == 1))
      {
        std::cerr << "Input Error: 'Forward' must be either 0 for inactive or 1 for active." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Reverse")) >= 0 ) {
      sscanf(line+pos, "%d", &reverse);
      if (!(reverse == 0 || reverse == 1))
      {
        std::cerr << "Input Error: 'Reverse' must be either 0 for inactive or 1 for active." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Promisc")) >= 0 ) {
      sscanf(line+pos, "%d", &promisc);
      if (!(promisc == 0 || promisc == 1))
      {
        std::cerr << "Input Error: 'Promisc' must be either 0 for inactive or 1 for active." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-FW-Send")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_fw_send);
      if ( cpu_fw_send < 0 || cpu_fw_send >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-FW-Send' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-RV-Receive")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_rv_receive);
      if ( cpu_rv_receive < 0 || cpu_rv_receive >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-RV-Receive' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-RV-Send")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_rv_send);
      if ( cpu_rv_send < 0 || cpu_rv_send >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-RV-Send' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-FW-Receive")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_fw_receive);
      if ( cpu_fw_receive < 0 || cpu_fw_receive >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-FW-Receive' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "MEM-Channels")) >= 0 ) {
      sscanf(line+pos, "%hhu", &memory_channels);
      if ( memory_channels <= 0 ) {
        std::cerr << "Input Error: 'MEM-Channels' must be > 0." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "FW-var-sport")) >= 0 ) {
      sscanf(line+pos, "%u", &fwd_var_sport);
      if ( fwd_var_sport > 3 ) {
        std::cerr << "Input Error: 'FW-var-sport' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "FW-var-dport")) >= 0 ) {
      sscanf(line+pos, "%u", &fwd_var_dport);
      if ( fwd_var_dport > 3 ) {
        std::cerr << "Input Error: 'FW-var-dport' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "RV-var-sport")) >= 0 ) {
      sscanf(line+pos, "%u", &rev_var_sport);
      if ( rev_var_sport > 3 ) {
        std::cerr << "Input Error: 'RV-var-sport' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "RV-var-dport")) >= 0 ) {
      sscanf(line+pos, "%u", &rev_var_dport);
      if ( rev_var_dport > 3 ) {
        std::cerr << "Input Error: 'RV-var-dport' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "FW-dport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &rev_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'FW-dport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "FW-dport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &rev_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'FW-dport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "RV-sport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &rev_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'RV-sport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "RV-sport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &rev_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'RV-sport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "bg-FW-dport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &bg_fw_dport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'bg-FW-dport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "bg-FW-dport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &bg_fw_dport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'bg-FW-dport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "bg-RV-sport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &bg_rv_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'bg-RV-sport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "bg-RV-sport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &bg_rv_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'bg-RV-sport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "PSID_length")) >= 0 ) {
      sscanf(line+pos, "%u", &psid_length);
      if ( psid_length < 1 || psid_length > 10 ) {
        std::cerr << "Input Error: 'PSID_length' must be >= 1 and <= 10." << std::endl;
        return -1;
      }
    } else if ((pos = findKey(line, "NUM-OF-lwB4s")) >= 0){
      sscanf(line + pos, "%u", &number_of_lwB4s);
      if (number_of_lwB4s < 1 || number_of_lwB4s > 1000000)
      {
        std::cerr << "Input Error: 'NUM-OF-lwB4s' must be >= 1 and <= 1000000." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "DUT-FW-IPv6")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&dut_fw_ipv6)) != 1 ) {
         std::cerr << "Input Error: Bad 'DUT-FW-IPv6' address." << std::endl;
        return -1;
      } 
    } else if ( (pos = findKey(line, "DUT-Tunnel-IPv6")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&dut_ipv6_tunnel)) != 1 ) {
         std::cerr << "Input Error: Bad 'DUT-Tunnel-IPv6' address." << std::endl;
        return -1;
      } 
    } else if ( (pos = findKey(line, "LWB4-start-IPv4")) >= 0 ) {
      if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&lwb4_start_ipv4)) != 1 ) {
        std::cerr << "Input Error: Bad 'LWB4-start-IPv4' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "LWB4-end-IPv4")) >= 0 ) {
      if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&lwb4_end_ipv4)) != 1 ) {
        std::cerr << "Input Error: Bad 'LWB4-end-IPv4' address." << std::endl;
        return -1;
      }
    } /*else if ( (pos = findKey(line, "IP-R-min")) >= 0 ) {
      if ( sscanf(line+pos, "%i", &ip_right_min) < 1 ) { // read decimal or hexa (in 0x... format)
        std::cerr << "Input Error: Unable to read 'IP-R-min' value." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IP-R-max")) >= 0 ) {
      if ( sscanf(line+pos, "%i", &ip_right_max) < 1 ) { // read decimal or hexa (in 0x... format)
        std::cerr << "Input Error: Unable to read 'IP-R-max' value." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv4-L-offset")) >= 0 ) {
      sscanf(line+pos, "%u", &ipv4_left_offset);
      if ( ipv4_left_offset < 1 || ipv4_left_offset > 2 ) {
        std::cerr << "Input Error: 'IPv4-L-offset' must be 1 or 2." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-L-offset")) >= 0 ) {
      sscanf(line+pos, "%u", &ipv6_left_offset);
      if ( ipv6_left_offset < 6 || ipv6_left_offset > 14 ) {
        std::cerr << "Input Error: 'IPv6-L-offset' must be in the [6, 14] interval." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv4-R-offset")) >= 0 ) {
      sscanf(line+pos, "%u", &ipv4_right_offset);
      if ( ipv4_right_offset < 1 || ipv4_right_offset > 2 ) {
        std::cerr << "Input Error: 'IPv4-R-offset' must be 1 or 2." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-R-offset")) >= 0 ) {
      sscanf(line+pos, "%u", &ipv6_right_offset);
      if ( ipv6_right_offset < 6 || ipv6_right_offset > 14 ) {
        std::cerr << "Input Error: 'IPv6-R-offset' must be in the [6, 14] interval." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Stateful")) >= 0 ) {
      sscanf(line+pos, "%u", &stateful);
      if ( stateful > 2 ) {
        std::cerr << "Input Error: 'Stateful' must be 0, 1, or 2." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Responder-tuples")) >= 0 ) {
      sscanf(line+pos, "%u", &responder_tuples);
      if ( responder_tuples > 3 ) {
        std::cerr << "Input Error: 'Responder-tuples' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Enumerate-ports")) >= 0 ) {
      sscanf(line+pos, "%u", &enumerate_ports);
      if ( enumerate_ports > 3 ) {
        std::cerr << "Input Error: 'Enumerate-ports' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Enumerate-ips")) >= 0 ) {
      sscanf(line+pos, "%u", &enumerate_ips);
      if ( enumerate_ips > 3 ) {
        std::cerr << "Input Error: 'Enumerate-ips' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    }*/else if ( nonComment(line) ) { // It may be too strict!
        std::cerr << "Input Error: Cannot interpret '" << filename << "' line " << line_no << ":" << std::endl;
        std::cerr << line << std::endl;
        return -1;
    } 
  }
  fclose(f);
  //std::cout << tester_fw_rec_ipv4 << std::endl;
  //std::cout << aftr_ipv6_tunnel << std::endl;
  //std::cout << unsigned(lwb4_start_ipv4) << std::endl;
  //std::cout << unsigned(psid_length) << std::endl;
  //std::cout << cpu_fw_send << std::endl;

  // check if at least one direction is active (compulsory for stateless tests)
  if ( forward == 0 && reverse == 0 ) {
    std::cerr << "Input Error: No active direction was specified." << std::endl;
    return -1;
  } 
  
  // check if the necessary lcores were specified
  if ( forward ) {
    if ( cpu_fw_send < 0 ) {
      std::cerr << "Input Error: No 'CPU-FW-Send' was specified." << std::endl;
      return -1;
    }
    if ( cpu_rv_receive < 0 ) {
      std::cerr << "Input Error: No 'CPU-RV-Receive' was specified." << std::endl;
      return -1;
    }
  }
  if ( reverse ) {
    if ( cpu_rv_send < 0 ) {
      std::cerr << "Input Error: No 'CPU-RV-Send' was specified." << std::endl;
      return -1;
    }
    if ( cpu_fw_receive < 0 ) {
      std::cerr << "Input Error: No 'CPU-FW-Receive' was specified." << std::endl;
      return -1;
    }
  }

  /*
  // calculate the derived values, if any port numbers or IP addresses have to be changed
  fwd_varport = fwd_var_sport || fwd_var_dport;
  rev_varport = rev_var_sport || rev_var_dport;
  ip_varies = ip_left_varies || ip_right_varies;

  // sanity checks regarding IP address and port number eumeration
  switch ( stateful ) {
    case 0: // stateless tests
      if ( enumerate_ports ) {
        std::cerr << "Input Error: Port number enumeration is available with stateful tests only." << std::endl;
        return -1;
      }
      if ( enumerate_ips ) {
        std::cerr << "Input Error: IP address enumeration is available with stateful tests only." << std::endl;
        return -1;
      }
      break;
    case 1: // Initiator is on the left side
      if ( enumerate_ports && num_right_nets > 1 ) {
        std::cerr << "Input Error: Port enumeration is available with a single destination network only." << std::endl;
        return -1;
      }
      break;
    case 2: // Initiator is on the right side
      if ( enumerate_ports && num_left_nets > 1 ) {
        std::cerr << "Input Error: Port enumeration is available with a single destination network only." << std::endl;
        return -1;
      }
      break;
  }
  // sanity checks regarding multiple IP addresses and multiple destination networks
  if ( ip_varies && ( num_left_nets > 1 || num_right_nets > 1 ) ) {
    std::cerr << "Input Error: Usage of multiple IP address is available with a single destination network only." << std::endl;
    return -1;
  }

  // checking the constraints for "Enumerate-ips" and "Enumerate-ports"
  if ( stateful && enumerate_ips && enumerate_ports && enumerate_ips != enumerate_ports ) {
    std::cerr << "Input Error: In stateful tests, if both 'Enumerate-ips' and 'Enumerate-ports' are non-zero then they MUST be equal." << std::endl; 
    return -1;
  }

  // forcing the restriction that stateful tests with port number enumeration and multiple IP addresses MUST use IP address enumeration, too.
  if ( stateful && enumerate_ports && ip_varies && !enumerate_ips ) {
    std::cerr << "Input Error: In stateful tests, if port number enumeration and multiple IP addresses are used then IP address enumeration MUST be used, too." << std::endl;
    return -1;
  }

  // forcing the restriction that stateful tests with IP address enumeration and multiple port numbers MUST use port number enumeration, too.
  if ( stateful && enumerate_ips && (fwd_varport||rev_varport) && !enumerate_ports ) {
    std::cerr << "Input Error: In stateful tests, if IP addresses enumeration and multiple port numbers are used then port number enumeration MUST be used." << std::endl; 
    return -1;
  }

  // forcing the restriction that with stateful tests, if Enumerate-ips is non-zero then IP-L-var and IP-R-var also must be non-zero.
  if ( stateful && enumerate_ips && ( !ip_left_varies || !ip_right_varies ) ) {
    std::cerr << "Input Error: In stateful tests, Enumerate-ips is non-zero then IP-L-var and IP-R-var also must be non-zero."  << std::endl;
    return -1;
  }

  // perform masking of the proper 16 bits of the IPv4 / IPv6 addresses
  if ( ip_left_varies ) {
    uint32_t ipv4mask = htonl(0xffffffff & ~(0xffffu<<((2-ipv4_left_offset)*8)));
    ipv4_left_real &= ipv4mask;
    ipv4_left_virtual &= ipv4mask;
    ipv6_left_real.s6_addr[ipv6_left_offset]=0;
    ipv6_left_real.s6_addr[ipv6_left_offset+1]=0;
    ipv6_left_virtual.s6_addr[ipv6_left_offset]=0;
    ipv6_left_virtual.s6_addr[ipv6_left_offset+1]=0;
  }
  if ( ip_right_varies ) {
    uint32_t ipv4mask = htonl(0xffffffff & ~(0xffffu<<((2-ipv4_left_offset)*8)));
    ipv4_right_real &= ipv4mask;
    ipv4_right_virtual &= ipv4mask;
    ipv6_right_real.s6_addr[ipv6_right_offset]=0;
    ipv6_right_real.s6_addr[ipv6_right_offset+1]=0;
    ipv6_right_virtual.s6_addr[ipv6_right_offset]=0;
    ipv6_right_virtual.s6_addr[ipv6_right_offset+1]=0;
  }
  */
  return 0;
}

// reads the command line arguments and stores the information in data members of class Throughput
// It may be called only AFTER the execution of readConfigFile
int Throughput::readCmdLine(int argc, const char *argv[])
{
  if (argc < 7)
  {
    printf("argc : %d\n", argc);
    std::cerr << "Input Error: Too few command line arguments." << std::endl;
    return -1;
  }
  if (sscanf(argv[1], "%hu", &ipv6_frame_size) != 1 || ipv6_frame_size < 84 || ipv6_frame_size > 1538)
  {
    std::cerr << "Input Error: IPv6 frame size must be between 84 and 1538." << std::endl;
    return -1;
  }
  // Further checking of the frame size will be done, when n and m are read.
  ipv4_frame_size = ipv6_frame_size - 20;
  if (sscanf(argv[2], "%u", &frame_rate) != 1 || frame_rate < 1 || frame_rate > 14880952)
  {
    // 14,880,952 is the maximum frame rate for 10Gbps Ethernet using 64-byte frame size
    std::cerr << "Input Error: Frame rate must be between 1 and 14880952." << std::endl;
    return -1;
  }
  if (sscanf(argv[3], "%hu", &test_duration) != 1 || test_duration < 1 || test_duration > 3600)
  {
    std::cerr << "Input Error: Test duration must be between 1 and 3600." << std::endl;
    return -1;
  }
  if (sscanf(argv[4], "%hu", &stream_timeout) != 1 || stream_timeout > 60000)
  {
    std::cerr << "Input Error: Stream timeout must be between 0 and 60000." << std::endl;
    return -1;
  }
  if (sscanf(argv[5], "%u", &n) != 1 || n < 2)
  {
    std::cerr << "Input Error: The value of 'n' must be at least 2." << std::endl;
    return -1;
  }
  if (sscanf(argv[6], "%u", &m) != 1)
  {
    std::cerr << "Input Error: Cannot read the value of 'm'." << std::endl;
    return -1;
  }

  return 0;
}

int Throughput::init(const char *argv0, uint16_t leftport, uint16_t rightport)
{
  std::cout << "INIT STARTED" << std::endl;
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
    snprintf(coresList, 101, "0,%d,%d,%d,%d", cpu_fw_send, cpu_rv_receive,  cpu_rv_send, cpu_fw_receive);
  }
  else if (forward)
    snprintf(coresList, 101, "0,%d,%d", cpu_fw_send, cpu_rv_receive); // only forward (left to right) is active
  else
    snprintf(coresList, 101, "0,%d,%d",  cpu_rv_send, cpu_fw_receive); // only reverse (right to left) is active
  std::cout << "HA forward és reverse KÉSZ" << std::endl;
  rte_argv[2] = coresList;
  std::cout << "CoreList BEÁLLÍTVA" << std::endl;
  rte_argv[3] = "-n";
  snprintf(numChannels, 11, "%hhu", memory_channels);
  rte_argv[4] = numChannels;
  rte_argv[5] = 0;
  std::cout << "RTE INIT KEZD" << std::endl;

  std::cout << coresList << std::endl;
  if (rte_eal_init(rte_argc, const_cast<char **>(rte_argv)) < 0)
  {
    std::cerr << "Error: DPDK RTE initialization failed, Tester exits." << std::endl;
    return -1;
  }
  std::cout << "RTE INIT SIKER" << std::endl;

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

  pkt_pool_left_sender = rte_pktmbuf_pool_create("pp_left_sender", left_sender_pool_size, PKTPOOL_CACHE, 0,
                                                 RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_fw_send));
  if (!pkt_pool_left_sender)
  {
    std::cerr << "Error: Cannot create packet pool for Left Sender, Tester exits." << std::endl;
    return -1;
  }
  pkt_pool_right_receiver = rte_pktmbuf_pool_create("pp_right_receiver", receiver_pool_size, PKTPOOL_CACHE, 0,
                                                    RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_rv_receive));
  if (!pkt_pool_right_receiver)
  {
    std::cerr << "Error: Cannot create packet pool for Right Receiver, Tester exits." << std::endl;
    return -1;
  }

  pkt_pool_right_sender = rte_pktmbuf_pool_create("pp_right_sender", right_sender_pool_size, PKTPOOL_CACHE, 0,
                                                  RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id( cpu_rv_send));
  if (!pkt_pool_right_sender)
  {
    std::cerr << "Error: Cannot create packet pool for Right Sender, Tester exits." << std::endl;
    return -1;
  }
  pkt_pool_left_receiver = rte_pktmbuf_pool_create("pp_left_receiver", receiver_pool_size, PKTPOOL_CACHE, 0,
                                                   RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_fw_receive));
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
        numaCheck(leftport, "Left", cpu_fw_send, "Left Sender");
        numaCheck(rightport, "Right", cpu_rv_receive, "Right Receiver");
      }
      if (reverse)
      {
        numaCheck(rightport, "Right",  cpu_rv_send, "Right Sender");
        numaCheck(leftport, "Left", cpu_fw_receive, "Left Receiver");
      }
    }
  }

  // Some sanity checks: TSCs of the used cores are synchronized or not...
  if (forward)
  {
    check_tsc(cpu_fw_send, "Left Sender");
    check_tsc(cpu_rv_receive, "Right Receiver");
  }
  if (reverse)
  {
    check_tsc( cpu_rv_send, "Right Sender");
    check_tsc(cpu_fw_receive, "Left Receiver");
  }

  // prepare further values for testing
  hz = rte_get_timer_hz();                                                       // number of clock cycles per second
  start_tsc = rte_rdtsc() + hz * START_DELAY / 1000;                             // Each active sender starts sending at this time
  finish_receiving = start_tsc + hz * (test_duration + stream_timeout / 1000.0); // Each receiver stops at this time

  /**
  num_of_port_sets = pow(2.0, PSID_length);
  int num_of_ports_in_one_port_set = (int)(65536.0 / num_of_port_sets);
  


  randomPermutationGeneratorParameters48 pars;
  pars.ip4_suffix_length = bmr_ipv4_suffix_length;
  pars.psid_length = psid_length;
  pars.hz = rte_get_timer_hz(); // number of clock cycles per second;
    
  if (forward)
    {
      pars.direction = "forward"; 
      pars.addr_of_arraypointer = &fwUniqueEAComb;
      // start randomPermutationGenerator32
      if ( rte_eal_remote_launch(randomPermutationGenerator48, &pars, cpu_fw_send ) )
        std::cerr << "Error: could not start randomPermutationGenerator48() for pre-generating unique EA-bits combinations at the " << pars.direction << " sender" << std::endl;
       rte_eal_wait_lcore(cpu_fw_send);
    }
  if (reverse)
    {
      pars.direction = "reverse";
      pars.addr_of_arraypointer = &rvUniqueEAComb;
      // start randomPermutationGenerator32
      if ( rte_eal_remote_launch(randomPermutationGenerator48, &pars,  cpu_rv_send ) )
        std::cerr << "Error: could not start randomPermutationGenerator48() for pre-generating unique EA-bits combinations at the " << pars.direction << " sender" << std::endl;
      rte_eal_wait_lcore( cpu_rv_send);
    }
*/
  std::cout << "INIT lefutott" << std::endl;
  return 0;
}


//checks NUMA localty: is the NUMA node of network port and CPU the same?
void Throughput::numaCheck(uint16_t port, const char *port_side, int cpu, const char *cpu_name) {
  int n_port, n_cpu;
  n_port = rte_eth_dev_socket_id(port);
  n_cpu = numa_node_of_cpu(cpu);
  if ( n_port == n_cpu )
    std::cout << "Info: " << port_side << " port and " << cpu_name << " CPU core belong to the same NUMA node: " << n_port << std::endl;
  else
    std::cout << "Warning: " << port_side << " port and " << cpu_name << " CPU core belong to NUMA nodes " <<
      n_port << ", " << n_cpu << ", respectively." << std::endl; 
}


struct rte_mbuf *mkTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, uint32_t *dst_ip, unsigned var_sport, unsigned var_dport)
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
  length -= RTE_ETHER_CRC_LEN;                                                                                       // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length;                                                               // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *);                                                          // Access the Test Frame in the message buffer
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt);                                                // Ethernet header
  rte_ipv6_hdr *ipv6_hdr = reinterpret_cast<rte_ipv6_hdr *>(pkt + sizeof(rte_ether_hdr));                        // IPv6 header
  rte_ipv4_hdr *ipv4_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr));    // IPv4 header                                      
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv6_hdr) + sizeof(rte_ipv4_hdr));                     // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t *>(pkt + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_ipv6_hdr) + sizeof(rte_udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x86DD); // contains an IPv6 packet
  int ipv6_length = length - sizeof(rte_ether_hdr);
  mkIpv6Header(ipv6_hdr, ipv6_length, src_ipv6, dst_ipv6);
  int ipv4_length = ipv6_length - sizeof(rte_ipv6_hdr);
  mkIpv4Header(ipv4_hdr, ipv4_length, src_ipv4, dst_ipv4); // Does not set IPv4 header checksum
  int udp_length = ipv4_length - sizeof(rte_ipv4_hdr); // No IP Options are used
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
  length -= RTE_ETHER_CRC_LEN;                                                                                       // exclude CRC from the frame length
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

// calculates sender pool size, it is a virtual member function, redefined in derived classes
int Throughput::senderPoolSize()
{
  return 2 * N + PORT_TX_QUEUE_SIZE + 100; // 2*: fg. and bg. Test Frames
  // if varport then everything exists in N copies, see the definition of N
}

// performs throughput (or frame loss rate) measurement
void Throughput::measure(uint16_t leftport, uint16_t rightport) {
  time_t now; // needed for printing out a timestamp in Info message

  // Several parameters are provided to the various sender functions (send, isend, msend, imsend) 
  // and receiver functions (receive, ) in the following 'struct'-s.
  // They are declared here so that they will not be overwritten in the stack when the program leaves an 'if' block.
  /*senderCommonParameters scp1, scp2;
  senderParameters spars1, spars2; 
  iSenderParameters ispars;
  mSenderParameters mspars1, mspars2;
  imSenderParameters imspars;
  receiverParameters rpars1, rpars2;
  rReceiverParameters rrpars1, rrpars2;
  rSenderParameters rspars;

  switch ( stateful ) {
    case 0:	// stateless test is to be performed
      {

      // set common parameters for senders
      scp1=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc);

      if ( forward ) {	// Left to Right direction is active
        // set individual parameters for the left sender
  
        // collect the appropriate values dependig on the IP versions 
        ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
                   &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);

        if ( !ip_varies ) { // use traditional single source and destination IP addresses
     
          // initialize the parameter class instance
          spars1=senderParameters(&scp1,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                  ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                  fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max);
          // start left sender
          if ( rte_eal_remote_launch(send, &spars1, cpu_left_sender) )
            std::cout << "Error: could not start Left Sender." << std::endl;

        } else { // use multiple source and/or destination IP addresses

          // initialize the parameter class instance
          mspars1=mSenderParameters(&scp1,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                    ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,
                                    ip_left_varies,ip_right_varies,ip_left_min,ip_left_max,ip_right_min,ip_right_max,
                                    ipv4_left_offset,ipv4_right_offset,ipv6_left_offset,ipv6_right_offset,
                                    fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max);

          // start left sender
          if ( rte_eal_remote_launch(msend, &mspars1, cpu_left_sender) )
            std::cout << "Error: could not start Left Sender." << std::endl;
        }

        // set parameters for the right receiver
        rpars1=receiverParameters(finish_receiving,rightport,"Forward");
    
        // start right receiver
        if ( rte_eal_remote_launch(receive, &rpars1, cpu_right_receiver) )
          std::cout << "Error: could not start Right Receiver." << std::endl;
      }
    
      if ( reverse ) {	// Right to Left direction is active 
        // set individual parameters for the right sender
    
        // collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
                   &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);

        if ( !ip_varies ) { // use traditional single source and destination IP addresses
    
          // initialize the parameter class instance
          spars2=senderParameters(&scp1,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                  ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                  rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max);
          // start right sender
          if (rte_eal_remote_launch(send, &spars2, cpu_right_sender) )
            std::cout << "Error: could not start Right Sender." << std::endl;
    
        } else { // use multiple source and/or destination IP addresses

          // initialize the parameter class instance
          mspars2=mSenderParameters(&scp1,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                    ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,
                                    ip_right_varies,ip_left_varies,ip_right_min,ip_right_max,ip_left_min,ip_left_max,
                                    ipv4_right_offset,ipv4_left_offset,ipv6_right_offset,ipv6_left_offset,
                                    rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max);

          // start right sender
          if (rte_eal_remote_launch(msend, &mspars2, cpu_right_sender) )
            std::cout << "Error: could not start Right Sender." << std::endl;

        }

        // set parameters for the left receiver
        rpars2=receiverParameters(finish_receiving,leftport,"Reverse");

        // start left receiver
        if ( rte_eal_remote_launch(receive, &rpars2, cpu_left_receiver) )
          std::cout << "Error: could not start Left Receiver." << std::endl;

      }
    
      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Testing initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;
    
      // wait until active senders and receivers finish 
      if ( forward ) {
        rte_eal_wait_lcore(cpu_left_sender);
        rte_eal_wait_lcore(cpu_right_receiver);
      }
      if ( reverse ) {
        rte_eal_wait_lcore(cpu_right_sender);
        rte_eal_wait_lcore(cpu_left_receiver);
      }
      std::cout << "Info: Test finished." << std::endl;
      break;
      }
    case 1:	// stateful test: Initiator is on the left side, Responder is on the right side
      { 
      // set "common" parameters (currently not common with anyone, only code is reused; it will be common, when sending test frames)
      scp1=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,pre_rate,0,n,m,hz,start_tsc_pre); // 0: duration in seconds is not applicable

      // set "individual" parameters for the sender of the Initiator residing on the left side
  
      // collect the appropriate values dependig on the IP versions 
      ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
                 &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);

      if ( !ip_varies  ) { // use traditional single source and destination IP addresses
  
        // initialize the parameter class instance for premiminary phase
        ispars=iSenderParameters(&scp1,ip_left_version,pkt_pool_left_sender,leftport,"Preliminary",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                 ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                 fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,
  			         enumerate_ports,pre_frames,uniquePortComb);
                                
        // start left sender
        if ( rte_eal_remote_launch(isend, &ispars, cpu_left_sender) )
          std::cout << "Error: could not start Initiator's Sender." << std::endl;

      } else { // use multiple source and/or destination IP addresses (because ip_varies OR enumerate_ips)

        // initialize the parameter class instance for premiminary phase
        imspars=imSenderParameters(&scp1,ip_left_version,pkt_pool_left_sender,leftport,"Preliminary",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                   ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,
                                   ip_left_varies,ip_right_varies,ip_left_min,ip_left_max,ip_right_min,ip_right_max,
                                   ipv4_left_offset,ipv4_right_offset,ipv6_left_offset,ipv6_right_offset,
                                   fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,
				   enumerate_ips,enumerate_ports,pre_frames,uniqueIpComb,uniqueFtComb);

          // start left sender
          if ( rte_eal_remote_launch(imsend, &imspars, cpu_left_sender) )
            std::cout << "Error: could not start Initiator's Sender." << std::endl;
      } 
  
      // set parameters for the right receiver
      rrpars1=rReceiverParameters(finish_receiving_pre,rightport,"Preliminary",state_table_size,&valid_entries,&stateTable); 
  
      // start right receiver
      if ( rte_eal_remote_launch(rreceive, &rrpars1, cpu_right_receiver) )
        std::cout << "Error: could not start Responder's Receiver." << std::endl;
 
      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Preliminary frame sending initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;
    
      // wait until active senders and receivers finish 
      rte_eal_wait_lcore(cpu_left_sender);
      rte_eal_wait_lcore(cpu_right_receiver);

      if ( valid_entries < state_table_size )
        rte_exit(EXIT_FAILURE, "Error: Failed to fill state table (valid entries: %u, state table size: %u)!\n", valid_entries, state_table_size);
      else
      	std::cout << "Info: Preliminary phase finished." << std::endl;

      // Now the real test may follow.

      // set "common" parameters 
      scp2=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc); 
  
      if ( forward ) {  // Left to right direction is active

        if ( !ip_varies ) { // use traditional single source and destination IP addresses

          // set "individual" parameters for the (normal) sender of the Initiator residing on the left side
    
          // initialize the parameter class instance for real test (reuse previously prepared 'ipq')
          spars2=senderParameters(&scp2,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                  ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                  fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max);
    
          // start left sender
          if ( rte_eal_remote_launch(send, &spars2, cpu_left_sender) )
            std::cout << "Error: could not start Initiator's Sender." << std::endl;

        } else { // use multiple source and/or destination IP addresses
 
          // initialize the parameter class instance for real test (reuse previously prepared 'ipq')
          mspars2=mSenderParameters(&scp2,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                   ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,
                                   ip_left_varies,ip_right_varies,ip_left_min,ip_left_max,ip_right_min,ip_right_max,
                                   ipv4_left_offset,ipv4_right_offset,ipv6_left_offset,ipv6_right_offset,
                                   fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max);

          // start left sender
          if ( rte_eal_remote_launch(msend, &mspars2, cpu_left_sender) )
            std::cout << "Error: could not start Left Sender." << std::endl;
        }
  
        // set parameters for the right receiver
        rrpars2=rReceiverParameters(finish_receiving,rightport,"Forward",state_table_size,&valid_entries,&stateTable);
  
        // start right receiver
        if ( rte_eal_remote_launch(rreceive, &rrpars2, cpu_right_receiver) )
          std::cout << "Error: could not start Responder's Receiver." << std::endl;
      }

      if ( reverse ) {  // Right to Left direction is active
        // set individual parameters for the right sender

        // first, collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
                   &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);

        // then, initialize the parameter class instance
        rspars=rSenderParameters(&scp2,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                 ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                 rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,
				 state_table_size,stateTable,responder_tuples);

        // start right sender
        if (rte_eal_remote_launch(rsend, &rspars, cpu_right_sender) )
          std::cout << "Error: could not start Right Sender." << std::endl;

        // set parameters for the left receiver
        rpars2=receiverParameters(finish_receiving,leftport,"Reverse");

        // start left receiver
        if ( rte_eal_remote_launch(receive, &rpars2, cpu_left_receiver) )
          std::cout << "Error: could not start Left Receiver." << std::endl;
      }

      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Testing initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;

      // wait until active senders and receivers finish
      if ( forward ) {
        rte_eal_wait_lcore(cpu_left_sender);
        rte_eal_wait_lcore(cpu_right_receiver);
      }
      if ( reverse ) {
        rte_eal_wait_lcore(cpu_right_sender);
        rte_eal_wait_lcore(cpu_left_receiver);
      }
      std::cout << "Info: Test finished." << std::endl;
      break;
      }
    case 2:	// stateful test: Initiator is on the right side, Responder is on the left side
      { 
      // set "common" parameters (currently not common with anyone, only code is reused; it will be common, when sending test frames)
      scp1=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,pre_rate,0,n,m,hz,start_tsc_pre); // 0: duration in seconds is not applicable

      // set "individual" parameters for the sender of the Initiator residing on the right side

      // collect the appropriate values dependig on the IP versions
      ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
                 &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);

      if ( !ip_varies ) { // use traditional single source and destination IP addresses
  
        // initialize the parameter class instance for preliminary phase
        ispars=iSenderParameters(&scp1,ip_right_version,pkt_pool_right_sender,rightport,"Preliminary",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                 ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                 rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,
  			         enumerate_ports,pre_frames,uniquePortComb);
  
        // start right sender
        if ( rte_eal_remote_launch(isend, &ispars, cpu_right_sender) )
          std::cout << "Error: could not Initiator's Sender." << std::endl;
  
      } else { // use multiple source and/or destination IP addresses (because ip_varies OR enumerate_ips)

        // initialize the parameter class instance for preliminary phase
        imspars=imSenderParameters(&scp1,ip_right_version,pkt_pool_right_sender,rightport,"Preliminary",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                   ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,
                                   ip_right_varies,ip_left_varies,ip_right_min,ip_right_max,ip_left_min,ip_left_max,
                                   ipv4_right_offset,ipv4_left_offset,ipv6_right_offset,ipv6_left_offset,
                                   rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,
                                   enumerate_ips,enumerate_ports,pre_frames,uniqueIpComb,uniqueFtComb);

        // start right sender
        if ( rte_eal_remote_launch(imsend, &imspars, cpu_right_sender) )
          std::cout << "Error: could not Initiator's Sender." << std::endl;
      }

      // set parameters for the left receiver
      rrpars1=rReceiverParameters(finish_receiving_pre,leftport,"Preliminary",state_table_size,&valid_entries,&stateTable); 

      // start left receiver
      if ( rte_eal_remote_launch(rreceive, &rrpars1, cpu_left_receiver) )
        std::cout << "Error: could not start Responder's Receiver." << std::endl;

      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Preliminary frame sending initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;

      // wait until active senders and receivers finish
      rte_eal_wait_lcore(cpu_right_sender);
      rte_eal_wait_lcore(cpu_left_receiver);
      
      if ( valid_entries < state_table_size )
        rte_exit(EXIT_FAILURE, "Error: Failed to fill state table (valid entries: %u, state table size: %u)!\n", valid_entries, state_table_size);
      else
        std::cout << "Info: Preliminary phase finished." << std::endl;

      // Now the real test may follow.

      // set "common" parameters
      scp2=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc);

      if ( reverse ) {  // Right to Left direction is active

        if ( !ip_varies ) { // use traditional single source and destination IP addresses (no enumeration in phase 2)

        // set "individual" parameters for the sender of the Initiator residing on the right side
    
          // initialize the parameter class instance for real test (reuse previously prepared 'ipq')
          spars2=senderParameters(&scp2,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                  ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                  rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max);
    
          // start right sender
          if ( rte_eal_remote_launch(send, &spars2, cpu_right_sender) )
            std::cout << "Error: could not start Initiator's Sender." << std::endl;
   
        } else { // use multiple source and/or destination IP addresses

          // initialize the parameter class instance
          mspars2=mSenderParameters(&scp2,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                    ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,
                                    ip_right_varies,ip_left_varies,ip_right_min,ip_right_max,ip_left_min,ip_left_max,
                                    ipv4_right_offset,ipv4_left_offset,ipv6_right_offset,ipv6_left_offset,
                                    rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max);

          // start right sender
          if (rte_eal_remote_launch(msend, &mspars2, cpu_right_sender) )
            std::cout << "Error: could not start Right Sender." << std::endl;
        }

        // set parameters for the left receiver
        rrpars2=rReceiverParameters(finish_receiving,leftport,"Reverse",state_table_size,&valid_entries,&stateTable);
  
        // start left receiver
        if ( rte_eal_remote_launch(rreceive, &rrpars2, cpu_left_receiver) )
          std::cout << "Error: could not start Responder's Receiver." << std::endl;
      }

      if ( forward ) {  // Left to right direction is active
        // set individual parameters for the left sender

        // first, collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
                   &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);

        // then, initialize the parameter class instance
        rspars=rSenderParameters(&scp2,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                 ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                 fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,
                                 state_table_size,stateTable,responder_tuples);

        // start left sender
        if (rte_eal_remote_launch(rsend, &rspars, cpu_left_sender) )
          std::cout << "Error: could not start Left Sender." << std::endl;

        // set parameters for the right receiver
        rpars2=receiverParameters(finish_receiving,rightport,"Forward");

        // start right receiver
        if ( rte_eal_remote_launch(receive, &rpars2, cpu_right_receiver) )
          std::cout << "Error: could not start Right Receiver." << std::endl;
      }

      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Testing initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;

      // wait until active senders and receivers finish
      if ( reverse ) {
        rte_eal_wait_lcore(cpu_right_sender);
        rte_eal_wait_lcore(cpu_left_receiver);
      }
      if ( forward ) {
        rte_eal_wait_lcore(cpu_left_sender);
        rte_eal_wait_lcore(cpu_right_receiver);
      }
      std::cout << "Info: Test finished." << std::endl;
      break;
      }
  } */
}

// sets the values of the data fields
senderCommonParameters::senderCommonParameters(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t test_duration_,
                                              uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, uint32_t number_of_lwB4s_, uint16_t num_of_port_sets_,
                                              uint16_t num_of_ports_, struct in6_addr *dut_ipv6_tunnel_, uint32_t *tester_fw_rec_ipv4_, struct in6_addr *dut_fw_ipv6_, 
                                              struct in6_addr *tester_bg_send_ipv6_, struct in6_addr *tester_bg_rec_ipv6_, struct in6_addr *tester_fw_send_ipv6_, uint16_t bg_rv_sport_min_, uint16_t bg_rv_sport_max_, 
                                              uint16_t bg_fw_dport_min_, uint16_t bg_fw_dport_max_)
{

  ipv6_frame_size = ipv6_frame_size_;
  ipv4_frame_size = ipv4_frame_size_;
  frame_rate = frame_rate_;
  test_duration = test_duration_;
  n = n_; //??
  m = m_; //??
  hz = hz_;
  start_tsc = start_tsc_;
  number_of_lwB4s = number_of_lwB4s_;
  num_of_port_sets = num_of_port_sets_;
  num_of_ports = num_of_ports_;
  dut_ipv6_tunnel = dut_ipv6_tunnel_;
  dut_fw_ipv6 = dut_fw_ipv6_;
  tester_bg_send_ipv6 = tester_bg_send_ipv6_; 
  tester_bg_rec_ipv6 = tester_bg_rec_ipv6_;
  tester_fw_send_ipv6 = tester_fw_send_ipv6_;
  tester_fw_rec_ipv4 = tester_fw_rec_ipv4_;
  bg_rv_sport_min = bg_rv_sport_min_;
  bg_rv_sport_max = bg_rv_sport_max_;
  bg_fw_dport_min = bg_fw_dport_min_;
  bg_fw_dport_max = bg_fw_dport_max_;
}

// sets the values of the data fields
senderParameters::senderParameters(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
                                  lwB4_data *lwB4_array_, struct ether_addr *dst_mac_, struct ether_addr *src_mac_, unsigned var_sport_, unsigned var_dport_,
                                  uint16_t preconfigured_port_min_, uint16_t preconfigured_port_max_)
{
cp = cp_;
pkt_pool = pkt_pool_;
eth_id = eth_id_;
direction = direction_;
lwB4_array = lwB4_array_;
dst_mac = dst_mac_;
src_mac = src_mac_;
var_sport = var_sport_;
var_dport = var_dport_;
preconfigured_port_min = preconfigured_port_min_;
preconfigured_port_max = preconfigured_port_max_;
}