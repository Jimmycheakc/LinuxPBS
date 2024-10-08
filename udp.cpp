
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
#include "lcd.h"
#include "log.h"
#include "version.h"

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
		case CmdMonitorStatus:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			int status = std::stoi(pField.Field(3));
			if (status == 1)
			{
				monitorStatus_ = true;
			}
			else
			{
				monitorStatus_ = false;
			}
			break;
		}
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
					if (GPIOManager::getInstance()->FnGetGPIO(actualDIOPinNum) != nullptr)
					{
						GPIOManager::getInstance()->FnGetGPIO(actualDIOPinNum)->FnSetValue(pinValue);
					}
					else
					{
						operation::getInstance()->writelog("Nullptr, Invalid DIO", "UDP");
					}
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
		case CmdStopStationSoftware:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->SendMsg2Monitor("11", "99");

			std::exit(0);
			break;
		}
		case CmdMonitorStationVersion:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->SendMsg2Monitor("313", SW_VERSION);
			break;
		}
		case CmdMonitorGetStationCurrLog:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->FnSendCmdGetStationCurrLogToMonitor();
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
			operation::getInstance()->SendMsg2Server("09","11Stopping...");
			operation::getInstance()->writelog("Exit by PMS", "UDP");

			// Display Station Stopped on LCD
			std::string LCDMsg = "Station Stopped!";
        	char* sLCDMsg = const_cast<char*>(LCDMsg.data());
			LCD::getInstance()->FnLCDClearDisplayRow(1);
        	LCD::getInstance()->FnLCDDisplayRow(1, sLCDMsg);

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
			int ret = operation::getInstance()->m_db->downloadseason();
			if (ret > 0)
			{
				std::stringstream ss;
				ss << "Download " << ret << " Season";
				operation::getInstance()->SendMsg2Server("99", ss.str());
			}
			break;
		}
		case CmdDownloadMsg:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("download LED message","UDP");
			int ret = operation::getInstance()->m_db->downloadledmessage();
			if (ret > 0)
			{
				std::stringstream ss;
				ss << "Download " << ret << " Messages";
				operation::getInstance()->SendMsg2Server("99", ss.str());
			}
			operation::getInstance()->m_db->loadmessage();
			if (operation::getInstance()->tProcess.gbcarparkfull.load() == false){
				operation::getInstance()->tProcess.setIdleMsg(0, operation::getInstance()->tMsg.Msg_DefaultLED[0]);
				operation::getInstance()->tProcess.setIdleMsg(1, operation::getInstance()->tMsg.Msg_Idle[1]);
			}
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
			int ret = operation::getInstance()->m_db->downloadparameter();
			if (ret > 0)
			{
				std::stringstream ss;
				ss << "Download " << ret << " Parameter";
				operation::getInstance()->SendMsg2Server("99", ss.str());
			}
			operation::getInstance()->m_db->loadParam();
			break;
		}
		case CmdDownloadtype:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("download Vehicle Type","UDP");
			int ret = operation::getInstance()->m_db->downloadvehicletype();
			if (ret > 0)
			{
				std::stringstream ss;
				ss << "Download " << ret << " Vehicle Type";
				operation::getInstance()->SendMsg2Server("99", ss.str());
			}
			operation::getInstance()->m_db->loadvehicletype();
			break;
		}
		case CmdOpenBarrier:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("open barrier from PMS","UDP");
			operation::getInstance()->ManualOpenBarrier();
			break;
		}
		case CmdCloseBarrier:
		{
			operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
			operation::getInstance()->writelog("Close barrier from PMS","UDP");
			operation::getInstance()->ManualCloseBarrier();
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
			bool bCarparkFull = static_cast<bool>(i);
			if (bCarparkFull != operation::getInstance()->tProcess.gbcarparkfull.load()) {
				operation::getInstance()->tProcess.gbcarparkfull.store(bCarparkFull);
				if (bCarparkFull == false) {
					string sIUNo = operation:: getInstance()->tEntry.sIUTKNo; 
					if (operation::getInstance()->tProcess.gbLoopApresent.load() == true and sIUNo != "" ){
						operation::getInstance()->PBSEntry(sIUNo);
					}
					operation::getInstance()->tProcess.setIdleMsg(0, operation::getInstance()->tMsg.Msg_DefaultLED[0]);
					operation::getInstance()->tProcess.setIdleMsg(1, operation::getInstance()->tMsg.Msg_Idle[1]);
				}
				else {
					operation::getInstance()->tProcess.setIdleMsg(0, operation::getInstance()->tMsg.Msg_CarParkFull2LED[0]);
					operation::getInstance()->tProcess.setIdleMsg(1, operation::getInstance()->tMsg.Msg_CarParkFull2LED[1]);
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
			int ret = operation::getInstance()->m_db->downloadstationsetup();
			if (ret > 0)
			{
				std::stringstream ss;
				ss << "Download " << ret << " Station Setup";
				operation::getInstance()->SendMsg2Server("99", ss.str());
			}
			operation::getInstance()->m_db->loadstationsetup();
			break;
		}
		case CmdDownloadTR:
		{
			operation::getInstance()->writelog("Received data:" + std::string(data, length), "UDP");
			operation::getInstance()->writelog("download TR", "UDP");
			int ret = operation::getInstance()->m_db->downloadTR();
			if (ret > 0)
			{
				std::stringstream ss;
				ss << "Download " << ret << " TR";
				operation::getInstance()->SendMsg2Server("99", ss.str());
			}
			operation::getInstance()->m_db->loadTR();
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
				if (GPIOManager::getInstance()->FnGetGPIO(actualDIOPinNum) != nullptr)
				{
					GPIOManager::getInstance()->FnGetGPIO(actualDIOPinNum)->FnSetValue(pinValue);
				}
				else
				{
					operation::getInstance()->writelog("Nullptr, Invalid DIO", "UDP");
				}
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


bool udpclient::FnGetMonitorStatus()
{
	return monitorStatus_;
}
