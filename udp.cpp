
#include "udp.h"
#include "ce_time.h"
#include <iostream>
#include <cstdlib>
#include <string>
#include "parsedata.h"


void udpinit(const std::string ServerIP, unsigned short ServerPort,unsigned short LocalPort) {

    try {
        io_service ioService;
        udpclient udpclient(ioService, ServerIP, ServerPort,LocalPort);
        udpclient.startsend("Create New UDP");
        ioService.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

}

void udpclient::processdata (const char* data, std::size_t length) 
{
    // Your processing logic here
    // Example: Print the received data
    int n,i, result;
    int rxcmd;

    std::cout << "Received data12: " << std::string(data, length) << std::endl;
     
     ParseData pField('[',']','|');
     n=pField.Parse(data);
     rxcmd = std::stoi(pField.Field(2));
     
    switch(rxcmd)
    {
    case CmdStatusEnquiry:
		break;
	case CmdStatusOnline:
		break;
	case CmdFeeTest:
		break;
	case CmdUpdateSeason:
		break;
	case CmdDownloadMsg:
		break;
	case CmdDownloadTariff:
		break;
		
	case CmdDownloadHoliday:
	
		break;
	case CmdUpdateParam:
	
		break;
	case CmdDownloadtype:

		break;
	case CmdOpenBarrier:
		
		break;
	case CmdSetTime:
		break;
	case CmdCarparkfull:

		i=stoi(pField.Field(3));
		if(i==0)
		{
		}
		else if(i==1)
		{
		}
		break;
	case CmdClearSeason:
		break;
	case CmdUpdateSetting:
		break;
	case CmdSetLotCount:
		break;
	default:
		//("URX: unknown command");
		break;
	}
}

