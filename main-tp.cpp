#include "defines.h"
#include "includes.h"
#include "Throughput.h"


int main(int argc, const char **argv) {
	
	Throughput tp;

	if ( (std::string)argv[1] == "generate_lwB4Data" ){
		tp.generate_lwB4Data(argc,argv);
	}else {
		if ( tp.readConfigFile(CONFIGFILE) < 0 )
    	return -1;
		if ( tp.readlwB4Data(LWB4DATAFILE) < 0 )
			return -1;
		if ( tp.readCmdLine(argc,argv) < 0 )
			return -1;
		if ( tp.init(argv[0], LEFTPORT, RIGHTPORT) < 0)
			return -1;
		tp.measure(LEFTPORT, RIGHTPORT);
	}

	
	
	return 0;
}