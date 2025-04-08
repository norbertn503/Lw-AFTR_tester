#include "defines.h"
#include "includes.h"
#include "Throughput.h"


int main(int argc, const char **argv) {
	
	std::cout << "MAIN STARTED" << std::endl;
	
	Throughput tp;
	if ( tp.readConfigFile(CONFIGFILE) < 0 )
    	return -1;
	if ( tp.readlwB4Data(LWB4DATAFILE) < 0 )
		return -1;
	if ( tp.readCmdLine(argc,argv) < 0 )
    	return -1;
	if ( tp.init(argv[0], LEFTPORT, RIGHTPORT) < 0)
		return -1;
	tp.measure(LEFTPORT, RIGHTPORT);
	
	return 0;
}