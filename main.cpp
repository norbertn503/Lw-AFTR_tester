#include "defines.h"
#include "includes.h"
#include "Throughput.h"

int main(int argc, const char **argv) {
	std::cout << "MAIN STARTED" << std::endl;
	Throughput tp;
	std::cout << LEFTPORT << std::endl;
	std::cout << RIGHTPORT << std::endl;
	if ( tp.readConfigFile(CONFIGFILE) < 0 )
    	return -1;
	if ( tp.readCmdLine(argc,argv) < 0 )
    	return -1;
	std::cout << "Read CMDLine and ConfFile" << std::endl;
	if ( tp.init(argv[0], LEFTPORT, RIGHTPORT) < 0)
		return -1;
	//tp.measure(LEFTPORT, RIGHTPORT);
	
	
	return 0;
}
