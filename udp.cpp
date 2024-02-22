
#include "udp.h"
#include "ce_time.h"
#include <iostream>
#include <cstdlib>
#include <string>
#include "dio.h"
#include "gpio.h"
#include "parsedata.h"
#include "operation.h"
#include "db.h"
#include "log.h"


void udpclient::processdata (const char* data, std::size_t length) 
{
    // Your processing logic here
    // Example: Print the received data

    int rxcmd;
	int n,i;
	string sData;
	//-------

     ParseData pField('[',']','|');
     n=pField.Parse(data);
	 
	 if(n < 4){
		//std::stringstream dbss;
		//dbss << "URX: invalid number of arguments" ;
    	//Logger::getInstance()->FnLog(dbss.str(), "", "UDP");
		return;
	}
	
    rxcmd = std::stoi(pField.Field(2));
    switch(rxcmd)
    {
		case CmdStatusEnquiry:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->Sendmystatus();
			break;
		}
		case CmdStatusOnline:
			break;
		case CmdFeeTest:
			break;
		case CmdUpdateSeason:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->m_db->downloadseason();
			break;
		}
		case CmdDownloadMsg:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("download LED message","UDP");
			operation::getInstance()->m_db->downloadledmessage();
			operation::getInstance()->m_db->loadmessage();
			break;
		}
		case CmdDownloadTariff:
			break;
		case CmdDownloadHoliday:
			break;
		case CmdUpdateParam:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("download Parameter","UDP");
			operation::getInstance()->m_db->downloadparameter();
			operation::getInstance()->m_db->loadParam();
			break;
		}
		case CmdDownloadtype:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("download Vheicle Type","UDP");
			operation::getInstance()->m_db->downloadvehicletype();
			operation::getInstance()->m_db->loadvehicletype();
			break;
		}
		case CmdOpenBarrier:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("open barrier from PMS","UDP");
			operation::getInstance()->Openbarrier();
			break;
		}
		case CmdSetTime:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->FnSyncCentralDBTime();
			break;
		}
		case CmdCarparkfull:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			i=stoi(pField.Field(3));
			operation::getInstance()->tProcess.gbcarparkfull = i;
			break;
		}
		case CmdClearSeason:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("Clear Local season.","UDP");
			operation::getInstance()->m_db->clearseason();
			break;
		}
		case CmdUpdateSetting:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("download station set up","UDP");
			operation::getInstance()->m_db->downloadstationsetup();
			operation::getInstance()->m_db->loadstationsetup();
			break;
		}
		case CmdSetLotCount:
			break;
		case CmdAvailableLots:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			sData = pField.Field(3);
			operation::getInstance()->ShowTotalLots(sData);
			break;
		}
		case CmdSetDioOutput:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			int pinNum = std::stoi(pField.Field(3));
			int pinValue = std::stoi(pField.Field(4));
			int actualDIOPinNum = DIO::getInstance()->FnGetOutputPinNum(pinNum);

			if ((actualDIOPinNum != 0) && (pinValue == 0 || pinValue == 1))
			{
				GPIOManager::getInstance()->FnGetGPIO(actualDIOPinNum)->FnSetValue(pinValue);
			}
			else
			{
				operation::getInstance()->writelog("Invalid DIO", "UDP");
			}
			break;
		}
		default:
			break;
	}
}

