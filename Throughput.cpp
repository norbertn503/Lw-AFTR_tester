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
  //lwB4_array = NULL;
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
    if (line[pos] == '[')  //new lwB4
      return -2;
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
  std::cout << "READ CONFIG FILE" << std::endl;
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
      if ( sscanf(line+pos, "%hu", &rev_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'FW-dport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "FW-dport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &rev_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'FW-dport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "RV-sport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &rev_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'RV-sport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "RV-sport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &rev_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'RV-sport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "bg-FW-dport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &bg_fw_dport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'bg-FW-dport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "bg-FW-dport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &bg_fw_dport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'bg-FW-dport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "bg-RV-sport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &bg_rv_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'bg-RV-sport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "bg-RV-sport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%hu", &bg_rv_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'bg-RV-sport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "PSID_length")) >= 0 ) {
      sscanf(line+pos, "%u", &psid_length);
      if ( psid_length < 1 || psid_length > 16 ) {
        std::cerr << "Input Error: 'PSID_length' must be >= 1 and <= 16." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "PSID")) >= 0 ) {
      sscanf(line+pos, "%u", &psid); 
      if ( psid == 0 ) {
        std::cerr << "Input Error: 'PSID' cannot be 0." << std::endl;
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
    } else if ( nonComment(line) ) { // It may be too strict!
        std::cerr << "Input Error: Cannot interpret '" << filename << "' line " << line_no << ":" << std::endl;
        std::cerr << line << std::endl;
        return -1;
    } 
  }
  fclose(f);
  
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
  return 0;
}

// reads the config files for lwB4s, creates lwB4_data object for each lwB4 and stores them in Array/Vector
int Throughput::readlwB4Data(const char *filename) {
  std::cout << "REA LWB4 CONFIG FILE" << std::endl;
  FILE *f; 	// file descriptor
  char line[LINELEN+1]; // buffer for reading a line of the input file
  int pos; 	// position in the line after the key (parameter name) was found
  uint8_t *m; 	// pointer to the MAC address being read
  int line_no;	// line number for error message

  lwB4_data tmp_obj;
  bool new_lwB4 = false;

  f=fopen(LWB4DATAFILE,"r");
  if ( f == NULL ) {
    std::cerr << "Input Error: Can't open file '" << LWB4DATAFILE << "'." << std::endl;
    return -1;
  }
  
  for ( line_no=1; fgets(line, LINELEN+1, f); line_no++ ) {
	  if ( (pos = findKey(line, "[]")) == -2 ){
		  std::cout << "Uj object" << std::endl;
		  if (new_lwB4){
			   tmp_lwb4data.push_back(tmp_obj);	
		  }
		  tmp_obj = {};
		  new_lwB4 = true;
    }
    else {
      std::cout << "NEM UJ OBJECT" << std::endl;
      if ( (pos = findKey(line, "b4-ipv6")) >= 0 ) {
        if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&tmp_obj.b4_ipv6_addr)) != 1 ) {
          std::cerr << "Input Error: Bad 'b4-ipv6' address." << std::endl;
          return -1;
        }
      } else if ( (pos = findKey(line, "br-address")) >= 0 ) {
        if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&tmp_obj.aftr_tunnel_addr)) != 1 ) {
          std::cerr << "Input Error: Bad 'br-address' address." << std::endl;
          return -1;
        }
      } else if ( (pos = findKey(line, "ipv4")) >= 0 ) {
        if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&tmp_obj.ipv4_addr)) != 1 ) {
          std::cerr << "Input Error: Bad 'ipv4' address." << std::endl;
          return -1;
        }
      } else if ( (pos = findKey(line, "psid-length")) >= 0 ) {
        sscanf(line+pos, "%u", &tmp_obj.psid_length);
        if ( tmp_obj.psid_length < 1 || tmp_obj.psid_length > 16 ) {
          std::cerr << "Input Error: 'psid_length' must be >= 1 and <= 16." << std::endl;
          return -1;
        }
      } else if ( (pos = findKey(line, "psid")) >= 0 ) {
        sscanf(line+pos, "%u", &tmp_obj.psid);
        if ( tmp_obj.psid <= 0 ) {
          std::cerr << "Input Error: 'psid' cannot be 0 or negative." << std::endl;
          return -1;
        }
      }
	  }
  }   
  
  //Save the last lwB4 to the vector
  if (new_lwB4){
    tmp_lwb4data.push_back(tmp_obj);	
  }

  if (tmp_lwb4data.size() != number_of_lwB4s) {
    std::cerr << "Number of lwB4 number is not the same as declared in lw4o6.conf" << std::endl;
    return -1;
  }

  return 0;
}

