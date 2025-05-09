#include "defines.h"
#include "includes.h"
#include "pdv.h"
#include "Throughput.h"

int main(int argc, const char **argv) {
	
	PDV pdv;
	if ( pdv.readConfigFile(CONFIGFILE) < 0 )
    	return -1;
	if ( pdv.readlwB4Data(LWB4DATAFILE) < 0 )
		return -1;
	if ( pdv.readCmdLine(argc,argv) < 0 )
    	return -1;
	if ( pdv.init(argv[0], LEFTPORT, RIGHTPORT) < 0)
		return -1;
    pdv.measure(LEFTPORT, RIGHTPORT);
	
	return 0;
}