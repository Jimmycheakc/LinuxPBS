
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
#include "common.h"

void udpclient::processmonitordata (const char* data, std::size_t length) 
{
	try
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
	catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
}

void udpclient::processdata (const char* data, std::size_t length) 
{
	try
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
		//---------
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
			{
				operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");

				// If system current state is offline
				if (operation::getInstance()->tProcess.giSystemOnline != 0)
				{
					std::stringstream ss;
					ss << "Status: " << (operation::getInstance()->tProcess.giSystemOnline == 0) ? "Online" : "Offline";
					operation::getInstance()->writelog(ss.str(), "UDP");

					// Set the system current state to online
					operation::getInstance()->tProcess.giSystemOnline = 0;

					// Check and move the offline data
					if (operation::getInstance()->tProcess.glNoofOfflineData > 0)
					{
						db::getInstance()->moveOfflineTransToCentral();
					}
				}
				operation::getInstance()->SendMsg2Server("99", "");
				break;
			}
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
				operation::getInstance()->m_db->loadExitmessage();
				if (operation::getInstance()->tProcess.gbcarparkfull.load() == false){
					operation::getInstance()->tProcess.setIdleMsg(0, operation::getInstance()->tMsg.Msg_DefaultLED[0]);
					operation::getInstance()->tProcess.setIdleMsg(1, operation::getInstance()->tMsg.Msg_Idle[1]);
				}
				break;
			}
			case CmdFeeTest:
			{
				operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
				operation::getInstance()->writelog("Fee test command","UDP");
				std::vector<std::string> tmpStr;
				boost::algorithm::split(tmpStr, pField.Field(3), boost::algorithm::is_any_of(","));
				float parkingfee; 
				parkingfee = operation::getInstance()->m_db->CalFeeRAM2G(tmpStr[0],tmpStr[1],std::stoi(tmpStr[2]));
				if (parkingfee >= 0)
				{
					sData = pField.Field(3);
					sData = sData + "," + Common::getInstance()->SetFeeFormat(parkingfee) + ", Fee OK";
					operation::getInstance()->SendMsg2Server("302", sData);
				}
				
				break;

			}
			case CmdDownloadXTariff:
			{
				operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
				int ret;
				operation::getInstance()->writelog("download XTariff","UDP");
				ret = operation::getInstance()->m_db->downloadxtariff(operation::getInstance()->tParas.giGroupID,operation::getInstance()->tParas.giSite, 0);
				if (ret > 0)
				{
					std::stringstream ss;
					ss << "Download " << ret << " XTariff";
					operation::getInstance()->SendMsg2Server("99", ss.str());
					operation::getInstance()->m_db->LoadXTariff();
					//int iAutoDebit;
					//float sAmt;
					//operation::getInstance()->m_db->GetXTariff(iAutoDebit, sAmt, 0);
					//operation::getInstance()->writelog("Autocharge:"+std::to_string(iAutoDebit), "UDP");
					//operation::getInstance()->writelog("ChargeAmt:"+std::to_string(sAmt), "UDP");
				}
				break;
			}
			case CmdDownloadTariff:
			{
				operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
				int ret;
				operation::getInstance()->writelog("download Tariff Type Info","UDP");
				ret = operation::getInstance()->m_db->downloadtarifftypeinfo();
				if (ret > 0)
				{
					std::stringstream ss;
					ss << "Download " << ret << " Tariff type info";
					operation::getInstance()->m_db->LoadTariffTypeInfo();
				}
				//---------
				operation::getInstance()->writelog("download Tariff","UDP");
				ret = operation::getInstance()->m_db->downloadtariffsetup(operation::getInstance()->tParas.giGroupID,operation::getInstance()->tParas.giSite, 0);
				if (ret > 0)
				{
					std::stringstream ss;
					ss << "Download " << ret << " Tariff";
					operation::getInstance()->SendMsg2Server("99", ss.str());
					operation::getInstance()->m_db->LoadTariff();
				}
				break;
			}
			case CmdDownloadHoliday:
			{
				operation::getInstance()->writelog("Received data:"+ std::string(data,length), "UDP");
				operation::getInstance()->writelog("download Holiday", "UDP");
				int ret = operation:: getInstance()-> m_db->downloadholidaymst(1);
				if (ret > 0)
				{
					std::stringstream ss;
					ss << "Download " << ret << " Holiday";
					operation::getInstance()->SendMsg2Server("99", ss.str());
				}
				operation::getInstance()->m_db->LoadHoliday();
				break;
			}
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
				operation::getInstance()->ManualOpenBarrier(true);
				break;
			}
			case CmdCloseBarrier:
			{
				operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
				operation::getInstance()->writelog("Close barrier from PMS","UDP");

				operation::getInstance()->ManualCloseBarrier();
				if (db::getInstance()->writeparameter2local("LockBarrier", "0") == 0)
				{
					// Update it when update/insert the parameter successfully
					operation::getInstance()->writelog("Update the parameter 'LockBarrier' successfully", "UDP");
					operation::getInstance()->tParas.gbLockBarrier = false;
				}
				operation::getInstance()->SendMsg2Server("99", "Close Barrier");
				break;
			}
			case CmdContinueOpenBarrier:
			{
				operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
				operation::getInstance()->writelog("Continue open barrier from PMS","UDP");

				operation::getInstance()->continueOpenBarrier();
				if (db::getInstance()->writeparameter2local("LockBarrier", "1") == 0)
				{
					// Update it when update/insert the parameter successfully
					operation::getInstance()->writelog("Update the parameter 'LockBarrier' successfully", "UDP");
					operation::getInstance()->tParas.gbLockBarrier = true;
				}
				operation::getInstance()->SendMsg2Server("99", "Continue Open Barrier");
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
			case CmdTimeForNoEntry:
			{
				operation::getInstance()->writelog("Received data:" + std::string(data, length), "UDP");
				
				if (operation::getInstance()->tProcess.gbLoopApresent.load() == true) {
					std::vector<std::string> tmpStr;
					boost::algorithm::split(tmpStr, pField.Field(3), boost::algorithm::is_any_of(","));
					//--------
					operation::getInstance()->tExit.sEntryTime = tmpStr[1];
					operation::getInstance()->writelog("Received Entry time: " + operation::getInstance()->tExit.sEntryTime + " from PMS.", "UDP");
					operation::getInstance()->tExit.bNoEntryRecord = 0;
					operation::getInstance()->ReceivedEntryRecord();

				}else {
					operation::getInstance()->writelog("No Vehicle on the Loop.", "DB");

				}
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
			case CmdBroadcastSaveTrans:
			{
				if (pField.Field(3).find("Entry OK") != std::string::npos) {
					operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
					string stnid = "," + pField.Field(1) + ",";
					string gsZoneEntries = operation::getInstance()->tParas.gsZoneEntries;
					if (gsZoneEntries.find(stnid) != std::string::npos)
					{
						std::vector<std::string> tmpStr;
						boost::algorithm::split(tmpStr, pField.Field(3), boost::algorithm::is_any_of(","));
						db::getInstance()->insertbroadcasttrans (pField.Field(1), tmpStr[0]);
					}
				}else{
					if (pField.Field(3).find("Exit OK") != std::string::npos) {
						operation::getInstance()->writelog("Received data:"+std::string(data,length), "UDP");
						std::vector<std::string> tmpStr;
						boost::algorithm::split(tmpStr, pField.Field(3), boost::algorithm::is_any_of(","));
						db::getInstance()->UpdateLocalEntry(tmpStr[0]);
					}
				}
				break;
			}
			default:
				break;
		}
	}
	catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }

}


bool udpclient::FnGetMonitorStatus()
{
	return monitorStatus_;
}

void udpclient::startreceive()
{
    socket_.async_receive_from(buffer(data_, max_length), senderEndpoint_, boost::asio::bind_executor(strand_, [this](const boost::system::error_code& error, std::size_t bytes_received)
    {
        if (!error)
        {
            std::string sender_ip = senderEndpoint_.address().to_string();  
				
		//	operation::getInstance()->writelog("remove IP:" + sender_ip, "UDP");
		//	operation::getInstance()->writelog("Local IP:"+ operation:: getInstance()->tParas.gsLocalIP, "UDP");  
				         
            if (sender_ip != operation:: getInstance()->tParas.gsLocalIP) {
                if (socket_.local_endpoint().port() == 2001)
                {
                    processdata(data_, bytes_received);
                }
                else if (socket_.local_endpoint().port() == 2008)
                {
                    processmonitordata(data_, bytes_received);
                }
            }
        }
        else
        {
         	std::stringstream dbss;
            dbss << "Error receiving message: " << error.message() ;
            Logger::getInstance()->FnLog(dbss.str(), "", "UDP");
        }

        startreceive();  // Continue with the next receive operation
    }));
}

	