#include "defines.h"
#include "includes.h"
#include "latency.h"
#include "Throughput.h"

int main(int argc, const char **argv) {
	
	Latency lat;
	if ( lat.readConfigFile(CONFIGFILE) < 0 )
    	return -1;
	if ( lat.readlwB4Data(LWB4DATAFILE) < 0 )
		return -1;
	if ( lat.readCmdLine(argc,argv) < 0 )
    	return -1;
	if ( lat.init(argv[0], LEFTPORT, RIGHTPORT) < 0)
		return -1;
	lat.measure(LEFTPORT, RIGHTPORT);
	
	return 0;
}