// reads the command line arguments and stores the information in data members of class Throughput
// It may be called only AFTER the execution of readConfigFile
int Throughput::readCmdLine(int argc, const char *argv[])
{
  std::cout << "READ CMD STARTED" << std::endl;
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
  std::cout << "READ CMD ENDED" << std::endl;
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

  rte_argv[2] = coresList;
  rte_argv[3] = "-n";
  snprintf(numChannels, 11, "%hhu", memory_channels);
  rte_argv[4] = numChannels;
  rte_argv[5] = 0;

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
  cfg_port.txmode.mq_mode = RTE_ETH_MQ_TX_NONE; // no multi queues
  cfg_port.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE; // no multi queues

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
  } while (link_info.link_status == RTE_ETH_LINK_DOWN);
  trials = 0;
  do
  {
    if (trials++ == MAX_PORT_TRIALS)
    {
      std::cerr << "Error: Right Ethernet port is DOWN, Tester exits." << std::endl;
      return -1;
    }
    rte_eth_link_get(rightport, &link_info);
  } while (link_info.link_status == RTE_ETH_LINK_DOWN);

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
  
  // allocate and build a memory to store the data of all possible simulated CEs.
  // The number of these CEs is identified as a configuration file parameter
  
  thread_local std::random_device rd;
  thread_local std::mt19937 gen {rd()};
  std::ranges::shuffle(tmp_lwb4data, gen);

  lwB4_array = (lwB4_data *)rte_malloc("CEs data memory", tmp_lwb4data.size() * sizeof(lwB4_data), 0);
  if (!lwB4_array)
    rte_exit(EXIT_FAILURE, "malloc failure!! Can not create memory for CEs data\n");

  for(int i = 0; i < tmp_lwb4data.size(); i++){
    num_of_port_sets = pow(2.0, tmp_lwb4data.at(i).psid_length);
    num_of_ports = (int)(65536.0 / num_of_port_sets);

    std::cout << "PSID: " << tmp_lwb4data.at(i).psid << std::endl;
    std::cout << "PSID-length: " << tmp_lwb4data.at(i).psid_length << std::endl;
    std::cout << "NUM OF PORTS: " << num_of_ports << std::endl;
    
    tmp_lwb4data.at(i).min_port = num_of_ports * tmp_lwb4data.at(i).psid;
    std::cout << "MIN PORT: " << tmp_lwb4data.at(i).min_port << std::endl;

    if(tmp_lwb4data.at(i).min_port < 1024){
      std::cerr << "System Ports can't be used by lwB4s"  << std::endl;
      return -1;
    } else if(tmp_lwb4data.at(i).min_port > 65535){
      std::cerr << "Minimum port for lwB4 can't be greater than 65535"  << std::endl;
      return -1;
    }
    
    tmp_lwb4data.at(i).max_port = tmp_lwb4data.at(i).min_port + num_of_ports -1; 
    std::cout << "MAX PORT: " << tmp_lwb4data.at(i).max_port << std::endl;

    if(tmp_lwb4data.at(i).max_port > 65535){
      std::cerr << "Maximum port for lwB4 can't be greater than 65535" << std::endl;
      return -1;
    }
    tmp_lwb4data.at(i).ipv4_addr_chksum = rte_raw_cksum(&tmp_lwb4data.at(i).ipv4_addr,4); //calculate the IPv4 header checksum
    
    std::cout << "-----------------------------" << std::endl;
    lwB4_array[i] = tmp_lwb4data.at(i);
  }
  
  
  std::cout << "---------RANDOM START----------" << std::endl;
  for(int i = 0; i < number_of_lwB4s; i++){
    std::cout << "PSID: " << lwB4_array[i].psid << std::endl;
    std::cout << "PSID-length: " << lwB4_array[i].psid_length << std::endl;
    std::cout << "MIN PORT: " << lwB4_array[i].min_port << std::endl;
    std::cout << "MAX PORT: " << lwB4_array[i].max_port << std::endl;
    std::cout << "-----------------------------" << std::endl;
  }
  
  std::cout << "INIT lefutott" << std::endl;
  return 0;
} //end init

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
  ipv4_hdr->hdr_checksum = rte_ipv4_cksum(ipv4_hdr); 
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
  
  senderCommonParameters scp(ipv6_frame_size, ipv4_frame_size, frame_rate, test_duration,
                            n, m, hz, start_tsc, number_of_lwB4s, lwB4_array, &dut_fw_ipv6, 
                            &tester_bg_send_ipv6, &tester_bg_rec_ipv6
                            );
  
                           // senderCommonParameters scp(ipv6_frame_size, ipv4_frame_size, frame_rate, test_duration, n, m, hz, start_tsc,
                           //   CE, num_of_CEs, num_of_port_sets, num_of_ports, &dmr_ipv6, &tester_right_ipv6);
  if (forward)
  { // Left to right direction is active
    
    // set individual parameters for the left sender
    // Initialize the parameter class instance
    senderParameters spars(&scp, pkt_pool_left_sender, leftport, "forward", (ether_addr *)dut_fw_mac, (ether_addr *)tester_fw_mac, 
                           fwd_var_sport, fwd_var_dport, fwd_dport_min, fwd_dport_max);
  
    

    // start left sender
    if (rte_eal_remote_launch(send, &spars, cpu_fw_send))
      std::cout << "Error: could not start Left Sender." << std::endl;

    // set parameters for the right receiver
    //receiverParameters rpars(finish_receiving, rightport, "forward");

    // start right receiver
    //if (rte_eal_remote_launch(receive, &rpars, cpu_fw_receive))
    //  std::cout << "Error: could not start Right Receiver." << std::endl;
  }
  
  rte_free(lwB4_array); // release the CEs data memory
}

