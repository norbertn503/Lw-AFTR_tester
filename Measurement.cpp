#include "Measurement.h" 
#include <iostream>

#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

int Measurement::init(char **list)
{
    #define LEFTPORT 0
    #define RIGHTPORT 1
	
    char coresList[101]; // buffer for preparing the list of lcores for DPDK init (like a command line argument)
	char numChannels[11]; // buffer for printing the number of memory channels into a string for DPDK init (like a command line argument)
	int ret;
	unsigned lcore_id;

	const char *rte_argv[6]; // parameters for DPDK EAL init, e.g.: {NULL, "-l", "4,5,6,7", "-n", "2", NULL};
  	int rte_argc = static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1; // argc value for DPDK EAL init

	rte_argv[0]=list[0]; 	// program name
    rte_argv[1]="-l";	// list of lcores will follow

	int cpu_left_sender = 4;
	int cpu_right_receiver = 8;
	int memory_channels = 4;

    snprintf(coresList, 101, "0,%d,%d", cpu_left_sender, cpu_right_receiver); // only forward (left to right) is active 
    std::cout << coresList << std::endl;

  	rte_argv[2]=coresList;
  	rte_argv[3]="-n";
  	snprintf(numChannels, 11, "%hhu", memory_channels);
  	rte_argv[4]=numChannels;
  	rte_argv[5]=0;

	ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
	if (ret < 0)
		rte_panic("Cannot init EAL\n");
	/* >8 End of initialization of Environment Abstraction Layer */

	if(rte_eth_dev_is_valid_port(LEFTPORT)){
		std::cout << "PORT OK" << std::endl;
	} else {std::cout << "PORT NOT OK" << std::endl; }

	if(rte_eth_dev_is_valid_port(RIGHTPORT)){
		std::cout << "PORT OK" << std::endl;		
	} else { std::cout << "PORT NOT OK" << std::endl; }
	//rte_eal_mp_wait_lcore();

	return 0;
}

void Measurement::cleanup()
{
    /* clean up the EAL */
	std::cout << "Cleanup called" << std::endl;
	rte_eal_cleanup();
}