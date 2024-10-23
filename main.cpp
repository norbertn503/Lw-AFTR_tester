#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
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
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include "tester.h"
#include "Measurement.h"





int main(int argc, char **argv){
	std::cout << "MAIN STARTED" << std::endl;
    //init(argv);
	Measurement ms;
	ms.init(argv);
	Tester tester;
	tester.create_package();
	ms.cleanup();
	//cleanup();
	std::cout << "Program ended" << std::endl;
	return 0;
}