// sets the values of the data fields
senderCommonParameters::senderCommonParameters(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t test_duration_,
                                              uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, uint32_t number_of_lwB4s_, lwB4_data *lwB4_array_,
                                              struct in6_addr *dut_fw_ipv6_, struct in6_addr *tester_bg_send_ipv6_, struct in6_addr *tester_bg_rec_ipv6_ 
                                              )
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
  dut_fw_ipv6 = dut_fw_ipv6_;
  tester_bg_send_ipv6 = tester_bg_send_ipv6_; 
  tester_bg_rec_ipv6 = tester_bg_rec_ipv6_;
  lwB4_array = lwB4_array_;
}

// sets the values of the data fields
senderParameters::senderParameters(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
                                  struct ether_addr *dst_mac_, struct ether_addr *src_mac_, unsigned var_sport_, unsigned var_dport_,
                                  uint16_t preconfigured_port_min_, uint16_t preconfigured_port_max_)
{
  cp = cp_;
  pkt_pool = pkt_pool_;
  eth_id = eth_id_;
  direction = direction_;
  dst_mac = dst_mac_;
  src_mac = src_mac_;
  var_sport = var_sport_;
  var_dport = var_dport_;
  preconfigured_port_min = preconfigured_port_min_;
  preconfigured_port_max = preconfigured_port_max_;
}

