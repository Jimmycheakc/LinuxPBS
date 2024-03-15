
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

void udpclient::processmonitordata (const char* data, std::size_t length) 
{
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
		case CmdMonitorEnquiry:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->FnSendMyStatusToMonitor();
			break;
		}
		case CmdMonitorFeeTest:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			break;
		}
		case CmdMonitorOutput:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			std::vector<std::string> dataTokens;
			std::stringstream ss(pField.Field(3));
			std::string token;

			while (std::getline(ss, token, ','))
			{
				dataTokens.push_back(token);
			}

			if (dataTokens.size() == 2)
			{
				std::cout << "pinNum : " << dataTokens[0] << std::endl;
				std::cout << "pinValue : " << dataTokens[1] << std::endl;
				int pinNum = std::stoi(dataTokens[0]);
				int pinValue = std::stoi(dataTokens[1]);
				int actualDIOPinNum = DIO::getInstance()->FnGetOutputPinNum(pinNum);

				if ((actualDIOPinNum != 0) && (pinValue == 0 || pinValue == 1))
				{
					GPIOManager::getInstance()->FnGetGPIO(actualDIOPinNum)->FnSetValue(pinValue);
				}
				else
				{
					operation::getInstance()->writelog("Invalid DIO", "UDP");
				}
			}
			break;
		}
		case CmdDownloadIni:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("download INI file","UDP");

			if (operation::getInstance()->CopyIniFile(pField.Field(0), pField.Field(3)))
			{
				operation::getInstance()->FnSendCmdDownloadIniAckToMonitor(true);
			}
			else
			{
				operation::getInstance()->FnSendCmdDownloadIniAckToMonitor(false);
			}
			break;
		}
		case CmdDownloadParam:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("download Parameter","UDP");
			operation::getInstance()->m_db->downloadparameter();

			if (db::getInstance()->FnGetDatabaseErrorFlag() == 0)
			{
				operation::getInstance()->m_db->loadParam();
				operation::getInstance()->FnSendCmdDownloadParamAckToMonitor(true);
			}
			else
			{
				operation::getInstance()->FnSendCmdDownloadParamAckToMonitor(false);
			}
			break;
		}
		case CmdMonitorSyncTime:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->FnSyncCentralDBTime();
			break;
		}
		default:
			break;
	}

}

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
		case CmdStopStationSoftware:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			std::exit(0);
			break;
		}
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
			if (i != operation::getInstance()->tProcess.gbcarparkfull) {
				operation::getInstance()->tProcess.gbcarparkfull = i;
				if (i == 0) {
					string sIUNo = operation:: getInstance()->tEntry.sIUTKNo; 
					if (operation::getInstance()->tProcess.gbLoopApresent == true and sIUNo != "" ){
						operation::getInstance()->PBSEntry(sIUNo);
					}
				}
			}
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