// sends Test Frames for throughput (or frame loss rate) measurement
int send(void *par)
{
  std::cout << "Send STARTED" << std::endl;
  
  //  collecting input parameters:
  class senderParameters *p = (class senderParameters *)par;
  class senderCommonParameters *cp = p->cp;

  // parameters directly correspond to the data members of class Throughput
  uint16_t ipv6_frame_size = cp->ipv6_frame_size;
  uint16_t ipv4_frame_size = cp->ipv4_frame_size;
  uint32_t frame_rate = cp->frame_rate;
  uint16_t test_duration = cp->test_duration;
  uint32_t n = cp->n;
  uint32_t m = cp->m;
  uint64_t hz = cp->hz;
  uint64_t start_tsc = cp->start_tsc;
  uint32_t num_of_lwB4s = cp->number_of_lwB4s;
  uint16_t num_of_port_sets = cp->num_of_port_sets;
  uint16_t num_of_ports = cp->num_of_ports;

  struct in6_addr *dut_ipv6_tunnel = cp->dut_ipv6_tunnel;
  uint32_t *tester_fw_rec_ipv4 = cp->tester_fw_rec_ipv4;
  struct in6_addr *dut_fw_ipv6 = cp->dut_fw_ipv6;
  struct in6_addr *tester_fw_send_ipv6 = cp->tester_fw_send_ipv6;
  struct in6_addr *tester_bg_send_ipv6 = cp->tester_bg_send_ipv6;
  struct in6_addr *tester_bg_rec_ipv6 = cp->tester_bg_rec_ipv6;

  uint16_t bg_fw_dport_min = cp->bg_fw_dport_min; 
  uint16_t bg_fw_dport_max = cp->bg_fw_dport_max; 
  uint16_t bg_rv_sport_min = cp->bg_rv_sport_min; 
  uint16_t bg_rv_sport_max = cp->bg_rv_sport_max;

  // parameters which are different for the Left sender and the Right sender
  rte_mempool *pkt_pool = p->pkt_pool;
  uint8_t eth_id = p->eth_id;
  const char *direction = p->direction;
  //CE_data *CE_array = p->CE_array;
  struct ether_addr *dst_mac = p->dst_mac;
  struct ether_addr *src_mac = p->src_mac;
  unsigned var_sport = p->var_sport;
  unsigned var_dport = p->var_dport;
  uint16_t preconfigured_port_min = p->preconfigured_port_min;
  uint16_t preconfigured_port_max = p->preconfigured_port_max;

  
  // further local variables
  uint64_t frames_to_send = test_duration * frame_rate; // Each active sender sends this number of frames
  uint64_t sent_frames = 0;                             // counts the number of sent frames
  double elapsed_seconds;                               // for checking the elapsed seconds during sending

    
  // temperoray initial IP addresses that will be put in the template packets and they will be changed later in the sending loop
  // useful to calculate correct checksums 
  //(more specifically, the uncomplemented checksum start value after calculating it by the DPDK rte_ipv4_cksum(), rte_ipv4_udptcp_cksum(), and rte_ipv6_udptcp_cksum() functions
  // when creating the template packets)
  uint32_t zero_dst_ipv4;
  struct in6_addr zero_src_ipv6;

  struct rte_mbuf *pkt_mbuf;

  /*pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, direction, dst_mac, src_mac, tester_bg_send_ipv6, tester_bg_rec_ipv6, var_sport, var_dport);// finally, send the frame
  std::cout << "IPV6 csomag elkészült" << std::endl;
  std::cout << ipv6_frame_size << std::endl;
  std::cout << frame_rate << std::endl;
  while (rte_rdtsc() < start_tsc + sent_frames * hz / frame_rate)
  std::cout << "ELSŐ while ciklusban" << std::endl;
  ; // Beware: an "empty" loop, as well as in the next line
  std::cout << "ELSŐ while ciklus vége" << std::endl;
  std::cout << "ELSŐ while ciklus vége2" << std::endl;
  while (!rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1))
  ; // send out the frame
  std::cout << "LEFUTOTT" << std::endl;
/*  
  // the dst_ipv4 must initially be "0.0.0.0" in order for the ipv4 header checksum to be calculated correctly by The rte_ipv4_cksum() 
  // and for the udp checksum to be calculated correctly the rte_ipv4_udptcp_cksum()
 // and consequently calculate correct checksums in the mKTestFrame4()
  if (inet_pton(AF_INET, "0.0.0.0", reinterpret_cast<void *>(&zero_dst_ipv4)) != 1)
  {
    std::cerr << "Input Error: Bad virt_dst_ipv4 address." << std::endl;
    return -1;
  }

  // the src_ipv6 must initially be "::" for the udp checksum to be calculated correctly by the rte_ipv6_udptcp_cksum
  // and consequently calculate correct checksum in the mKTestFrame6()
  if (inet_pton(AF_INET6, "::", reinterpret_cast<void *>(&zero_src_ipv6)) != 1)
  {
    std::cerr << "Input Error: Bad  virt_src_ipv6 address." << std::endl;
    return -1;
  }
  
  // These addresses are for the foreground traffic in the reverse direction
  // setting the source ipv4 address of the reverse direction to the ipv4 address of the tester right interface
  uint32_t *src_ipv4 = tester_fw_rec_ipv4;// This would be set without change during testing in the reverse direction.
                                       // It will represent the ipv4 address of the right interface of the Tester
  *src_ipv4 = htonl(*src_ipv4);
  
  uint32_t *dst_ipv4 = &zero_dst_ipv4; // This would be variable during testing in the reverse direction.
                                       // It will represent the simulated CE (BMR-ipv4-prefix + suffix)
                                       // and is merely specified inside the sending loop using the CE_array

  // These addresses are for the foreground traffic in the forward direction
  struct in6_addr *src_ipv6 = &zero_src_ipv6; // This would be variable during testing in the forward direction.
                                              // It will represent the simulated CE (MAP address) 
                                              //and is merely specified inside the sending loop using the CE_array
                                               
  struct in6_addr *dst_ipv6 = dmr_ipv6; // This would be set without change during testing in the forward direction.
                                        // It will represent the DMR IPv6 address.

  // These addresses are for the background traffic only
  struct in6_addr *src_bg = (direction == "forward" ? tester_l_ipv6 : tester_r_ipv6);  
  struct in6_addr *dst_bg = (direction == "forward" ? tester_r_ipv6 : tester_l_ipv6); 
  
  uint16_t sport_min, sport_max, dport_min, dport_max; // worker port range variables
  
// set the relevant ranges to the wide range prespecified in the configuration file (usually comply with RFC 4814)
// the other ranges that are not set now. They will be set in the sending loop because they are based on the PSID of the
//pseudorandomly enumerated CE
  if (direction == "reverse")
    {
      sport_min = preconfigured_port_min;
      sport_max = preconfigured_port_max;
    }
  else //forward
    {
      dport_min = preconfigured_port_min;
      dport_max = preconfigured_port_max;
    }
  
  // check whether the CE array is built or not
   if(!CE_array)
    rte_exit(EXIT_FAILURE,"No CE array can be accessed by the %s sender",direction);
    
  
  // implementation of varying port numbers recommended by RFC 4814 https://tools.ietf.org/html/rfc4814#section-4.5
  // RFC 4814 requires pseudorandom port numbers, increasing and decreasing ones are our additional, non-stantard solutions
  // always one of the same N pre-prepared foreground or background frames is updated and sent,
  // source and/or destination IP addresses and port number(s), and UDP and IPv4 header checksum are updated
  // N size arrays are used to resolve the write after send problem

  //some worker variables
  int i;                                                       // cycle variable for the above mentioned purpose: takes {0..N-1} values
  int current_CE;                                              // index variable to the current simulated CE in the CE_array
  uint16_t psid;                                               // working variable for the pseudorandomly enumerated PSID of the currently simulated CE
  struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
  uint8_t *pkt;                                                // working pointer to the current frame (in the message buffer)
  
  //IP workers
  uint32_t *fg_dst_ipv4[N], *fg_src_ipv4[N];
  struct in6_addr *fg_src_ipv6[N]; *fg_dst_ipv6[N];
  struct in6_addr *bg_src_ipv6[N], *bg_dst_ipv6[N];
  uint16_t *fg_ipv4_chksum[N];
  
  //UDP workers
  uint16_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; 
  uint16_t *udp_sport, *udp_dport, *udp_chksum;   

  uint16_t fg_udp_chksum_start, bg_udp_chksum_start, fg_ipv4_chksum_start; // starting values (uncomplemented checksums taken from the original frames created by mKTestFrame functions)                    
  uint32_t chksum = 0; // temporary variable for UDP checksum calculation
  uint32_t ip_chksum = 0; //temporary variable for IPv4 header checksum calculation
  uint16_t sport, dport, bg_sport, bg_dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done
  uint16_t sp, dp;                           // values of source and destination port numbers -- temporary values

  // creating buffers of template test frames
 for (i = 0; i < N; i++)
  {

    // create a foreground Test Frame
    if (direction == "reverse")
    {

      fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size, pkt_pool, direction, dst_mac, src_mac, src_ipv4, dst_ipv4, var_sport, var_dport);
      pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
      // the source ipv4 address will not be manipulated as it will permenantly be the tester-right-ipv4 (extracted from the dmr-ipv6 as done above)
      fg_ipv4_chksum[i] = (uint16_t *)(pkt + 24);
      fg_dst_ipv4[i] = (uint32_t *)(pkt + 30); // The destination ipv4 should be manipulated in the sending loop as it will be the BMR-ipv4-prefix + suffix (i.e. changing each time) in the reverse direction
      // The source address will not be manipulated as it will permentantly be the IP address of the right interface of the Tester (as done in the initilization above)
      fg_udp_sport[i] = (uint16_t *)(pkt + 34);
      fg_udp_dport[i] = (uint16_t *)(pkt + 36);
      fg_udp_chksum[i] = (uint16_t *)(pkt + 40);
    }
    else
    { //"forward"
      fg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, direction, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport);
      pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
      fg_src_ipv6[i] = (struct in6_addr *)(pkt + 22);    // The source address should be manipulated as it will be the MAP address (i.e. changing each time) in the forward direction
      // The destination address will not be manipulated as it will permenantly be the DMR IPv6 address(as done in the initilization above)
      fg_udp_sport[i] = (uint16_t *)(pkt + 54);
      fg_udp_dport[i] = (uint16_t *)(pkt + 56);
      fg_udp_chksum[i] = (uint16_t *)(pkt + 60);
    }
    // Always create a backround Test Frame (it is always an IPv6 frame) regardless of the direction of the test
    // The source and destination IP addresses of the packet have already been set in the initialization above
    // and they will permenantely be the IP addresses of the left and right interfaces of the Tester 
    // and based on the direction of the test 
    bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, direction, dst_mac, src_mac, src_bg, dst_bg, var_sport, var_dport);
    pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
    bg_udp_sport[i] = (uint16_t *)(pkt + 54);
    bg_udp_dport[i] = (uint16_t *)(pkt + 56);
    bg_udp_chksum[i] = (uint16_t *)(pkt + 60);
  }

  //save the uncomplemented UDP checksum value (same for all values of [i]). So, [0] is enough
  fg_udp_chksum_start = ~*fg_udp_chksum[0]; // for the foreground frames 
  bg_udp_chksum_start = ~*bg_udp_chksum[0]; // same but for the background frames
  
  // save the uncomplemented IPv4 header checksum (same for all values of [i]). So, [0] is enough
  if (direction == "reverse") // in case of foreground IPv4 only
      fg_ipv4_chksum_start = ~*fg_ipv4_chksum[0]; 

  //  arrays to store the minimum and maximum possible source and destination port numbers in each port set.
  uint16_t sport_min_for_ps[num_of_port_sets], sport_max_for_ps[num_of_port_sets], dport_min_for_ps[num_of_port_sets], dport_max_for_ps[num_of_port_sets];

  // arrays of indices to know the current source and destination port numbers for each port set, to be used in case of incrementing or decrementing
  uint16_t curr_sport_for_ps[num_of_port_sets];  // used to restore the last used sport in the port set to set the next sport to.
  uint16_t curr_dport_for_ps[num_of_port_sets];  // used to restore the last used dport in the port set to set the next dport to.
  uint16_t curr_sport_for_bg, curr_dport_for_bg; // used to restore the last used port in the background traffic in case the sport or dport are modified in the range of a port set

  for (i = 0; i < num_of_port_sets; i++)
  {
    // set the port boundaries for each port set
    sport_min_for_ps[i] = (uint16_t)(i * num_of_ports);
    sport_max_for_ps[i] = (uint16_t)((i + 1) * num_of_ports) - 1;

    dport_min_for_ps[i] = (uint16_t)(i * num_of_ports);
    dport_max_for_ps[i] = (uint16_t)((i + 1) * num_of_ports) - 1;

    // set the initial values of port numbers for each port set, depending whether they will be increased (1) or decreased (2)
    if (var_sport == 1)
      curr_sport_for_ps[i] = sport_min_for_ps[i];
    if (var_dport == 1)
      curr_dport_for_ps[i] = dport_min_for_ps[i];
    if (var_sport == 2)
      curr_sport_for_ps[i] = sport_max_for_ps[i];
    if (var_dport == 2)
      curr_dport_for_ps[i] = dport_max_for_ps[i];
  }

  // The sport and dport values are initialized according to wide range of values.
  // However, for the foreground packets, in the forward direction, the sport_min and sport_max will be set later in the sending loop based on the generated PSID
  // in the reverse direction, the dport_min and dport_max will be set later in the sending loop based on the generated PSID
  // No change for bg_sport and bg_dport in case of the background packets.
  if (var_sport == 1)
  {
    sport = sport_min;
    bg_sport = bg_rv_sport_min;
  }
  if (var_sport == 2)
  {
    sport = sport_max;
    bg_sport = bg_rv_sport_max;
  }
  if (var_dport == 1)
  {
    dport = dport_min;
    bg_dport = bg_fw_dport_min;
  }
  if (var_dport == 2)
  {
    dport = dport_max;
    bg_dport = bg_fw_dport_max;
  }

  i = 0; // increase maunally after each sending
  current_CE = 0; // increase maunally after each sending

  // prepare random number infrastructure
  thread_local std::random_device rd_sport;           // Will be used to obtain a seed for the random number engines
  thread_local std::mt19937_64 gen_sport(rd_sport()); // Standard 64-bit mersenne_twister_engine seeded with rd()
  thread_local std::random_device rd_dport;           // Will be used to obtain a seed for the random number engines
  thread_local std::mt19937_64 gen_dport(rd_dport()); // Standard 64-bit mersenne_twister_engine seeded with rd()

  // naive sender version: it is simple and fast
  for (sent_frames = 0; sent_frames < frames_to_send; sent_frames++)
  { // Main cycle for the number of frames to send
    // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame

    if (sent_frames % n < m)
    {
      // foreground frame is to be sent

      psid = CE_array[current_CE].psid;
      chksum = fg_udp_chksum_start; // restore the uncomplemented UDP checksum to add the values of the varying fields
      udp_sport = fg_udp_sport[i];
      udp_dport = fg_udp_dport[i];
      udp_chksum = fg_udp_chksum[i];
      pkt_mbuf = fg_pkt_mbuf[i];

      if (direction == "forward")
      {

        *fg_src_ipv6[i] = CE_array[current_CE].map_addr; // set it with the map address
        chksum += CE_array[current_CE].map_addr_chksum;  // and add its checksum to the UDP checksum

        // the sport_min and sport_max will be set according to the port range values of the selected port set and the sport will retrieve its last value within this range
        // the dport_min and dport_max will remain on thier default values within the wide range. The dport will be changed based on its value from the last cycle.
        sport_min = sport_min_for_ps[psid];
        sport_max = sport_max_for_ps[psid];
        if (var_sport == 1 || var_sport == 2)
          sport = curr_sport_for_ps[psid]; // restore the last used sport in the port set to start over from it (useful when increment or decrement; useless when random)
      }

      if (direction == "reverse")
      {
        ip_chksum = fg_ipv4_chksum_start; // restore the uncomplemented IPv4 header checksum to add the checksum value of the destination IPv4 address

        *fg_dst_ipv4[i] = CE_array[current_CE].ipv4_addr; //set it with the CE's IPv4 address

        chksum += CE_array[current_CE].ipv4_addr_chksum; //add its chechsum to the UDP checksum
        ip_chksum += CE_array[current_CE].ipv4_addr_chksum; //and to the IPv4 header checksum

        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = ((ip_chksum & 0xffff0000) >> 16) + (ip_chksum & 0xffff); // calculate 16-bit one's complement sum
        ip_chksum = (~ip_chksum) & 0xffff;                                   // make one's complement
        if (ip_chksum == 0)                                                  // checksum should not be 0 (0 means, no checksum is used)
          ip_chksum = 0xffff;
        *fg_ipv4_chksum[i] = (uint16_t)ip_chksum; //now set the IPv4 header checksum of the packet

        // the dport_min and dport_max will be set according to the port range values of the selected port set and the dport will retrieve its last value within this range
        // the sport_min and sport_max will remain on thier default values within the wide range. The sport will be changed based on its value from the last cycle.
        dport_min = dport_min_for_ps[psid];
        dport_max = dport_max_for_ps[psid];
        if (var_dport == 1 || var_dport == 2)
          dport = curr_dport_for_ps[psid]; // restore the last used dport in the ps to start over from it (useful when increment or decrement; useless when random)
      }

    // time to change the value of the source and destination port numbers
    if (var_sport)
    {
      // sport is varying
      switch (var_sport)
      {
      case 1: // increasing port numbers
        if ((sp = sport++) == sport_max)
          sport = sport_min;
        break;
      case 2: // decreasing port numbers
        if ((sp = sport--) == sport_min)
          sport = sport_max;
        break;
      case 3: // pseudorandom port numbers
        std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max); // uniform distribution in [sport_min, sport_max]
        sp = uni_dis_sport(gen_sport);
      }
      *udp_sport = htons(sp); // set the source port 
      chksum += *udp_sport; // and add it to the UDP checksum
    }
    if (var_dport)
    {
      // dport is varying
      switch (var_dport)
      {
      case 1: // increasing port numbers
        if ((dp = dport++) == dport_max)
          dport = dport_min;
        break;
      case 2: // decreasing port numbers
        if ((dp = dport--) == dport_min)
          dport = dport_max;
        break;
      case 3: // pseudorandom port numbers
        std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max); // uniform distribution in [sport_min, sport_max]
        dp = uni_dis_dport(gen_dport);
      }
      *udp_dport = htons(dp); // set the destination port 
      chksum += *udp_dport; // and add it to the UDP checksum
    }
    if (direction == "forward")
        curr_sport_for_ps[psid] = sport; // save the current sport of the ps as a starting point for the later use if the same ps will be selected
      else
        curr_dport_for_ps[psid] = dport; // save the current dport of the ps as a starting point for the later use if the same ps will be selected
    }
    else
    {
      // background frame is to be sent
      // from here, we need to handle the background frame identified by the temporary variables

      chksum = bg_udp_chksum_start; // restore the uncomplemented UDP checksum to add the values of the varying fields
      udp_sport = bg_udp_sport[i];
      udp_dport = bg_udp_dport[i];
      udp_chksum = bg_udp_chksum[i];
      pkt_mbuf = bg_pkt_mbuf[i];
  
    // time to change the value of the source and destination port numbers
    if (var_sport)
    {
      // sport is varying
      switch (var_sport)
      {
      case 1: // increasing port numbers
        if ((sp = bg_sport++) == bg_rv_sport_max)
          bg_sport = bg_rv_sport_min;
        break;
      case 2: // decreasing port numbers
        if ((sp = bg_sport--) == bg_rv_sport_min)
          bg_sport = bg_rv_sport_max;
        break;
      case 3: // pseudorandom port numbers
        std::uniform_int_distribution<int> uni_dis_sport(bg_rv_sport_min, bg_rv_sport_max); // uniform distribution in [sport_min, sport_max]
        sp = uni_dis_sport(gen_sport);
      }
      *udp_sport = htons(sp); // set the source port 
      chksum += *udp_sport; // and add it to the UDP checksum
    }
    if (var_dport)
    {
      // dport is varying
      switch (var_dport)
      {
      case 1: // increasing port numbers
        if ((dp = bg_dport++) == bg_fw_dport_max)
          bg_dport = bg_fw_dport_min;
        break;
      case 2: // decreasing port numbers
        if ((dp = bg_dport--) == bg_fw_dport_min)
          bg_dport = bg_fw_dport_max;
        break;
      case 3: // pseudorandom port numbers
        std::uniform_int_distribution<int> uni_dis_dport(bg_fw_dport_min, bg_fw_dport_max); // uniform distribution in [sport_min, sport_max]
        dp = uni_dis_dport(gen_dport);
      }
      *udp_dport = htons(dp); // set the destination port 
      chksum += *udp_dport; // and add it to the UDP checksum
    }
    }

    //finalize the UDP checksum
    chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff); // calculate 16-bit one's complement sum
    chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff); // calculate 16-bit one's complement sum
    chksum = (~chksum) & 0xffff;                                // make one's complement
   
    if (direction == "reverse")
      {
        if (chksum == 0)                                        // checksum should not be 0 (0 means, no checksum is used)
          chksum = 0xffff;
      }
    *udp_chksum = (uint16_t)chksum; // set the UDP checksum in the frame

    // finally, send the frame
    while (rte_rdtsc() < start_tsc + sent_frames * hz / frame_rate)
      ; // Beware: an "empty" loop, as well as in the next line
    while (!rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1))
      ; // send out the frame

    current_CE = (current_CE + 1) % num_of_CEs; // proceed to the next CE element in the CE array
    i = (i + 1) % N;
  } // this is the end of the sending cycle
*/
  // Now, we check the time
  elapsed_seconds = (double)(rte_rdtsc() - start_tsc) / hz;
  printf("Info: %s sender's sending took %3.10lf seconds.\n", direction, elapsed_seconds);
  if (elapsed_seconds > test_duration * TOLERANCE)
    rte_exit(EXIT_FAILURE, "%s sending exceeded the %3.10lf seconds limit, the test is invalid.\n", direction, test_duration * TOLERANCE);
  printf("%s frames sent: %lu\n", direction, sent_frames);
  
  return 0;
}