#include "boost/algorithm/string.hpp"
#include <boost/filesystem.hpp>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <string>
#include <sstream>
#include "boost/asio/serial_port.hpp"
#include "common.h"
#include "event_manager.h"
#include "lcsc.h"
#include "log.h"
#include "mount.h"
#include "operation.h"

// CscPacket Class
CscPacket::CscPacket()
    : attn(0),
    code(false),
    type(0),
    len(0),
    crc(0)
{

}

CscPacket::~CscPacket()
{

}

void CscPacket::setAttentionCode(uint8_t attn)
{
    this->attn = attn;
}

uint8_t CscPacket::getAttentionCode() const
{
    return attn;
}

void CscPacket::setCode(uint8_t code)
{
    this->code =  code;
}

uint8_t CscPacket::getCode() const
{
    return code;
}

void CscPacket::setType(uint8_t type)
{
    this->type = type;
}

uint8_t CscPacket::getType() const
{
    return type;
}

void CscPacket::setLength(uint16_t len)
{
    this->len = len;
}

uint16_t CscPacket::getLength() const
{
    return len;
}

void CscPacket::setPayload(const std::vector<uint8_t>& payload)
{
    this->payload = payload;
}

std::vector<uint8_t> CscPacket::getPayload() const
{
    return payload;
}

void CscPacket::setCrc(uint16_t crc)
{
    this->crc = crc;
}

uint16_t CscPacket::getCrc()
{
    return crc;
}

std::vector<uint8_t> CscPacket::serializeWithoutCRC() const
{
    std::vector<uint8_t> data;

    // Serialize the attention code
    data.push_back(attn);

    // Serialize the code and type into a single byte
    uint8_t codeAndType = ((code << 7) | (type & 0x7F));
    data.push_back(codeAndType);

    // Serialize the length (16-bit length)
    data.push_back(len >> 8);
    data.push_back(len & 0xFF);

    // Serialize the payload
    data.insert(data.end(), payload.begin(), payload.end());

    return data;
}

std::vector<uint8_t> CscPacket::serialize() const
{
    std::vector<uint8_t> data;

    // Serialize the attention code
    data.push_back(attn);

    // Serialize the code and type into a single byte
    uint8_t codeAndType = ((code << 7) | (type & 0x7F));
    data.push_back(codeAndType);

    // Serialize the length (16-bit length)
    data.push_back(len >> 8);
    data.push_back(len & 0xFF);

    // Serialize the payload
    data.insert(data.end(), payload.begin(), payload.end());

    // Serialize the CRC (16-bit length)
    data.push_back(crc >> 8);
    data.push_back(crc & 0xFF);

    return data;
}

void CscPacket::deserialize(const std::vector<uint8_t>& data)
{
    auto it = data.begin();

    // Deserialize the attention code
    attn = *it++;

    // Deserialize the code and type from a single byte
    uint8_t codeAndType = *it++;
    code = codeAndType & 0x80;
    type = codeAndType & 0x7F;

    // Deserialize the length (16-bit length)
    len = static_cast<uint16_t>(*it++) << 8;
    len |= static_cast<uint16_t>(*it++);

    // Deserialize the payload
    std::size_t payloadLen = len - 6;
    payload.assign(it, it + payloadLen);
    it += payloadLen;

    // Deserialize the CRC (16-bit value)
    crc = static_cast<uint16_t>(*it++) << 8;
    crc |= static_cast<uint16_t>(*it++);
}

std::string CscPacket::getMsgCscPacketOutput() const
{
    std::ostringstream oss;

    std::string msgCode = (code == 0x00) ? "CMD" : "RESP";
    std::string msgType = LCSCReader::getInstance()->getCommandTypeString(type);
    std::size_t payloadInBytes = payload.size();


    oss << std::endl;
    oss << std::setw(32) << std::setfill(' ') << "";
    oss << msgType << " (" << msgCode << ")" << std::endl;
    oss << std::setw(32) << std::setfill(' ') << "";
    oss << "--------------------------------------------------" << std::endl;
    oss << std::setw(32) << std::setfill(' ') << "";
    oss << "Attention Code      [  1 byte  ] [H] " << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(attn) << std::endl;
    oss << std::setw(32) << std::setfill(' ') << "";
    oss << "Code                [  1 bit   ] [H] " << std::setw(1) << std::setfill('0') << std::hex << static_cast<int>((code & 0x01)) << std::endl;
    oss << std::setw(32) << std::setfill(' ') << "";
    oss << "Type                [  2 bytes ] [H] " << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>((type & 0x7F)) << std::endl;
    oss << std::setw(32) << std::setfill(' ') << "";
    oss << "Length              [  4 bytes ] [H] " << std::setw(4) << std::setfill('0') << std::hex << static_cast<int>(len) << std::endl;
    oss << std::setw(32) << std::setfill(' ') << "";
    oss << "Payload             [" << std::setw(3) << std::setfill(' ') << std::dec << static_cast<int>(payloadInBytes) << " bytes ] [H] " << Common::getInstance()->FnGetDisplayVectorCharToHexString(payload) << std::endl;
    oss << std::setw(32) << std::setfill(' ') << "";
    oss << "CRC                 [  4 bytes ] [H] " << std::setw(4) << std::setfill('0') << std::hex << static_cast<int>(crc) << std::endl;

    return oss.str();
}

void CscPacket::clear()
{
    attn = 0;
    code = false;
    type = 0;
    len = 0;
    payload.clear();
    crc = 0;
}


// LCSCReader Class

LCSCReader* LCSCReader::lcscReader_ = nullptr;
std::mutex LCSCReader::mutex_;
std::mutex LCSCReader::currentCmdMutex_;

LCSCReader::LCSCReader()
    : ioContext_(),
    strand_(boost::asio::make_strand(ioContext_)),
    workGuard_(boost::asio::make_work_guard(ioContext_)),
    rspTimer_(ioContext_),
    serialWriteDelayTimer_(ioContext_),
    serialWriteTimer_(ioContext_),
    logFileName_("lcsc"),
    currentState_(LCSCReader::STATE::IDLE),
    write_in_progress_(false),
    rxState_(LCSCReader::RX_STATE::RX_START),
    lastSerialReadTime_(std::chrono::steady_clock::now()),
    continueReadFlag_(false),
    currentCmd(LCSCReader::LCSC_CMD::GET_STATUS_CMD),
    HasCDFileToUpload_(false),
    LastCDUploadDate_(0),
    LastCDUploadTime_(0),
    uploadLcscFileName_(""),
    currentUploadLcscFilesState_(LCSCReader::UPLOAD_LCSC_FILES_STATE::IDLE),
    lastDebitTime_("")
{
    resetRxBuffer();
    
    aes_key = 
    {
        0x92, 0xCE, 0xE9, 0x2D, 0x81, 0xE5, 0x4A, 0xEB,
        0xB4, 0x1F, 0x7F, 0x56, 0x2A, 0xF2, 0x7A, 0xDF,
        0x34, 0x65, 0xAA, 0xF4, 0x27, 0x43, 0xF4, 0x3D,
        0xDE, 0x97, 0xE1, 0x86, 0x78, 0x39, 0xEC, 0x0B
    };
}

LCSCReader* LCSCReader::getInstance()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (lcscReader_ == nullptr)
    {
        lcscReader_ = new LCSCReader();
    }
    return lcscReader_;
}

int LCSCReader::FnLCSCReaderInit(unsigned int baudRate, const std::string& comPortName)
{
    int ret = static_cast<int>(mCSCEvents::iCommPortError);

    pSerialPort_ = std::make_unique<boost::asio::serial_port>(ioContext_, comPortName);

    try
    {
        pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

        Logger::getInstance()->FnCreateLogFile(logFileName_);

        if (!(boost::filesystem::exists(LOCAL_LCSC_FOLDER_PATH)))
        {
            if (!(boost::filesystem::create_directories(LOCAL_LCSC_FOLDER_PATH)))
            {
                std::ostringstream oss;
                oss << "Failed to create directory: " << LOCAL_LCSC_FOLDER_PATH;
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            }
        }
        
        std::ostringstream oss;
        if (pSerialPort_->is_open())
        {
            oss << "Successfully open serial port for LCSC Reader Communication: " << comPortName;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            Logger::getInstance()->FnLog("LCSC Reader initialization completed.");

            startIoContextThread();
            startRead();
            FnSendGetLoginCmd();
            ret = 1;
        }
        else
        {
            oss << "Failed to open serial port for LCSC Reader Communication: " << comPortName;
            Logger::getInstance()->FnLog(oss.str());
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
        }

    }
    catch (const boost::filesystem::filesystem_error& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Boost Asio Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
    }
    catch (const boost::system::system_error& e)
    {
        std::stringstream ss;
        ss << __func__ << ", Boost Asio Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
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

    return ret;
}

void LCSCReader::startIoContextThread()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (!ioContextThread_.joinable())
    {
        ioContextThread_ = std::thread([this] () { ioContext_.run(); });
    }
}

void LCSCReader::FnLCSCReaderClose()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    workGuard_.reset();
    ioContext_.stop();

    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }
}

void LCSCReader::setCurrentCmd(LCSCReader::LCSC_CMD cmd)
{
    std::unique_lock<std::mutex> lock(currentCmdMutex_);

    currentCmd = cmd;
}

LCSCReader::LCSC_CMD LCSCReader::getCurrentCmd()
{
    std::unique_lock<std::mutex> lock(currentCmdMutex_);

    return currentCmd;
}

std::string LCSCReader::getCommandString(LCSCReader::LCSC_CMD cmd)
{
    std::string returnStr = "";

    switch (cmd)
    {
        case LCSC_CMD::GET_STATUS_CMD:
        {
            returnStr = "GET_STATUS_CMD";
            break;
        }
        case LCSC_CMD::LOGIN_1:
        {
            returnStr = "LOGIN_1";
            break;
        }
        case LCSC_CMD::LOGIN_2:
        {
            returnStr = "LOGIN_2";
            break;
        }
        case LCSC_CMD::GET_CARD_ID:
        {
            returnStr = "GET_CARD_ID";
            break;
        }
        case LCSC_CMD::CARD_BALANCE:
        {
            returnStr = "CARD_BALANCE";
            break;
        }
        case LCSC_CMD::CARD_DEDUCT:
        {
            returnStr = "CARD_DEDUCT";
            break;
        }
        case LCSC_CMD::CARD_RECORD:
        {
            returnStr = "CARD_RECORD";
            break;
        }
        case LCSC_CMD::CARD_FLUSH:
        {
            returnStr = "CARD_FLUSH";
            break;
        }
        case LCSC_CMD::GET_TIME:
        {
            returnStr = "GET_TIME";
            break;
        }
        case LCSC_CMD::SET_TIME:
        {
            returnStr = "SET_TIME";
            break;
        }
        case LCSC_CMD::UPLOAD_CFG_FILE:
        {
            returnStr = "UPLOAD_CFG_FILE";
            break;
        }
        case LCSC_CMD::UPLOAD_CIL_FILE:
        {
            returnStr = "UPLOAD_CIL_FILE";
            break;
        }
        case LCSC_CMD::UPLOAD_BL_FILE:
        {
            returnStr = "UPLOAD_BL_FILE";
            break;
        }
    }

    return returnStr;
}

std::string LCSCReader::getCommandTypeString(uint8_t type)
{
    std::string returnStr = "";

    switch (static_cast<LCSC_CMD_TYPE>(type))
    {
        case LCSC_CMD_TYPE::AUTH_LOGIN1:
        {
            returnStr = "AUTH_LOGIN1";
            break;
        }
        case LCSC_CMD_TYPE::AUTH_LOGIN2:
        {
            returnStr = "AUTH_LOGIN2";
            break;
        }
        case LCSC_CMD_TYPE::AUTH_LOGOUT:
        {
            returnStr = "AUTH_LOGOUT";
            break;
        }
        case LCSC_CMD_TYPE::CARD_ID:
        {
            returnStr = "CARD_ID";
            break;
        }
        case LCSC_CMD_TYPE::CARD_BALANCE:
        {
            returnStr = "CARD_BALANCE";
            break;
        }
        case LCSC_CMD_TYPE::CARD_DEDUCT:
        {
            returnStr = "CARD_DEDUCT";
            break;
        }
        case LCSC_CMD_TYPE::CARD_RECORD:
        {
            returnStr = "CARD_RECORD";
            break;
        }
        case LCSC_CMD_TYPE::CARD_FLUSH:
        {
            returnStr = "CARD_FLUSH";
            break;
        }
        case LCSC_CMD_TYPE::BL_UPLOAD:
        {
            returnStr = "BL_UPLOAD";
            break;
        }
        case LCSC_CMD_TYPE::CIL_UPLOAD:
        {
            returnStr = "CIL_UPLOAD";
            break;
        }
        case LCSC_CMD_TYPE::CFG_UPLOAD:
        {
            returnStr = "CFG_UPLOAD";
            break;
        }
        case LCSC_CMD_TYPE::RSA_UPLOAD:
        {
            returnStr = "RSA_UPLOAD";
            break;
        }
        case LCSC_CMD_TYPE::CLK_SET:
        {
            returnStr = "CLK_SET";
            break;
        }
        case LCSC_CMD_TYPE::CLK_GET:
        {
            returnStr = "CLK_GET";
            break;
        }
        case LCSC_CMD_TYPE::LOG_READ:
        {
            returnStr = "LOG_READ";
            break;
        }
        case LCSC_CMD_TYPE::FW_UPDATE:
        {
            returnStr = "FW_UPDATE";
            break;
        }
        case LCSC_CMD_TYPE::GET_STATUS:
        {
            returnStr = "GET_STATUS";
            break;
        }
        
    }

    return returnStr;
}

void LCSCReader::enqueueCommand(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> data)
{
    if (!pSerialPort_ || !(pSerialPort_->is_open()))
    {
        Logger::getInstance()->FnLog("Serial Port not open, unable to enqueue command.", logFileName_, "LCSC");
        return;
    }

    std::ostringstream oss;
    oss << "Sending LCSC Command to queue: " << getCommandString(cmd);
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

    {
        std::lock_guard<std::mutex> lock(commandQueueMutex_);
        commandQueue_.emplace_back(cmd, data);
    }

    if (currentState_ == STATE::IDLE)
    {
        boost::asio::post(strand_, [this]() {
            checkCommandQueue();
        });
    }
}

void LCSCReader::enqueueCommandToFront(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> data)
{
    if (!pSerialPort_ || !(pSerialPort_->is_open()))
    {
        Logger::getInstance()->FnLog("Serial Port not open, unable to enqueue command.", logFileName_, "LCSC");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(commandQueueMutex_);

        // Check if the front command is the same as the one being enqueued
        if (!commandQueue_.empty() && commandQueue_.front().cmd == cmd) 
        {
            Logger::getInstance()->FnLog("Same command is already at the front of the queue, skipping enqueue.", logFileName_, "LCSC");
            return;
        }

        std::ostringstream oss;
        oss << "Sending LCSC Command to the front of queue: " << getCommandString(cmd);
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

        commandQueue_.emplace_front(cmd, data);
    }

    if (currentState_ == STATE::IDLE)
    {
        boost::asio::post(strand_, [this]() {
            checkCommandQueue();
        });
    }
}

void LCSCReader::enqueueChunkCommand(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> data)
{
    if (!pSerialPort_ && !(pSerialPort_->is_open()))
    {
        Logger::getInstance()->FnLog("Serial Port not open, unable to enqueue command.", logFileName_, "LCSC");
        return;
    }

    std::ostringstream oss;
    oss << "Sending LCSC Command to queue: " << getCommandString(cmd);
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

    {
        std::lock_guard<std::mutex> lock(chunkCommandQueueMutex_);
        chunkCommandQueue_.emplace_back(cmd, data);
    }
}

void LCSCReader::checkCommandQueue()
{
    bool hasCommand = false;
    LCSC_CMD cmd;

    {
        std::lock_guard<std::mutex> lock(commandQueueMutex_);
        if (!commandQueue_.empty())
        {
            hasCommand = true;
            CommandWithData cmdData = commandQueue_.front();
            cmd = cmdData.cmd;
        }
    }

    if (hasCommand)
    {
        if (isChunkedCommand(cmd))
        {
            boost::asio::post(strand_, [this]() {
                processEvent(EVENT::CHUNK_COMMAND_ENQUEUED);
            });
        }
        else
        {
            boost::asio::post(strand_, [this]() {
                processEvent(EVENT::COMMAND_ENQUEUED);
            });
        }
    }
}

void LCSCReader::clearChunkCommandQueue()
{
    std::lock_guard<std::mutex> lock(chunkCommandQueueMutex_);

    std::ostringstream oss;
    oss << "Clearing chunkCommandQueue_ | Size before: " << chunkCommandQueue_.size();
    chunkCommandQueue_.clear();
    oss << ", Size after: " << chunkCommandQueue_.size();

    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
}


const LCSCReader::StateTransition LCSCReader::stateTransitionTable[static_cast<int>(LCSCReader::STATE::STATE_COUNT)] = 
{
    {STATE::IDLE,
    {
        {EVENT::COMMAND_ENQUEUED                        , &LCSCReader::handleIdleState                                  , STATE::SENDING_REQUEST_ASYNC                  },
        {EVENT::CHUNK_COMMAND_ENQUEUED                  , &LCSCReader::handleIdleState                                  , STATE::SENDING_CHUNK_COMMAND_REQUEST_ASYNC    }
    }},
    {STATE::SENDING_REQUEST_ASYNC,
    {
        {EVENT::WRITE_COMPLETED                         , &LCSCReader::handleSendingRequestAsyncState                   , STATE::WAITING_FOR_RESPONSE                   },
        {EVENT::WRITE_FAILED                            , &LCSCReader::handleSendingRequestAsyncState                   , STATE::IDLE                                   },
        {EVENT::WRITE_TIMEOUT                           , &LCSCReader::handleSendingRequestAsyncState                   , STATE::IDLE                                   }
    }},
    {STATE::WAITING_FOR_RESPONSE,
    {
        {EVENT::RESPONSE_TIMEOUT                        , &LCSCReader::handleWaitingForResponseState                    , STATE::IDLE                                   },
        {EVENT::RESPONSE_RECEIVED                       , &LCSCReader::handleWaitingForResponseState                    , STATE::IDLE                                   }
    }},
    {STATE::SENDING_CHUNK_COMMAND_REQUEST_ASYNC,
    {
        {EVENT::WRITE_COMPLETED                         , &LCSCReader::handleSendingChunkCommandRequestAsyncState       , STATE::WAITING_FOR_CHUNK_COMMAND_RESPONSE     },
        {EVENT::WRITE_FAILED                            , &LCSCReader::handleSendingChunkCommandRequestAsyncState       , STATE::IDLE                                   },
        {EVENT::WRITE_TIMEOUT                           , &LCSCReader::handleSendingChunkCommandRequestAsyncState       , STATE::IDLE                                   }
    }},
    {STATE::WAITING_FOR_CHUNK_COMMAND_RESPONSE,
    {
        {EVENT::RESPONSE_TIMEOUT                        , &LCSCReader::handleWaitingForChunkCommandResponseState        , STATE::IDLE                                   },
        {EVENT::RESPONSE_RECEIVED                       , &LCSCReader::handleWaitingForChunkCommandResponseState        , STATE::WAITING_FOR_CHUNK_COMMAND_RESPONSE     },
        {EVENT::SEND_NEXT_CHUNK_COMMAND                 , &LCSCReader::handleWaitingForChunkCommandResponseState        , STATE::SENDING_CHUNK_COMMAND_REQUEST_ASYNC    },
        {EVENT::ALL_CHUNK_COMMAND_COMPLETED             , &LCSCReader::handleWaitingForChunkCommandResponseState        , STATE::IDLE                                   },
        {EVENT::CHUNK_COMMAND_ERROR                     , &LCSCReader::handleWaitingForChunkCommandResponseState        , STATE::IDLE                                   }
    }}
};

std::string LCSCReader::eventToString(LCSCReader::EVENT event)
{
    std::string returnStr = "Unknown Event";

    switch (event)
    {
        case EVENT::COMMAND_ENQUEUED:
        {
            returnStr = "COMMAND_ENQUEUED";
            break;
        }
        case EVENT::CHUNK_COMMAND_ENQUEUED:
        {
            returnStr = "CHUNK_COMMAND_ENQUEUED";
            break;
        }
        case EVENT::WRITE_COMPLETED:
        {
            returnStr = "WRITE_COMPLETED";
            break;
        }
        case EVENT::WRITE_FAILED:
        {
            returnStr = "WRITE_FAILED";
            break;
        }
        case EVENT::RESPONSE_TIMEOUT:
        {
            returnStr = "RESPONSE_TIMEOUT";
            break;
        }
        case EVENT::RESPONSE_RECEIVED:
        {
            returnStr = "RESPONSE_RECEIVED";
            break;
        }
        case EVENT::SEND_NEXT_CHUNK_COMMAND:
        {
            returnStr = "SEND_NEXT_CHUNK_COMMAND";
            break;
        }
        case EVENT::ALL_CHUNK_COMMAND_COMPLETED:
        {
            returnStr = "ALL_CHUNK_COMMAND_COMPLETED";
            break;
        }
        case EVENT::CHUNK_COMMAND_ERROR:
        {
            returnStr = "CHUNK_COMMAND_ERROR";
            break;
        }
        case EVENT::WRITE_TIMEOUT:
        {
            returnStr = "WRITE_TIMEOUT";
            break;
        }
    }

    return returnStr;
}

std::string LCSCReader::stateToString(LCSCReader::STATE state)
{
    std::string returnStr = "Unknown State";

    switch (state)
    {
        case STATE::IDLE:
        {
            returnStr = "IDLE";
            break;
        }
        case STATE::SENDING_REQUEST_ASYNC:
        {
            returnStr = "SENDING_REQUEST_ASYNC";
            break;
        }
        case STATE::WAITING_FOR_RESPONSE:
        {
            returnStr = "WAITING_FOR_RESPONSE";
            break;
        }
        case STATE::SENDING_CHUNK_COMMAND_REQUEST_ASYNC:
        {
            returnStr = "SENDING_CHUNK_COMMAND_REQUEST_ASYNC";
            break;
        }
        case STATE::WAITING_FOR_CHUNK_COMMAND_RESPONSE:
        {
            returnStr = "WAITING_FOR_CHUNK_COMMAND_RESPONSE";
            break;
        }
    }

    return returnStr;
}

std::string LCSCReader::getEventStringFromResponseCmdType(uint8_t respType)
{
    std::string retStr = "";

    switch (static_cast<LCSC_CMD_TYPE>(respType))
    {
        case LCSC_CMD_TYPE::AUTH_LOGIN1:
        {
            retStr = "Evt_LcscReaderLogin";
            break;
        }
        case LCSC_CMD_TYPE::AUTH_LOGIN2:
        {
            retStr = "Evt_LcscReaderLogin";
            break;
        }
        case LCSC_CMD_TYPE::AUTH_LOGOUT:
        {
            retStr = "Evt_handleLcscReaderLogout";
            break;
        }
        case LCSC_CMD_TYPE::CARD_ID:
        {
            retStr = "Evt_handleLcscReaderGetCardID";
            break;
        }
        case LCSC_CMD_TYPE::CARD_BALANCE:
        {
            retStr = "Evt_handleLcscReaderGetCardBalance";
            break;
        }
        case LCSC_CMD_TYPE::CARD_DEDUCT:
        {
            retStr = "Evt_handleLcscReaderGetCardDeduct";
            break;
        }
        case LCSC_CMD_TYPE::CARD_RECORD:
        {
            retStr = "Evt_handleLcscReaderGetCardRecord";
            break;
        }
        case LCSC_CMD_TYPE::CARD_FLUSH:
        {
            retStr = "Evt_handleLcscReaderGetCardFlush";
            break;
        }
        case LCSC_CMD_TYPE::CLK_SET:
        {
            retStr = "Evt_handleLcscReaderSetTime";
            break;
        }
        case LCSC_CMD_TYPE::CLK_GET:
        {
            retStr = "Evt_handleLcscReaderGetTime";
            break;
        }
        case LCSC_CMD_TYPE::BL_UPLOAD:
        {
            retStr = "Evt_handleLcscReaderUploadBLFile";
            break;
        }
        case LCSC_CMD_TYPE::CIL_UPLOAD:
        {
            retStr = "Evt_handleLcscReaderUploadCILFile";
            break;
        }
        case LCSC_CMD_TYPE::CFG_UPLOAD:
        {
            retStr = "Evt_handleLcscReaderUploadCFGFile";
            break;
        }
        case LCSC_CMD_TYPE::GET_STATUS:
        {
            retStr = "Evt_LcscReaderStatus";
            break;
        }
    }

    return retStr;
}

void LCSCReader::processEvent(EVENT event)
{
    boost::asio::post(strand_, [this, event]() {
        std::ostringstream oss;
        oss << "processEvent => " << eventToString(event);
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

        int currentStateIndex_ = static_cast<int>(currentState_);
        const auto& stateTransitions = stateTransitionTable[currentStateIndex_].transitions;

        bool eventHandled = false;
        for (const auto& transition : stateTransitions)
        {
            if (transition.event == event)
            {
                eventHandled = true;

                std::ostringstream oss;
                oss << "Current State : " << stateToString(currentState_);
                oss << " , Event : " << eventToString(event);
                oss << " , Event Handler : " << (transition.eventHandler ? "YES" : "NO");
                oss << " , Next State : " << stateToString(transition.nextState);
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

                if (transition.eventHandler != nullptr)
                {
                    (this->*transition.eventHandler)(event);
                }
                currentState_ = transition.nextState;

                if (currentState_ == STATE::IDLE)
                {
                    boost::asio::post(strand_, [this]() {
                        checkCommandQueue();
                    });
                }
                return;
            }
        }

        if (!eventHandled)
        {
            std::ostringstream oss;
            oss << "Event '" << eventToString(event) << "' not handled in state '" << stateToString(currentState_) << "'";
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
        }
    });
}

bool LCSCReader::isChunkedCommand(LCSCReader::LCSC_CMD cmd)
{
    bool ret = false;

    switch (cmd)
    {
        case LCSC_CMD::UPLOAD_CFG_FILE:
        case LCSC_CMD::UPLOAD_CIL_FILE:
        case LCSC_CMD::UPLOAD_BL_FILE:
        {
            ret = true;
            break;
        }
    }

    return ret;
}

bool LCSCReader::isChunkedCommandType(LCSCReader::LCSC_CMD_TYPE cmd)
{
    bool ret = false;

    switch (cmd)
    {
        case LCSC_CMD_TYPE::BL_UPLOAD:
        case LCSC_CMD_TYPE::CIL_UPLOAD:
        case LCSC_CMD_TYPE::CFG_UPLOAD:
        {
            ret = true;
        }
    }

    return ret;
}

void LCSCReader::popFromCommandQueueAndEnqueueWrite()
{
    std::lock_guard<std::mutex> lock(commandQueueMutex_);

    std::ostringstream oss;
    oss << "Command queue size: " << commandQueue_.size() << std::endl;
    if (!commandQueue_.empty())
    {
        oss << "Commands in queue: " << std::endl;
        for (const auto& cmdData : commandQueue_)
        {
            oss << "[Cmd: " << getCommandString(cmdData.cmd) << "]" << std::endl;
        }
    }
    else
    {
        oss << "Command queue is empty." << std::endl;
    }
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

    if (!commandQueue_.empty())
    {
        CommandWithData cmdData = commandQueue_.front();
        commandQueue_.pop_front();
        setCurrentCmd(cmdData.cmd);
        enqueueWrite(prepareCmd(cmdData.cmd, cmdData.data));
    }
}

void LCSCReader::popFromChunkCommandQueueAndEnqueueWrite()
{
    try
    {
        bool hasCommand = false;
        struct CommandWithData cmdData(LCSC_CMD::GET_STATUS_CMD);

        std::lock_guard<std::mutex> lock(commandQueueMutex_);

        std::ostringstream oss;
        oss << "Command queue size: " << commandQueue_.size() << std::endl;
        if (!commandQueue_.empty())
        {
            oss << "Commands in queue: " << std::endl;
            for (const auto& cmdData : commandQueue_)
            {
                oss << "[Cmd: " << getCommandString(cmdData.cmd) << "]" << std::endl;
            }
        }
        else
        {
            oss << "Command queue is empty." << std::endl;
        }
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

        {
            if (!commandQueue_.empty())
            {
                hasCommand = true;
                cmdData = commandQueue_.front();
                commandQueue_.pop_front();
            }
        }

        if (hasCommand)
        {
            setCurrentCmd(cmdData.cmd);
            auto chunksData = std::static_pointer_cast<std::vector<std::vector<uint8_t>>>(cmdData.data);

            for (const auto& chunk : *chunksData)
            {
                std::shared_ptr<void> req_data = std::make_shared<std::vector<uint8_t>>(chunk);
                enqueueChunkCommand(cmdData.cmd, req_data);
            }

            sendNextChunkCommandData();
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

void LCSCReader::sendNextChunkCommandData()
{
    std::lock_guard<std::mutex> lock(chunkCommandQueueMutex_);
    if (!chunkCommandQueue_.empty())
    {
        CommandWithData cmdData = chunkCommandQueue_.front();
        chunkCommandQueue_.pop_front();
        enqueueWrite(prepareCmd(cmdData.cmd, cmdData.data));
    }
}

void LCSCReader::handleIdleState(LCSCReader::EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (event == EVENT::COMMAND_ENQUEUED)
    {
        Logger::getInstance()->FnLog("Pop from command queue and write.", logFileName_, "LCSC");
        popFromCommandQueueAndEnqueueWrite();
        startSerialWriteTimer(NORMAL_CMD_WRITE_TIMEOUT);
    }
    else if (event == EVENT::CHUNK_COMMAND_ENQUEUED)
    {
        Logger::getInstance()->FnLog("Pop from chunk command queue and write.", logFileName_, "LCSC");
        popFromChunkCommandQueueAndEnqueueWrite();
        startSerialWriteTimer(CHUNK_CMD_WRITE_TIMEOUT);
    }
}

void LCSCReader::handleSendingRequestAsyncState(LCSCReader::EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (event == EVENT::WRITE_COMPLETED)
    {
        Logger::getInstance()->FnLog("Write completed. Start receiving response.", logFileName_, "LCSC");
        startResponseTimer();
        serialWriteTimer_.cancel();
    }
    else if (event == EVENT::WRITE_FAILED)
    {
        Logger::getInstance()->FnLog("Write failed.", logFileName_, "LCSC");
        serialWriteTimer_.cancel();
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sSendcmdfail);
    }
    else if (event == EVENT::WRITE_TIMEOUT)
    {
        Logger::getInstance()->FnLog("Received serial write timeout event in Sending Request Async State.", logFileName_, "LCSC");
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sSendcmdfail);
    }
}

void LCSCReader::handleWaitingForResponseState(LCSCReader::EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (event == EVENT::RESPONSE_TIMEOUT)
    {
        Logger::getInstance()->FnLog("Response Timer Timeout.", logFileName_, "LCSC");
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sTimeout);
    }
    else if (event == EVENT::RESPONSE_RECEIVED)
    {
        Logger::getInstance()->FnLog("Response Received. Handling the response.", logFileName_, "LCSC");
        handleReceivedCmd(getRxBuffer());
        resetRxBuffer();
    }
}

void LCSCReader::handleSendingChunkCommandRequestAsyncState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (event == EVENT::WRITE_COMPLETED)
    {
        Logger::getInstance()->FnLog("Write completed. Start receiving response.", logFileName_, "LCSC");
        startResponseTimer();
        serialWriteTimer_.cancel();
    }
    else if (event == EVENT::WRITE_FAILED)
    {
        Logger::getInstance()->FnLog("Write failed.", logFileName_, "LCSC");
        serialWriteTimer_.cancel();
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sSendcmdfail);
        clearChunkCommandQueue();
    }
    else if (event == EVENT::WRITE_TIMEOUT)
    {
        Logger::getInstance()->FnLog("Received serial write timeout event in Sending Chunk Cmd Request Async State.", logFileName_, "LCSC");
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sSendcmdfail);
        clearChunkCommandQueue();
    }
}

void LCSCReader::handleWaitingForChunkCommandResponseState(EVENT event)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (event == EVENT::RESPONSE_TIMEOUT)
    {
        Logger::getInstance()->FnLog("Response Timer Timeout.", logFileName_, "LCSC");
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sTimeout);
        clearChunkCommandQueue();
    }
    else if (event == EVENT::RESPONSE_RECEIVED)
    {
        Logger::getInstance()->FnLog("Response Received. Handling the response.", logFileName_, "LCSC");
        handleReceivedCmd(getRxBuffer());
        resetRxBuffer();
    }
    else if (event == EVENT::SEND_NEXT_CHUNK_COMMAND)
    {
        Logger::getInstance()->FnLog("Send next chunk of command data", logFileName_, "LCSC");
        sendNextChunkCommandData();
    }
    else if (event == EVENT::ALL_CHUNK_COMMAND_COMPLETED)
    {
        Logger::getInstance()->FnLog("All chunk of command data sent completely.", logFileName_, "LCSC");
    }
    else if (event == EVENT::CHUNK_COMMAND_ERROR)
    {
        Logger::getInstance()->FnLog("Chunk of command data error.", logFileName_, "LCSC");
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::rNotRespCmd);
        clearChunkCommandQueue();
    }
}

void LCSCReader::startSerialWriteTimer(int seconds)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    serialWriteTimer_.expires_from_now(boost::posix_time::seconds(seconds));
    serialWriteTimer_.async_wait(boost::asio::bind_executor(strand_, 
        std::bind(&LCSCReader::handleSerialWriteTimeout, this, std::placeholders::_1)));
}

void LCSCReader::handleSerialWriteTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (!error)
    {
        Logger::getInstance()->FnLog("Serial Timer Timeout.", logFileName_, "LCSC");
        write_in_progress_ = false;  // Reset flag to allow next write
        processEvent(EVENT::WRITE_TIMEOUT);
    }
    else if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Serial Write Timer was cancelled (likely because write completed in time).", logFileName_, "LCSC");
        return;
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Write Timer error : " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

        write_in_progress_ = false; // Reset flag to allow retry
        processEvent(EVENT::WRITE_TIMEOUT);
    }
}

void LCSCReader::startResponseTimer()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    rspTimer_.expires_from_now(boost::posix_time::seconds(2));
    rspTimer_.async_wait(boost::asio::bind_executor(strand_, 
        std::bind(&LCSCReader::handleCmdResponseTimeout, this, std::placeholders::_1)));
}

void LCSCReader::handleCmdResponseTimeout(const boost::system::error_code& error)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (!error)
    {
        Logger::getInstance()->FnLog("Response Timer Timeout.", logFileName_, "LCSC");
        processEvent(EVENT::RESPONSE_TIMEOUT);
    }
    else if (error == boost::asio::error::operation_aborted)
    {
        Logger::getInstance()->FnLog("Response Timer Cancelled.", logFileName_, "LCSC");
    }
    else
    {
        std::ostringstream oss;
        oss << "Response Timer error : " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
        processEvent(EVENT::RESPONSE_TIMEOUT);
    }
}

uint16_t LCSCReader::CRC16_CCITT(const uint8_t* inStr, size_t length)
{
    uint8_t CL = 0xFF;
    uint8_t Ch = 0xFF;

    uint8_t HB1, LB1;

    for (size_t i = 0; i < length; ++i)
    {
        Ch ^= inStr[i];

        for (int j = 0; j < 8; ++j)
        {
            HB1 = Ch & 0x80;
            LB1 = CL & 0x80;

            Ch = (Ch & 0x7F) * 2;
            CL = (CL & 0x7F) * 2;

            if (LB1 == 0x80)
            {
                Ch |= 0x01;
            }

            if (HB1 == 0x80)
            {
                Ch ^= 0x10;
                CL ^= 0x21;
            }
        }
    }

    return (static_cast<uint16_t>(Ch) << 8) | static_cast<uint16_t>(CL);
}

void LCSCReader::encryptAES256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& challenge, std::vector<uint8_t>& encryptedChallenge)
{
    try
    {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
        {
            throw std::runtime_error("Failed to create EVP_CIPHER_CTX.");
        }

        if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr, key.data(), nullptr))
        {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("AES encryption initialization failed.");
        }

        // Disable padding to match AES_encrypt()
        EVP_CIPHER_CTX_set_padding(ctx, 0);

        encryptedChallenge.resize(challenge.size() + AES_BLOCK_SIZE);
        int outLen = 0;

        if (!EVP_EncryptUpdate(ctx, encryptedChallenge.data(), &outLen, challenge.data(), challenge.size()))
        {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("AES encryption failed during update.");
        }

        int finalLen = 0;
        if (!EVP_EncryptFinal_ex(ctx, encryptedChallenge.data() + outLen, &finalLen))
        {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("AES encryption failed during finalization.");
        }

        // Resize to the actual encrypted size
        encryptedChallenge.resize(outLen + finalLen);

        EVP_CIPHER_CTX_free(ctx);
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

std::vector<uint8_t> LCSCReader::prepareCmd(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> payloadData)
{
    std::vector<uint8_t> msgData;

    CscPacket msg;

    msg.setAttentionCode(0xA5);
    msg.setCode(static_cast<uint8_t>(LCSC_CMD_CODE::COMMAND));

    switch (cmd)
    {
        case LCSC_CMD::GET_STATUS_CMD:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::GET_STATUS));
            msg.setLength(0x0006);
            break;
        }
        case LCSC_CMD::LOGIN_1:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::AUTH_LOGIN1));
            msg.setLength(0x0017);

            // Payload
            std::vector<uint8_t> payload;
            // Payload field - Mode
            payload.push_back(0x01);
            // Payload field - TS Challenge
            for (int i = 0; i < 16; i++)
            {
                payload.push_back(i);
            }
            msg.setPayload(payload);
            break;
        }
        case LCSC_CMD::LOGIN_2:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::AUTH_LOGIN2));
            msg.setLength(0x0016);

            // Payload
            try
            {
                std::vector<uint8_t> payload;
                auto cmdFieldData = std::static_pointer_cast<std::vector<uint8_t>>(payloadData);
                if (!cmdFieldData)
                {
                    throw std::runtime_error("Failed to cast payloadData to std::vector<uint8_t>");
                }
                // Payload field - TS Response
                std::vector<uint8_t> encryptedChallenge(16);
                encryptAES256(aes_key, *cmdFieldData, encryptedChallenge);
                payload = encryptedChallenge;
                msg.setPayload(payload);
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << __func__ << ", Exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            break;
        }
        case LCSC_CMD::LOGOUT:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::AUTH_LOGOUT));
            msg.setLength(0x0006);
            break;
        }
        case LCSC_CMD::GET_CARD_ID:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::CARD_ID));
            msg.setLength(0x0007);

            // Payload
            std::vector<uint8_t> payload;
            // Payload field - Timeout (1s)
            payload.push_back(0x01);
            msg.setPayload(payload);
            break;
        }
        case LCSC_CMD::CARD_BALANCE:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::CARD_BALANCE));
            msg.setLength(0x0007);

            // Payload
            std::vector<uint8_t> payload;
            // Payload field - Timeout (1s)
            payload.push_back(0x01);
            msg.setPayload(payload);
            break;
        }
        case LCSC_CMD::CARD_DEDUCT:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::CARD_DEDUCT));
            msg.setLength(0x0012);

            // Payload
            try
            {
                std::vector<uint8_t> payload;
                // Payload field - Timeout (4s)
                payload.push_back(0x04);
                // Payload field - Deduction Amount
                auto cmdFieldData = std::static_pointer_cast<uint32_t>(payloadData);
                if (!cmdFieldData)
                {
                    throw std::runtime_error("Failed to cast payloadData to std::vector<uint32_t>");
                }
                uint32_t amount = *cmdFieldData;
                payload.push_back(((amount >> 24) & 0xFF));
                payload.push_back(((amount >> 16) & 0xFF));
                payload.push_back(((amount >> 8) & 0xFF));
                payload.push_back((amount & 0xFF));
                // Payload field - Transaction Time
                std::time_t epochSeconds = Common::getInstance()->FnGetEpochSeconds();
                payload.push_back(((epochSeconds >> 24) & 0xFF));
                payload.push_back(((epochSeconds >> 16) & 0xFF));
                payload.push_back(((epochSeconds >> 8) & 0xFF));
                payload.push_back((epochSeconds & 0xFF));
                lastDebitTime_ = Common::getInstance()->FnFormatEpochTime(epochSeconds);
                // Payload field - Carpark ID
                payload.push_back(0x00);
                payload.push_back(0x00);
                payload.push_back(0x02);
                msg.setPayload(payload);
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << __func__ << ", Exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            break;
        }
        case LCSC_CMD::CARD_RECORD:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::CARD_RECORD));
            msg.setLength(0x0006);
            break;
        }
        case LCSC_CMD::CARD_FLUSH:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::CARD_FLUSH));
            msg.setLength(0x000A);

            // Payload
            try
            {
                std::vector<uint8_t> payload;
                // Payload field - Seed
                auto cmdFieldData = std::static_pointer_cast<uint32_t>(payloadData);
                if (!cmdFieldData)
                {
                    throw std::runtime_error("Failed to cast payloadData to std::vector<uint32_t>");
                }
                uint32_t seed = *cmdFieldData;
                payload.push_back(((seed >> 24) & 0xFF));
                payload.push_back(((seed >> 16) & 0xFF));
                payload.push_back(((seed >> 8) & 0xFF));
                payload.push_back((seed & 0xFF));
                msg.setPayload(payload);
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << __func__ << ", Exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            break;
        }
        case LCSC_CMD::GET_TIME:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::CLK_GET));
            msg.setLength(0x0006);
            break;
        }
        case LCSC_CMD::SET_TIME:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::CLK_SET));
            msg.setLength(0x000A);

            // Payload
            std::vector<uint8_t> payload;
            // Payload field - Epoch Time
            std::time_t epochSeconds = Common::getInstance()->FnGetEpochSeconds();
            payload.push_back(((epochSeconds >> 24) & 0xFF));
            payload.push_back(((epochSeconds >> 16) & 0xFF));
            payload.push_back(((epochSeconds >> 8) & 0xFF));
            payload.push_back((epochSeconds & 0xFF));
            msg.setPayload(payload);
            break;
        }
        case LCSC_CMD::UPLOAD_CFG_FILE:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::CFG_UPLOAD));

            // Payload
            try
            {
                std::vector<uint8_t> payload;
                // Payload field - size and configuration file block
                auto cmdFieldData = std::static_pointer_cast<std::vector<uint8_t>>(payloadData);
                if (!cmdFieldData)
                {
                    throw std::runtime_error("Failed to cast payloadData to std::vector<uint8_t>");
                }
                payload = *cmdFieldData;
                msg.setPayload(payload);

                std::size_t msgLength = payload.size() + 6;
                msg.setLength(static_cast<uint16_t>(msgLength));
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << __func__ << ", Exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            break;
        }
        case LCSC_CMD::UPLOAD_CIL_FILE:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::CIL_UPLOAD));

            // Payload
            try
            {
                std::vector<uint8_t> payload;
                // Payload field - Blacklist index, size and blacklist block
                auto cmdFieldData = std::static_pointer_cast<std::vector<uint8_t>>(payloadData);
                if (!cmdFieldData)
                {
                    throw std::runtime_error("Failed to cast payloadData to std::vector<uint8_t>");
                }
                payload = *cmdFieldData;
                msg.setPayload(payload);

                std::size_t msgLength = payload.size() + 6;
                msg.setLength(static_cast<uint16_t>(msgLength));
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << __func__ << ", Exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            break;
        }
        case LCSC_CMD::UPLOAD_BL_FILE:
        {
            msg.setType(static_cast<uint8_t>(LCSC_CMD_TYPE::BL_UPLOAD));

            // Payload
            try
            {
                std::vector<uint8_t> payload;
                // Payload field - Blacklist index, size and blacklist block
                auto cmdFieldData = std::static_pointer_cast<std::vector<uint8_t>>(payloadData);
                if (!cmdFieldData)
                {
                    throw std::runtime_error("Failed to cast payloadData to std::vector<uint8_t>");
                }
                payload = *cmdFieldData;
                msg.setPayload(payload);

                std::size_t msgLength = payload.size() + 6;
                msg.setLength(static_cast<uint16_t>(msgLength));
            }
            catch (const std::exception& e)
            {
                std::stringstream ss;
                ss << __func__ << ", Exception: " << e.what();
                Logger::getInstance()->FnLogExceptionError(ss.str());
            }
            break;
        }
        default:
        {
            std::ostringstream oss;
            oss << __func__ << " : Command not found.";
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
        }
    }

    std::vector<uint8_t> dataSerializeWithoutCRC = msg.serializeWithoutCRC();
    uint16_t crc = CRC16_CCITT(dataSerializeWithoutCRC.data(), dataSerializeWithoutCRC.size());
    msg.setCrc(crc);

    msgData = msg.serialize();

    // Can enable if want to debug
    //Logger::getInstance()->FnLog(msg.getMsgCscPacketOutput(), logFileName_, "LCSC");

    return msgData;
}

void LCSCReader::resetRxBuffer()
{
    rxBuffer_.fill(0);
    rxNum_ = 0;
}

std::vector<uint8_t> LCSCReader::getRxBuffer() const
{
    return std::vector<uint8_t>(rxBuffer_.begin(), rxBuffer_.begin() + rxNum_);
}

void LCSCReader::startRead()
{
    boost::asio::post(strand_, [this]() {
        pSerialPort_->async_read_some(
            boost::asio::buffer(readBuffer_, readBuffer_.size()),
            boost::asio::bind_executor(strand_,
                                        std::bind(&LCSCReader::readEnd, this,
                                        std::placeholders::_1,
                                        std::placeholders::_2)));
    });
}

void LCSCReader::readEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    lastSerialReadTime_ = std::chrono::steady_clock::now();

    if (!error)
    {
        std::vector<uint8_t> data(readBuffer_.begin(), readBuffer_.begin() + bytesTransferred);
        if (isRxResponseComplete(data))
        {
            processEvent(EVENT::RESPONSE_RECEIVED);
        }
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Read Error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
    }

    startRead();
}

bool LCSCReader::isRxResponseComplete(const std::vector<uint8_t>& dataBuff)
{
    bool ret = false;

    for (const auto& data : dataBuff)
    {
        switch (rxState_)
        {
            case RX_STATE::RX_START:
            {
                if (data == 0xa5)
                {
                    resetRxBuffer();
                    rxBuffer_[rxNum_++] = data;
                    rxState_ = RX_STATE::RX_RECEIVING;
                }
                break;
            }
            case RX_STATE::RX_RECEIVING:
            {
                rxBuffer_[rxNum_++] = data;

                if (rxNum_ > 4)
                {
                    uint16_t dataLen = (static_cast<uint16_t>(rxBuffer_[2] << 8) | (static_cast<uint16_t>(rxBuffer_[3])));

                    if (rxNum_ == dataLen)
                    {
                        rxState_ = RX_STATE::RX_START;
                        ret = true;
                    }
                    else
                    {
                        ret = false;
                    }
                }
                else
                {
                    ret = false;
                }
                break;
            }
        }
    }

    return ret;
}

void LCSCReader::enqueueWrite(const std::vector<uint8_t>& data)
{
    boost::asio::dispatch(strand_, [this, data = std::move(data)]() {
        bool wasWriting_ = this->write_in_progress_;
        writeQueue_.push(std::move(data));
        if (!wasWriting_)
        {
            startWrite();
        }
    });
}

void LCSCReader::startWrite()
{
    if (writeQueue_.empty())
    {
        Logger::getInstance()->FnLog(__func__ + std::string(" Write Queue is empty."), logFileName_, "UPT");
        write_in_progress_ = false;  // Ensure flag is reset when queue is empty
        return;
    }

    write_in_progress_ = true;  // Set this immediately to prevent duplicate writes

    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastRead = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSerialReadTime_).count();

    // Check if less than 200 milliseconds
    /* Temp: Disable due to send CD files slow
    if (timeSinceLastRead < 200)
    {
        auto boostTime = boost::posix_time::milliseconds(200 - timeSinceLastRead);
        serialWriteDelayTimer_.expires_from_now(boostTime);
        serialWriteDelayTimer_.async_wait(boost::asio::bind_executor(strand_, 
                [this](const boost::system::error_code&) {
                    boost::asio::post(strand_, [this]() { startWrite(); });
            }));
        
        return;
    }
    */

    const auto& data = writeQueue_.front();
    std::ostringstream oss;
    oss << "Data sent : " << Common::getInstance()->FnGetDisplayVectorCharToHexString(data);
    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
    boost::asio::async_write(*pSerialPort_,
                            boost::asio::buffer(data),
                            boost::asio::bind_executor(strand_,
                                                        std::bind(&LCSCReader::writeEnd, this,
                                                                    std::placeholders::_1,
                                                                    std::placeholders::_2)));
}

void LCSCReader::writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    if (!error)
    {
        writeQueue_.pop();
        processEvent(EVENT::WRITE_COMPLETED);
    }
    else
    {
        std::ostringstream oss;
        oss << "Serial Write error: " << error.message();
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
        processEvent(EVENT::WRITE_FAILED);
    }
    write_in_progress_ = false;
}

bool LCSCReader::isCurrentCmdResponse(LCSCReader::LCSC_CMD currCmd, uint8_t respType)
{
    bool ret = false;

    if (((currCmd == LCSC_CMD::GET_STATUS_CMD) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::GET_STATUS))
        || ((currCmd == LCSC_CMD::LOGIN_1) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::AUTH_LOGIN1))
        || ((currCmd == LCSC_CMD::LOGIN_2) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::AUTH_LOGIN2))
        || ((currCmd == LCSC_CMD::LOGOUT) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::AUTH_LOGOUT))
        || ((currCmd == LCSC_CMD::GET_CARD_ID) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::CARD_ID))
        || ((currCmd == LCSC_CMD::CARD_BALANCE) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::CARD_BALANCE))
        || ((currCmd == LCSC_CMD::CARD_DEDUCT) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::CARD_DEDUCT))
        || ((currCmd == LCSC_CMD::CARD_RECORD) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::CARD_RECORD))
        || ((currCmd == LCSC_CMD::CARD_FLUSH) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::CARD_FLUSH))
        || ((currCmd == LCSC_CMD::GET_TIME) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::CLK_GET))
        || ((currCmd == LCSC_CMD::SET_TIME) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::CLK_SET))
        || ((currCmd == LCSC_CMD::UPLOAD_CFG_FILE) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::CFG_UPLOAD))
        || ((currCmd == LCSC_CMD::UPLOAD_CIL_FILE) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::CIL_UPLOAD))
        || ((currCmd == LCSC_CMD::UPLOAD_BL_FILE) && (static_cast<LCSC_CMD_TYPE>(respType) == LCSC_CMD_TYPE::BL_UPLOAD)))
    {
        ret = true;
    }

    return ret;
}

void LCSCReader::handleReceivedCmd(const std::vector<uint8_t>& msgDataBuff)
{
    bool isResponse = false;
    std::stringstream receivedRespStream;
    std::stringstream rspEventStream;

    receivedRespStream << "Received MSG data buffer: " << Common::getInstance()->FnGetDisplayVectorCharToHexString(msgDataBuff);
    Logger::getInstance()->FnLog(receivedRespStream.str(), logFileName_, "LCSC");

    CscPacket msg;
    msg.deserialize(msgDataBuff);

    // Check CRC
    uint16_t crc_result = CRC16_CCITT(msgDataBuff.data(), (msgDataBuff.size() - 2));
    if (crc_result == msg.getCrc())
    {
        // Check command code is response
        if ((msg.getCode() == static_cast<uint8_t>(LCSC_CMD_CODE::RESPONSE)) && isCurrentCmdResponse(getCurrentCmd(), msg.getType()))
        {
            isResponse = true;
            // Cancel rspTimer_ timer
            rspTimer_.cancel();
            // Handle command response
            std::string msgRsp = handleCmdResponse(msg);
            rspEventStream << msgRsp;
        }
        else
        {
            Logger::getInstance()->FnLog("Invalid Command Code.", logFileName_, "LCSC");
            rspEventStream << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::rNotRespCmd));

            // Raise internal event with response received by checking its command type
            if (isChunkedCommandType(static_cast<LCSC_CMD_TYPE>(msg.getType())))
            {
                // Display the cmd response when error
                Logger::getInstance()->FnLog(msg.getMsgCscPacketOutput(), logFileName_, "LCSC");

                processEvent(EVENT::CHUNK_COMMAND_ERROR);
                handleUploadLcscFilesCmdResponse(msg, rspEventStream.str());
            }
        }
    }
    else
    {
        Logger::getInstance()->FnLog("Invalid CRC.", logFileName_, "LCSC");
        rspEventStream << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::rCRCError));

        // Raise internal event with response received by checking its command type
        if (isChunkedCommandType(static_cast<LCSC_CMD_TYPE>(msg.getType())))
        {
            // Display the cmd response when error
            Logger::getInstance()->FnLog(msg.getMsgCscPacketOutput(), logFileName_, "LCSC");

            processEvent(EVENT::CHUNK_COMMAND_ERROR);
            handleUploadLcscFilesCmdResponse(msg, rspEventStream.str());
        }
    }

    if (isResponse == false && continueReadFlag_.load() == false && (getCurrentCmd() == LCSC_CMD::GET_CARD_ID || getCurrentCmd() == LCSC_CMD::CARD_BALANCE))
    {
        rspEventStream.str("");
        rspEventStream.clear();
        Logger::getInstance()->FnLog("No raise event due to stop read.", logFileName_, "LCSC");
    }

    // Raise event
    if (!(rspEventStream.str().empty()))
    {
        EventManager::getInstance()->FnEnqueueEvent(getEventStringFromResponseCmdType(msg.getType()), rspEventStream.str());
    }
}

void LCSCReader::handleUploadLcscFilesCmdResponse(const CscPacket& msg, const std::string& msgRsp)
{
    if ((msg.getType() == static_cast<uint8_t>(LCSC_CMD_TYPE::BL_UPLOAD))
        || (msg.getType() == static_cast<uint8_t>(LCSC_CMD_TYPE::CIL_UPLOAD))
        || (msg.getType() == static_cast<uint8_t>(LCSC_CMD_TYPE::CFG_UPLOAD)))
    {
        std:string cdFilesRsp = msgRsp;
        uint8_t msg_status = 0xFF;
        try
        {
            std::vector<std::string> subVector = Common::getInstance()->FnParseString(cdFilesRsp, ',');
            for (unsigned int i = 0; i < subVector.size(); i++)
            {
                std::string pair = subVector[i];
                std::string param = Common::getInstance()->FnBiteString(pair, '=');
                std::string value = pair;

                if (param == "msgStatus")
                {
                    msg_status = static_cast<uint8_t>(std::stoi(value));
                }
            }
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: " << e.what();
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }

        if ((msg_status == static_cast<int>(mCSCEvents::sBLUploadSuccess))
            || (msg_status == static_cast<int>(mCSCEvents::sCILUploadSuccess))
            || (msg_status == static_cast<int>(mCSCEvents::sCFGUploadSuccess)))
        {
            processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOADED);
        }
        else
        {
            processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
        }
    }
}

void LCSCReader::handleUploadLcscGetStatusCmdResponse(const CscPacket& msg, const std::string& msgRsp)
{
    if (msg.getType() == static_cast<int>(LCSC_CMD_TYPE::GET_STATUS))
    {
        std::string getStatusRsp = msgRsp;
        int msg_status = -1;
        try
        {
            std::vector<std::string> subVector = Common::getInstance()->FnParseString(getStatusRsp, ',');
            for (unsigned int i = 0; i < subVector.size(); i++)
            {
                std::string pair = subVector[i];
                std::string param = Common::getInstance()->FnBiteString(pair, '=');
                std::string value = pair;

                if (param == "msgStatus")
                {
                    msg_status = static_cast<int>(std::stoi(value));
                }
            }
        }
        catch (const std::exception& e)
        {
            std::stringstream ss;
            ss << __func__ << ", Exception: " << e.what();
            Logger::getInstance()->FnLogExceptionError(ss.str());
        }

        if (msg_status == static_cast<int>(mCSCEvents::sGetStatusOK))
        {
            processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_OK, msgRsp);
        }
        else
        {
            processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_FAILED);
        }
    }
}

std::string LCSCReader::handleCmdResponse(const CscPacket& msg)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");
    // If not chunk command (other than upload BL, CIL, CFG cmd), will display the cmd response immediately
    if (!isChunkedCommandType(static_cast<LCSC_CMD_TYPE>(msg.getType())))
    {
        Logger::getInstance()->FnLog(msg.getMsgCscPacketOutput(), logFileName_, "LCSC");
    }

    std::ostringstream oss;
    std::vector<uint8_t> payload = msg.getPayload();
    // Flag for those command required to send login cmd due to unsupported cmd result received
    bool isLoginCmdRequired = false;

    switch (msg.getType())
    {
        // Get Status Cmd
        case 0x00:
        {
            switch (payload[0])
            {
                case 0x00:  // Result cmd success
                {
                    std::string serial_num = "";
                    int reader_mode = 0;
                    std::string bl1_version = "";
                    std::string bl2_version = "";
                    std::string bl3_version = "";
                    std::string bl4_version = "";
                    std::string cil1_version = "";
                    std::string cil2_version = "";
                    std::string cil3_version = "";
                    std::string cil4_version = "";
                    std::string cfg_version = "";
                    std::string firmware_version = "";

                    // Format like .assign(begin + startPos, .begin() + startPos + length)
                    serial_num.assign(payload.begin() + 1, payload.begin() + 1 + 32);
                    reader_mode = payload[33];
                    bl1_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 37, 2);
                    bl2_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 39, 2);
                    bl3_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 41, 2);
                    bl4_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 43, 2);
                    cil1_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 45, 2);
                    cil2_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 47, 2);
                    cil3_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 49, 2);
                    cil4_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 51, 2);
                    cfg_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 53, 2);
                    firmware_version = Common::getInstance()->FnGetVectorCharToHexString(payload, 55, 3);

                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sGetStatusOK));
                    oss << ",serialNum=" << serial_num;
                    oss << ",readerMode=" << reader_mode;
                    oss << ",bl1Version=" << bl1_version;
                    oss << ",bl2Version=" << bl2_version;
                    oss << ",bl3Version=" << bl3_version;
                    oss << ",bl4Version=" << bl4_version;
                    oss << ",cil1Version=" << cil1_version;
                    oss << ",cil2Version=" << cil2_version;
                    oss << ",cil3Version=" << cil3_version;
                    oss << ",cil4Version=" << cil4_version;
                    oss << ",cfgVersion=" << cfg_version;
                    oss << ",firmwareVersion=" << firmware_version;
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x1c:  // Result unknown error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnknown));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
        case 0x10:  // AUTH_LOGIN1
        {
            switch (payload[0])
            {
                case 0x00:  // Result cmd success
                {
                    std::vector<uint8_t> reader_challenge;
                    reader_challenge.assign(payload.begin() + 17, payload.begin() + 17 + 16);
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sLogin1Success));
                    oss << ",readerChallenge=" << Common::getInstance()->FnConvertVectorUint8ToHexString(reader_challenge);
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

                    std::shared_ptr<void> req_data = std::make_shared<std::vector<uint8_t>>(reader_challenge);
                    enqueueCommand(LCSC_CMD::LOGIN_2, req_data);
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x04:  // Result unsupported mode
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedMode));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x1D:  // Result reader already authenticated
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sLoginAlready));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
        case 0x11:  // AUTH_LOGIN2
        {
            switch (payload[0])
            {
                case 0x00:  // Result cmd success
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sLoginSuccess));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x04:  // Result unsupported mode
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedMode));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x05:  // Result reader authentication error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sLoginAuthFail));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
        case 0x12:  // AUTH_LOGOUT
        {
            switch (payload[0])
            {
                case 0x00:  // Result cmd success
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sLogoutSuccess));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x13:  // Result last transaction record not flushed
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sRecordNotFlush));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
        case 0x20:  // CARD ID
        {
            bool getCardIDSuccess = false;

            // If received the stop read, no need to proceed the response
            if (continueReadFlag_.load() == false)
            {
                Logger::getInstance()->FnLog("Stop read, no need to process the response.", logFileName_, "LCSC");
                break;
            }

            switch (payload[0])
            {
                case 0x00:  // Result cmd success
                {
                    std::string card_serial_num = "";
                    std::string card_application_num = "";

                    card_serial_num = Common::getInstance()->FnGetVectorCharToHexString(payload, 1, 8);
                    card_application_num = Common::getInstance()->FnGetVectorCharToHexString(payload, 9, 8);

                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sGetIDSuccess));
                    oss << ",CSN=" << card_serial_num;
                    oss << ",CAN=" << card_application_num;
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    continueReadFlag_.store(false);
                    FnSendGetCardBalance();
                    getCardIDSuccess = true;
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x06:  // Result no card
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sNoCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x07:  // Result card error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCardError));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x08:  // Result RF error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sRFError));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x10:  // Result multiple cards detected
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sMultiCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x12:  // Result card not on card issuer list
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCardnotinlist));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }

            if ((continueReadFlag_.load() == true) && (getCardIDSuccess == false))
            {
                oss.str("");
                Logger::getInstance()->FnLog("No card detected, continue detect.", logFileName_, "LCSC");
                enqueueCommandToFront(LCSC_CMD::GET_CARD_ID);
            }
            break;
        }
        case 0x21:  // Get Card Balance
        {
            bool getCardBalanceSuccess = false;
            // If received the stop read, no need to proceed the response
            if (continueReadFlag_.load() == false)
            {
                Logger::getInstance()->FnLog("Stop read, no need to process the response.", logFileName_, "LCSC");
                break;
            }

            switch (payload[0])
            {
                case 0x00:  // Result success cmd
                {
                    std::string card_serial_num = "";
                    std::string card_application_num = "";
                    std::string card_balance = "";

                    card_serial_num = Common::getInstance()->FnGetVectorCharToHexString(payload, 1, 8);
                    card_application_num = Common::getInstance()->FnGetVectorCharToHexString(payload, 9, 8);
                    uint32_t temp_card_balance = (payload[17] << 16) | (payload[18] << 8) | payload[19];
                    card_balance = std::to_string(temp_card_balance);

                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sGetBlcSuccess));
                    oss << ",CSN=" << card_serial_num;
                    oss << ",CAN=" << card_application_num;
                    oss << ",cardBalance=" << card_balance;
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    continueReadFlag_.store(false);
                    getCardBalanceSuccess = true;
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x06:  // Result no card
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sNoCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x07:  // Result card error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCardError));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x08:  // Result RF error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sRFError));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x10:  // Result multiple cards detected
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sMultiCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x12:  // Result card not on card issuer list
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCardnotinlist));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }

            if ((continueReadFlag_.load() == true) && (getCardBalanceSuccess == false))
            {
                oss.str("");
                Logger::getInstance()->FnLog("No card balance detected, continue detect.", logFileName_, "LCSC");
                enqueueCommandToFront(LCSC_CMD::CARD_BALANCE);
            }
            break;
        }
        case 0x22:  // Card Deduct
        {
            switch (payload[0])
            {
                case 0x00:  // Result success cmd
                {
                    std::string seed = "";
                    std::string card_application_num = "";
                    std::string card_serial_num = "";
                    std::vector<uint8_t> transRecordVec1;
                    std::string transRecord1 = "";
                    std::string balanceBeforeTrans = "";
                    std::vector<uint8_t> transRecordVec2;
                    std::string transRecord2 = "";
                    std::string balanceAfterTrans = "";

                    seed = Common::getInstance()->FnGetVectorCharToHexString(payload, 1, 4);
                    card_application_num = Common::getInstance()->FnGetVectorCharToHexString(payload, 5, 8);
                    card_serial_num.assign(payload.begin() + 13, payload.begin() + 13 + 32);
                    transRecordVec1.assign(payload.begin() + 45, payload.begin() + 45 + 30);
                    transRecord1 = Common::getInstance()->FnVectorUint8ToBinaryString(transRecordVec1);
                    balanceBeforeTrans = std::to_string(Common::getInstance()->FnConvertStringToDecimal(Common::getInstance()->FnConvertBinaryStringToString(transRecord1.substr(171, 24))));
                    transRecordVec2.assign(payload.begin() + 77, payload.begin() + 77 + 30);
                    transRecord2 = Common::getInstance()->FnVectorUint8ToBinaryString(transRecordVec2);
                    balanceAfterTrans = std::to_string(Common::getInstance()->FnConvertStringToDecimal(Common::getInstance()->FnConvertBinaryStringToString(transRecord2.substr(168, 24))));

                    // Process Trans
                    processTrans(payload);

                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sGetDeductSuccess));
                    oss << ",seed=" << seed;
                    oss << ",CAN=" << card_application_num;
                    oss << ",CSN=" << card_serial_num;
                    oss << ",BalanceBeforeTrans=" << balanceBeforeTrans;
                    oss << ",BalanceAfterTrans=" << balanceAfterTrans;
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

                    // Deduct successfully, need to flush card
                    try
                    {
                        std::shared_ptr<void> req_data = std::make_shared<uint32_t>(static_cast<uint32_t>(std::stoul(seed, nullptr, 16)));
                        enqueueCommandToFront(LCSC_CMD::CARD_FLUSH, req_data);
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
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x06:  // Result no card
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sNoCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x07:  // Result card erorr
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCardError));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x08:  // Result RF error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sRFError));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x09:  // Result expired card
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sExpiredCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x0a:  // Result card authentication error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCardAuthError));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x0b:  // Result lock card
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sLockedCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x0c:  // Result inadequate purse
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sInadequatePurse));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x0d:  // Result cryptographic error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCryptoError));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x0e:  // Result card parameters error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCardParaError));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x0f:  // Result changed card
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sChangedCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x10:  // Result multiple card detected
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sMultiCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x11:  // Result card on blacklist
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sBlackCard));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x12:  // Result card not on card issuer list
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCardnotinlist));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x13:  // Result last transaction record not flushed
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sRecordNotFlush));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x1e:  // Result configuration file not loaded
                {
                    break;
                }
            }
            break;
        }
        case 0x23:  // Card Record
        {
            switch (payload[0])
            {
                case 0x00:  // Result success cmd
                {
                    std::string seed = "";
                    seed = Common::getInstance()->FnGetVectorCharToHexString(payload, 1, 4);

                    // Process Trans
                    processTrans(payload);

                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sGetCardRecord));
                    oss << ",seed=" << seed;
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x14:  // Result last transaction record not available
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sNoLastTrans));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
        case 0x24:  // Card Flush Cmd
        {
            switch (payload[0])
            {
                case 0x00:  // Result success cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCardFlushed));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x15:  // Result no transaction record to flush
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sNoNeedFlush));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x16:  // Result incorrect seed
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sWrongSeed));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
        case 0x42:  // Get Time Cmd
        {
            switch (payload[0])
            {
                case 0x00:  // Result success cmd
                {
                    std::string reader_time = "";

                    uint32_t epochSeconds = (payload[1] << 24) | (payload[2] << 16) | (payload[3] << 8) | payload[4];
                    reader_time = Common::getInstance()->FnConvertDateTime(epochSeconds);
                    
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sGetTimeSuccess));
                    oss << ",readerTime=" << reader_time;
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                }
                case 0x1c:  // Result unknown error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnknown));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                }
            }
            break;
        }
        case 0x41:  // Set Time cmd
        {
            switch (payload[0])
            {
                case 0x00:  // Result success cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sSetTimeSuccess));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x1c:  // Result unknown error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnknown));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
        case 0x30:  // Upload BL cmd
        {
            switch (payload[0])
            {
                case 0x00:  // Result success cmd
                {
                    std::string version = "";
                    version = Common::getInstance()->FnGetVectorCharToHexString(payload, 1, 2);
                    if (version == "0000")
                    {
                        oss.str("");
                        Logger::getInstance()->FnLog("Chunked of BL upload successfully.", logFileName_, "LCSC");
                    }
                    else
                    {
                        oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sBLUploadSuccess));
                        oss << ",version=" << version;
                        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    }
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    isLoginCmdRequired = true;
                    break;
                }
                case 0x17:  // Result incorrect list index
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncorrectIndex));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x19:  // Result blacklist upload corrupted
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sBLUploadCorrupt));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x1c:  // Result unknown error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnknown));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
        case 0x31:  // Upload CIL cmd
        {
            switch (payload[0])
            {
                case 0x00:  // Result success cmd
                {
                    std::string version = "";
                    version = Common::getInstance()->FnGetVectorCharToHexString(payload, 1, 2);
                    if (version == "0000")
                    {
                        oss.str("");
                        Logger::getInstance()->FnLog("Chunk of CIL upload successfully.", logFileName_, "LCSC");
                    }
                    else
                    {
                        oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCILUploadSuccess));
                        oss << ",version=" << version;
                        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    }
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    isLoginCmdRequired = true;
                    break;
                }
                case 0x17:  // Result incorrect list index
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncorrectIndex));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x19:  // Result blacklist upload corrupted
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCILUploadCorrupt));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x1c:  // Result unknown error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnknown));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
        case 0x32:  // Upload CFG cmd
        {
            switch (payload[0])
            {
                case 0x00:  // Result success cmd
                {
                    std::string version = "";
                    version = Common::getInstance()->FnGetVectorCharToHexString(payload, 1, 2);
                    if (version == "0000")
                    {
                        oss.str("");
                        Logger::getInstance()->FnLog("Chunk of CFG upload successfully.", logFileName_, "LCSC");
                    }
                    else
                    {
                        oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCFGUploadSuccess));
                        oss << ",version=" << version;
                        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    }
                    break;
                }
                case 0x01:  // Result corrupted cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCorruptedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x02:  // Result incomplete cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sIncompleteCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x03:  // Result unsupported cmd
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnsupportedCmd));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    isLoginCmdRequired = true;
                    break;
                }
                case 0x13:  // Result last transaction record not flushed
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sRecordNotFlush));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x1b:  // Result configuration file upload corrupted
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sCFGUploadCorrupt));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
                case 0x1c:  // Result unknown error
                {
                    oss << "msgStatus=" << std::to_string(static_cast<int>(mCSCEvents::sUnknown));
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                    break;
                }
            }
            break;
        }
    }

    // Raise internal event with response received by checking its command type
    if (isChunkedCommandType(static_cast<LCSC_CMD_TYPE>(msg.getType())))
    {
        if (payload[0] == 0x00)
        {
            std::string version = Common::getInstance()->FnGetVectorCharToHexString(payload, 1, 2);
            if (version == "0000")
            {
                processEvent(EVENT::SEND_NEXT_CHUNK_COMMAND);
            }
            else
            {
                // Display the cmd response when completed
                Logger::getInstance()->FnLog(msg.getMsgCscPacketOutput(), logFileName_, "LCSC");

                processEvent(EVENT::ALL_CHUNK_COMMAND_COMPLETED);
                handleUploadLcscFilesCmdResponse(msg, oss.str());
            }
        }
        else
        {
            // Display the cmd response when error
            Logger::getInstance()->FnLog(msg.getMsgCscPacketOutput(), logFileName_, "LCSC");

            processEvent(EVENT::CHUNK_COMMAND_ERROR);
            handleUploadLcscFilesCmdResponse(msg, oss.str());
        }
    }
    else
    {
        handleUploadLcscGetStatusCmdResponse(msg, oss.str());
    }

    if (isLoginCmdRequired == true)
    {
        FnSendGetLoginCmd();
    }

    return oss.str();
}

void LCSCReader::handleCmdErrorOrTimeout(LCSCReader::LCSC_CMD cmd, LCSCReader::mCSCEvents eventStatus)
{
    switch (cmd)
    {
        case LCSC_CMD::GET_STATUS_CMD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_LcscReaderStatus", oss.str());
            break;
        }
        case LCSC_CMD::LOGIN_1:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_LcscReaderLogin", oss.str());
            break;
        }
        case LCSC_CMD::LOGIN_2:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_LcscReaderLogin", oss.str());
            break;
        }
        case LCSC_CMD::LOGOUT:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderLogout", oss.str());
            break;
        }
        case LCSC_CMD::GET_CARD_ID:
        {
            if (continueReadFlag_.load())
            {
                Logger::getInstance()->FnLog("Timeout and no card detected, continue detect.", logFileName_, "LCSC");
                enqueueCommandToFront(LCSC_CMD::GET_CARD_ID);
            }
            else
            {
                Logger::getInstance()->FnLog("Timeout but stop read, no need to raise event.", logFileName_, "LCSC");
                /*
                std::ostringstream oss;
                oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

                EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderGetCardID", oss.str());
                */
            }
            break;
        }
        case LCSC_CMD::CARD_BALANCE:
        {
            if (continueReadFlag_.load())
            {
                Logger::getInstance()->FnLog("Timeout and no card balance detected, continue detect.", logFileName_, "LCSC");
                enqueueCommandToFront(LCSC_CMD::CARD_BALANCE);
            }
            else
            {
                Logger::getInstance()->FnLog("Timeout but stop read, no need to raise event.", logFileName_, "LCSC");
                /*
                std::ostringstream oss;
                oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

                EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderGetCardBalance", oss.str());
                */
            }
            break;
        }
        case LCSC_CMD::CARD_DEDUCT:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderGetCardDeduct", oss.str());
            break;
        }
        case LCSC_CMD::CARD_RECORD:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderGetCardRecord", oss.str());
            break;
        }
        case LCSC_CMD::CARD_FLUSH:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderGetCardFlush", oss.str());
            break;
        }
        case LCSC_CMD::GET_TIME:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderGetTime", oss.str());
            break;
        }
        case LCSC_CMD::SET_TIME:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderSetTime", oss.str());
            break;
        }
        case LCSC_CMD::UPLOAD_CFG_FILE:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderUploadCFGFile", oss.str());
            break;
        }
        case LCSC_CMD::UPLOAD_CIL_FILE:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderUploadCILFile", oss.str());
            break;
        }
        case LCSC_CMD::UPLOAD_BL_FILE:
        {
            std::ostringstream oss;
            oss << "msgStatus=" << std::to_string(static_cast<int>(eventStatus));
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            EventManager::getInstance()->FnEnqueueEvent("Evt_handleLcscReaderUploadBLFile", oss.str());
            break;
        }
    }
}

void LCSCReader::FnLCSCReaderStopRead()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    continueReadFlag_.store(false);
}

void LCSCReader::FnSendGetStatusCmd()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    enqueueCommand(LCSC_CMD::GET_STATUS_CMD);
}

void LCSCReader::FnSendGetLoginCmd()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    enqueueCommand(LCSC_CMD::LOGIN_1);
}

void LCSCReader::FnSendGetLogoutCmd()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    enqueueCommand(LCSC_CMD::LOGOUT);
}

void LCSCReader::FnSendGetCardIDCmd()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    continueReadFlag_.store(true);
    enqueueCommand(LCSC_CMD::GET_CARD_ID);
}

void LCSCReader::FnSendGetCardBalance()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    continueReadFlag_.store(true);
    enqueueCommand(LCSC_CMD::CARD_BALANCE);
}

void LCSCReader::FnSendCardDeduct(uint32_t amount)
{
    std::shared_ptr<void> req_data = std::make_shared<uint32_t>(amount);
    enqueueCommand(LCSC_CMD::CARD_DEDUCT, req_data);
}

void LCSCReader::FnSendCardRecord()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    enqueueCommand(LCSC_CMD::CARD_RECORD);
}

void LCSCReader::FnSendCardFlush(uint32_t seed)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    std::shared_ptr<void> req_data = std::make_shared<uint32_t>(seed);
    enqueueCommand(LCSC_CMD::CARD_FLUSH, req_data);
}

void LCSCReader::FnSendGetTime()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    enqueueCommand(LCSC_CMD::GET_TIME);
}

void LCSCReader::FnSendSetTime()
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    enqueueCommand(LCSC_CMD::SET_TIME);
}

std::vector<uint8_t> LCSCReader::readFile(const std::filesystem::path& filePath)
{
    try
    {
        std::ifstream file(filePath.string(), std::ios::binary | std::ios::ate);
        
        if (!file.is_open())
        {
            std::ostringstream oss;
            oss << __func__ << " Error opening file: " << filePath;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LSCS");
            return {};
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> fileData(fileSize);
        if (file.read(reinterpret_cast<char*>(fileData.data()), fileSize))
        {
            return fileData;
        }
        else
        {
            std::ostringstream oss;
            oss << __func__ << " Error reading file: " << filePath;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            return {};
        }
    }
    catch (const std::exception& e)
    {
        std::stringstream ss;
        ss << __func__ << ", file path: " << filePath.string() << ", Exception: " << e.what();
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return {};
    }
    catch (...)
    {
        std::stringstream ss;
        ss << __func__ << ", file path: " << filePath.string() << ", Exception: Unknown Exception";
        Logger::getInstance()->FnLogExceptionError(ss.str());
        return {};
    }
}

std::vector<std::vector<uint8_t>> LCSCReader::chunkData(const std::vector<uint8_t>& data, std::size_t chunkSize)
{
    std::vector<std::vector<uint8_t>> chunks;

    for (std::size_t i = 0; i < data.size(); i += chunkSize)
    {
        auto begin = data.begin() + i;
        auto end = (i + chunkSize < data.size()) ? begin + chunkSize : data.end();
        chunks.push_back(std::vector<uint8_t>(begin, end));
    }

    return chunks;
}


int LCSCReader::FnSendUploadCFGFile(const std::string& path)
{
    int ret = -1;

    try
    {
        Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

        const std::filesystem::path filePath(path);
        if (std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath))
        {
            std::vector<unsigned char> fileData = readFile(filePath);

            if (!fileData.empty() && (fileData.size() > 6))
            {
                std::size_t chunkSize = 512;
                std::vector<std::vector<uint8_t>> chunks = chunkData(fileData, chunkSize);
                std::vector<std::vector<uint8_t>> dataChunks;

                for (const auto& chunk : chunks)
                {
                    std::vector<uint8_t> cfgData;
                    std::size_t chunkDataSize = chunk.size();
                    cfgData.push_back(((chunkDataSize >> 8) & 0xFF));
                    cfgData.push_back((chunkDataSize & 0xFF));
                    cfgData.insert(cfgData.end(), chunk.begin(), chunk.end());
                    dataChunks.push_back(cfgData);
                }

                std::vector<uint8_t> cfgLastData;
                cfgLastData.push_back(0x00);
                cfgLastData.push_back(0x00);
                dataChunks.push_back(cfgLastData);
                std::shared_ptr<void> req_data = std::make_shared<std::vector<std::vector<uint8_t>>>(dataChunks);
                enqueueCommand(LCSC_CMD::UPLOAD_CFG_FILE, req_data);
                ret = 0;
            }
            else
            {
                // Todo: need to raise event - sCFGUploadCorrupt
                std::ostringstream oss;
                oss << "File not found or not a regular file :" << filePath;
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            }
        }
        else
        {
            // Todo: need to raise event - sendFailed
            std::ostringstream oss;
            oss << "File not found or not a regular file :" <<filePath;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
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
    return ret;
}

int LCSCReader::FnSendUploadCILFile(const std::string& path)
{
    int ret = -1;

    try
    {
        Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

        const std::filesystem::path filePath(path);
        if (std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath))
        {
            std::vector<unsigned char> fileData = readFile(filePath);

            if (!fileData.empty() && (fileData.size() > 6))
            {
                std::size_t chunkSize = 512;
                std::vector<std::vector<uint8_t>> chunks = chunkData(fileData, chunkSize);
                std::vector<std::vector<uint8_t>> dataChunks;

                bool first = true;
                for (const auto& chunk : chunks)
                {
                    std::vector<uint8_t> cilData;
                    if (first)
                    {
                        //  Block list issuer type - refer back to 
                        //  EPS CCS - CPO Interface Spec v1.4 _26012010.pdf (Page 14)
                        uint8_t BlockListIssuerType = chunk[5] - 143;
                        std::size_t chunkDataSize = chunk.size();
                        cilData.push_back(BlockListIssuerType);
                        cilData.push_back(((chunkDataSize >> 8) & 0xFF));
                        cilData.push_back((chunkDataSize & 0xFF));
                        cilData.insert(cilData.end(), chunk.begin(), chunk.end());
                        first = false;
                    }
                    else
                    {
                        std::size_t chunkDataSize = chunk.size();
                        cilData.push_back(0x00);
                        cilData.push_back(((chunkDataSize >> 8) & 0xFF));
                        cilData.push_back((chunkDataSize & 0xFF));
                        cilData.insert(cilData.end(), chunk.begin(), chunk.end());
                    }
                    dataChunks.push_back(cilData);
                }

                std::vector<uint8_t> cilLastData;
                cilLastData.push_back(0x00);
                cilLastData.push_back(0x00);
                cilLastData.push_back(0x00);
                dataChunks.push_back(cilLastData);
                std::shared_ptr<void> req_data = std::make_shared<std::vector<std::vector<uint8_t>>>(dataChunks);
                enqueueCommand(LCSC_CMD::UPLOAD_CIL_FILE, req_data);
                ret = 0;
            }
            else
            {
                // Todo: need to raise event - sCILUploadCorrupt
                std::ostringstream oss;
                oss << "File not found or not a regular file :" << filePath;
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            }
        }
        else
        {
            // Todo: need to raise event - sendFailed
            std::ostringstream oss;
            oss << "File not found or not a regular file :" <<filePath;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
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

    return ret;
}

int LCSCReader::FnSendUploadBLFile(const std::string& path)
{
    int ret = -1;

    try
    {
        Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

        const std::filesystem::path filePath(path);
        if (std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath))
        {
            std::vector<unsigned char> fileData = readFile(filePath);

            if (!fileData.empty() && (fileData.size() > 6))
            {
                std::size_t chunkSize = 512;
                std::vector<std::vector<uint8_t>> chunks = chunkData(fileData, chunkSize);
                std::vector<std::vector<uint8_t>> dataChunks;

                bool first = true;
                for (const auto& chunk : chunks)
                {
                    std::vector<uint8_t> blData;
                    if (first)
                    {
                        //  Block list issuer type - refer back to 
                        //  EPS CCS - CPO Interface Spec v1.4 _26012010.pdf (Page 15)
                        uint8_t BlockListIssuerType = chunk[5] - 147;
                        std::size_t chunkDataSize = chunk.size();
                        blData.push_back(BlockListIssuerType);
                        blData.push_back(((chunkDataSize >> 8) & 0xFF));
                        blData.push_back((chunkDataSize & 0xFF));
                        blData.insert(blData.end(), chunk.begin(), chunk.end());
                        first = false;
                    }
                    else
                    {
                        std::size_t chunkDataSize = chunk.size();
                        blData.push_back(0x00);
                        blData.push_back(((chunkDataSize >> 8) & 0xFF));
                        blData.push_back((chunkDataSize & 0xFF));
                        blData.insert(blData.end(), chunk.begin(), chunk.end());
                    }
                    dataChunks.push_back(blData);
                }

                std::vector<uint8_t> blLastData;
                blLastData.push_back(0x00);
                blLastData.push_back(0x00);
                blLastData.push_back(0x00);
                dataChunks.push_back(blLastData);
                std::shared_ptr<void> req_data = std::make_shared<std::vector<std::vector<uint8_t>>>(dataChunks);
                enqueueCommand(LCSC_CMD::UPLOAD_BL_FILE, req_data);
                ret = 0;
            }
            else
            {
                // Todo: need to raise event - BLuploadCorrupt
                std::ostringstream oss;
                oss << "File not found or not a regular file :" << filePath;
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            }
        }
        else
        {
            // Todo: need to raise event - sendFailed
            std::ostringstream oss;
            oss << "File not found or not a regular file :" <<filePath;
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
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

    return ret;
}

bool LCSCReader::FnMoveCDAckFile()
{
    std::string folderPath = operation::getInstance()->tParas.gsCSCRcdackFolder;
    std::replace(folderPath.begin(), folderPath.end(), '\\', '/');

    std::string mountPoint = "/mnt/cdack";
    std::string sharedFolderPath = folderPath;
    std::string username = IniParser::getInstance()->FnGetCentralUsername();
    std::string password = IniParser::getInstance()->FnGetCentralPassword();
    std::string cdAckFilePath = LOCAL_LCSC_FOLDER_PATH;//operation::getInstance()->tParas.gsLocalLCSC;

    std::string details;
    if (PingWithTimeOut(IniParser::getInstance()->FnGetCentralDBServer(), 1, details) == true)
    {
        MountManager mountManager(sharedFolderPath, mountPoint, username, password, logFileName_, "LCSC");
        if (!mountManager.isMounted())
        {
            return false;
        }

        // Copy files to mount point
        std::filesystem::path folder(cdAckFilePath);
        if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder))
        {
            for (const auto& entry : std::filesystem::directory_iterator(folder))
            {
                std::string filename = entry.path().filename().string();

                if (std::filesystem::is_regular_file(entry)
                    && (filename.size() >= 6) && (filename.substr(filename.size() - 6) == ".cdack"))
                {
                    std::filesystem::path dest_file = mountPoint / entry.path().filename();
                    std::filesystem::copy(entry.path(), dest_file, std::filesystem::copy_options::overwrite_existing);
                    std::filesystem::remove(entry.path());

                    std::ostringstream oss;
                    oss << "Move " << entry.path() << " to " << dest_file << " successfully";
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                }
            }
        }
        else
        {
            Logger::getInstance()->FnLog("Folder doesn't exists or is not a directory.", logFileName_, "LCSC");
            return false;
        }

        return true;
    }
    else
    {
        Logger::getInstance()->FnLog("CDACK files failed to upload due to ping failed.", logFileName_, "LCSC");
        return false;
    }
}

std::string LCSCReader::calculateSHA256(const std::string& data)
{
    try
    {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();

        if (!ctx ||
            !EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) ||
            !EVP_DigestUpdate(ctx, data.c_str(), data.length()) ||
            !EVP_DigestFinal_ex(ctx, hash, nullptr))
        {
            if (ctx)
            {
                EVP_MD_CTX_free(ctx);
            }
            throw std::runtime_error("SHA-256 calculation failed.");
        }

        EVP_MD_CTX_free(ctx);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLog("SHA256 calculation error: " + std::string(e.what()), logFileName_, "LCSC");
        return {};
    }
    catch (...)
    {
        Logger::getInstance()->FnLog("SHA256 calculation unknown error occurred.", logFileName_, "LCSC");
        return {};
    }
}

bool LCSCReader::FnGenerateCDAckFile(const std::string& serialNum, const std::string& fwVer, const std::string& bl1Ver,
                            const std::string& bl2Ver, const std::string& bl3Ver, const std::string& cil1Ver,
                            const std::string& cil2Ver, const std::string& cil3Ver, const std::string& cfgVer)
{
    std::string cdAckFilePath = LOCAL_LCSC_FOLDER_PATH;//operation::getInstance()->tParas.gsLocalLCSC;

    // Create cd ack file path
    if (!std::filesystem::exists(cdAckFilePath))
    {
        std::error_code ec;
        if (!std::filesystem::create_directories(cdAckFilePath, ec))
        {
            Logger::getInstance()->FnLog(("Failed to create " + cdAckFilePath + " directory : " + ec.message()), logFileName_, "LCSC");
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(("Successfully to create " + cdAckFilePath + " directory."), logFileName_, "LCSC");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(("CD Ack directory: " + cdAckFilePath + " exists."), logFileName_, "LCSC");
    }

    // Construct cd ack file name
    std::string terminalID;
    std::size_t terminalIDLen = serialNum.length();
    if (terminalIDLen < 6)
    {
        terminalID = "000000";
    }
    else
    {
        terminalID = serialNum.substr(terminalIDLen - 6);
    }

    std::string ackFileName = boost::algorithm::trim_copy(operation::getInstance()->tParas.gsCPOID) + "_" + operation::getInstance()->tParas.gsCPID + "_CR"
                                + "0" + terminalID + "_" + Common::getInstance()->FnGetDateTimeFormat_yyyymmdd_hhmmss() + ".cdack";
    std::string sAckFile = cdAckFilePath + "/" + ackFileName;
    
    // Construct data contents
    std::string sHeader;
    std::string sData;
    std::string sDataO;

    sHeader = "H" + Common::getInstance()->FnPadRightSpace(53, ackFileName) + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss() + fwVer;
    sDataO = sHeader;
    sData = sHeader + '\n';

    std::string tmp;
    int count = 0;

    tmp = "";
    if (bl1Ver != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("netscsc2.blk")) + ",$" + bl1Ver;
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (bl2Ver != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("ezlkcsc2.blk")) + ",$" + bl2Ver;
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (bl3Ver != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("fut3csc2.blk")) + ",$" + bl3Ver;
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (cil1Ver != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("netsiss2.sys")) + ",$" + cil1Ver;
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (cil2Ver != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("ezlkiss2.sys")) + ",$" + cil2Ver;
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (cil3Ver != "FFFF")
    {
        tmp = std::string("C") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("fut3iss2.sys")) + ",$" + cil3Ver;
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    tmp = "";
    if (cfgVer != "FFFF")
    {
        tmp = std::string("D") + "$" + "0" + terminalID + "," + boost::algorithm::to_lower_copy(std::string("device.zip")) + ",$" + cfgVer;
        sDataO = sDataO + tmp;
        sData = sData + tmp + '\n';
        count = count + 1;
    }

    // Open ack file and write binary data to it
    std::ofstream outFile(sAckFile, std::ios::binary);

    sData = sData + "T" + Common::getInstance()->FnPadLeft0(6, count);

    std::string hash = calculateSHA256(sDataO);
    if (hash.empty())
    {
        Logger::getInstance()->FnLog("Failed to calculate SHA256 hash.", logFileName_, "LCSC");
    }

    sData = sData + hash + '\n';

    if (!(outFile << sData))
    {
        Logger::getInstance()->FnLog(("Failed to write data into " + sAckFile), logFileName_, "LCSC");
        outFile.close();
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog(("Successfully write data into " + sAckFile), logFileName_, "LCSC");
    }

    outFile.close();

    return true;
}

bool LCSCReader::FnDownloadCDFiles()
{
    std::string folderPath = operation::getInstance()->tParas.gsCSCRcdfFolder;
    std::replace(folderPath.begin(), folderPath.end(), '\\', '/');

    std::string mountPoint = "/mnt/cd";
    std::string sharedFolderPath = folderPath;
    std::string username = IniParser::getInstance()->FnGetCentralUsername();
    std::string password = IniParser::getInstance()->FnGetCentralPassword();
    std::string outputFolderPath = LOCAL_LCSC_FOLDER_PATH;//operation::getInstance()->tParas.gsLocalLCSC;

    std::string details;
    if (PingWithTimeOut(IniParser::getInstance()->FnGetCentralDBServer(), 1, details) == true)
    {
        MountManager mountManager(sharedFolderPath, mountPoint, username, password, logFileName_, "LCSC");
        if (!mountManager.isMounted())
        {
            return false;
        }

        // Check Folder exist or not
        std::filesystem::path folder(mountPoint);
        int fileCount = 0;
        if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder))
        {
            for (const auto& entry : std::filesystem::directory_iterator(folder))
            {
                if (std::filesystem::is_regular_file(entry))
                {
                    fileCount++;
                }
            }
        }

        if (fileCount == 0)
        {
            Logger::getInstance()->FnLog("No CD file to download.", logFileName_, "LCSC");
            return false;
        }

        // Create the output folder if it doesn't exist
        if (!std::filesystem::exists(outputFolderPath))
        {
            std::error_code ec;
            if (!std::filesystem::create_directories(outputFolderPath, ec))
            {
                Logger::getInstance()->FnLog("Failed to create " + outputFolderPath + " directory: " + ec.message(), logFileName_, "LCSC");
                return false;
            }
            else
            {
                Logger::getInstance()->FnLog("Successfully to create " + outputFolderPath + " directory.", logFileName_, "LCSC");
            }
        }
        else
        {
            Logger::getInstance()->FnLog("Output folder directory: " + outputFolderPath + " exists.", logFileName_, "LCSC");
        }

        int downloadTotal = 0;
        if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder))
        {
            for (const auto& entry : std::filesystem::directory_iterator(folder))
            {
                if (std::filesystem::is_regular_file(entry))
                {
                    std::filesystem::path dest_file = outputFolderPath / entry.path().filename();
                    std::filesystem::copy(entry.path(), dest_file, std::filesystem::copy_options::overwrite_existing);
                    std::filesystem::remove(entry.path());
                    downloadTotal++;

                    std::ostringstream oss;
                    oss << "Download " << entry.path() << " to " << dest_file << " successfully.";
                    Logger::getInstance()->FnLog(oss.str());
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                }
            }
        }
        else
        {
            Logger::getInstance()->FnLog("Folder doesn't exist or is not a directory.", logFileName_, "LCSC");
            return false;
        }

        std::ostringstream oss;
        oss << "Total " << downloadTotal << " cd files downloaded successfully.";
        Logger::getInstance()->FnLog(oss.str());
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

        return true;
    }
    else
    {
        Logger::getInstance()->FnLog("Download CD files failed due to ping failed.", logFileName_, "LCSC");
        return false;
    }
}

const LCSCReader::UploadLcscStateTransition LCSCReader::UploadLcscStateTransitionTable[static_cast<int>(LCSCReader::UPLOAD_LCSC_FILES_STATE::STATE_COUNT)] = 
{
    {UPLOAD_LCSC_FILES_STATE::IDLE,
    {
        {UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION                         , &LCSCReader::handleUploadLcscIdleState                        , UPLOAD_LCSC_FILES_STATE::IDLE                                   },
        {UPLOAD_LCSC_FILES_EVENT::ALLOW_DOWNLOAD                          , &LCSCReader::handleUploadLcscIdleState                        , UPLOAD_LCSC_FILES_STATE::DOWNLOAD_CDFILES                       },
        {UPLOAD_LCSC_FILES_EVENT::CONTINUE_UPLOAD                         , &LCSCReader::handleUploadLcscIdleState                        , UPLOAD_LCSC_FILES_STATE::DOWNLOAD_CDFILES                       }
    }},
    {UPLOAD_LCSC_FILES_STATE::DOWNLOAD_CDFILES,
    {
        {UPLOAD_LCSC_FILES_EVENT::CDFILES_DOWNLOADED                      , &LCSCReader::handleDownloadCDFilesState                       , UPLOAD_LCSC_FILES_STATE::UPLOAD_CDFILES                         },
        {UPLOAD_LCSC_FILES_EVENT::NO_CDFILES_DOWNLOADED                   , &LCSCReader::handleDownloadCDFilesState                       , UPLOAD_LCSC_FILES_STATE::IDLE                                   }
    }},
    {UPLOAD_LCSC_FILES_STATE::UPLOAD_CDFILES,
    {
        {UPLOAD_LCSC_FILES_EVENT::CDFILES_UPLOADING                       , &LCSCReader::handleUploadCDFilesState                         , UPLOAD_LCSC_FILES_STATE::UPLOAD_CDFILES                         },
        {UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS                  , &LCSCReader::handleUploadCDFilesState                         , UPLOAD_LCSC_FILES_STATE::GENERATE_CDACKFILES                    },
        {UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOADED                         , &LCSCReader::handleUploadCDFilesState                         , UPLOAD_LCSC_FILES_STATE::IDLE                                   },
        {UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED                    , &LCSCReader::handleUploadCDFilesState                         , UPLOAD_LCSC_FILES_STATE::IDLE                                   }
    }},
    {UPLOAD_LCSC_FILES_STATE::GENERATE_CDACKFILES,
    {
        {UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_OK               , &LCSCReader::handleGenerateCDAckFilesState                    , UPLOAD_LCSC_FILES_STATE::IDLE                                   },
        {UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_FAILED           , &LCSCReader::handleGenerateCDAckFilesState                    , UPLOAD_LCSC_FILES_STATE::IDLE                                   }
    }}
};

std::string LCSCReader::uploadLcscFilesEventToString(LCSCReader::UPLOAD_LCSC_FILES_EVENT event)
{
    std::string returnStr = "Unknown Event";

    switch (event)
    {
        case UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION:
        {
            returnStr = "CHECK_CONDITION";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::ALLOW_DOWNLOAD:
        {
            returnStr = "ALLOW_DOWNLOAD";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::CONTINUE_UPLOAD:
        {
            returnStr = "CONTINUE_UPLOAD";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::CDFILES_DOWNLOADED:
        {
            returnStr = "CDFILES_DOWNLOADED";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::NO_CDFILES_DOWNLOADED:
        {
            returnStr = "NO_CDFILES_DOWNLOADED";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::CDFILES_UPLOADING:
        {
            returnStr = "CDFILES_UPLOADING";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS:
        {
            returnStr = "GET_LCSC_DEVICE_STATUS";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOADED:
        {
            returnStr = "CDFILE_UPLOADED";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED:
        {
            returnStr = "CDFILE_UPLOAD_FAILED";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_OK:
        {
            returnStr = "GET_LCSC_DEVICE_STATUS_OK";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_FAILED:
        {
            returnStr = "GET_LCSC_DEVICE_STATUS_FAILED";
            break;
        }
    }

    return returnStr;
}

std::string LCSCReader::uploadLcscFilesStateToString(LCSCReader::UPLOAD_LCSC_FILES_STATE state)
{
    std::string returnStr = "Unknown State";

    switch (state)
    {
        case UPLOAD_LCSC_FILES_STATE::IDLE:
        {
            returnStr = "IDLE";
            break;
        }
        case UPLOAD_LCSC_FILES_STATE::DOWNLOAD_CDFILES:
        {
            returnStr = "DOWNLOAD_CDFILES";
            break;
        }
        case UPLOAD_LCSC_FILES_STATE::UPLOAD_CDFILES:
        {
            returnStr = "UPLOAD_CDFILES";
            break;
        }
        case UPLOAD_LCSC_FILES_STATE::GENERATE_CDACKFILES:
        {
            returnStr = "GENERATE_CDACKFILES";
            break;
        }
    }

    return returnStr;
}

void LCSCReader::processUploadLcscFilesEvent(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    boost::asio::post(strand_, [this, event, str]() {

        // In idle state it will keep checking and don't want to log it
        if (event != UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION)
        {
            std::ostringstream oss;
            oss << "processUploadLcscFilesEvent => " << uploadLcscFilesEventToString(event);
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
        }

        int currentStateIndex_ = static_cast<int>(currentUploadLcscFilesState_);
        const auto& stateTransitions = UploadLcscStateTransitionTable[currentStateIndex_].transitions;

        bool eventHandled = false;
        for (const auto& transition : stateTransitions)
        {
            if (transition.event == event)
            {
                eventHandled = true;

                if (event != UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION)
                {
                    std::ostringstream oss;
                    oss << "Current State : " << uploadLcscFilesStateToString(currentUploadLcscFilesState_);
                    oss << " , Event : " << uploadLcscFilesEventToString(event);
                    oss << " , Event Handler : " << (transition.lcscEventHandler ? "YES" : "NO");
                    oss << " , Next State : " << uploadLcscFilesStateToString(transition.nextState);
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                }

                if (transition.lcscEventHandler != nullptr)
                {
                    (this->*transition.lcscEventHandler)(event, str);
                }
                currentUploadLcscFilesState_ = transition.nextState;
                return;
            }
        }

        if (!eventHandled)
        {
            std::ostringstream oss;
            oss << "Event '" << uploadLcscFilesEventToString(event) << "' not handled in state '" << uploadLcscFilesStateToString(currentUploadLcscFilesState_) << "'";
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
        }
    });
}

void LCSCReader::handleUploadLcscIdleState(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    if (event == UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION)
    {
        if ((operation::getInstance()->tParas.giCommPortLCSC > 0)
            && (operation::getInstance()->tProcess.gbLoopApresent.load() == false)
            && ((Common::getInstance()->FnGetCurrentHour() < 20)))
        {
            if ((HasCDFileToUpload_ == false)
                && (LastCDUploadDate_ != Common::getInstance()->FnGetCurrentDay())
                && (LastCDUploadTime_ != Common::getInstance()->FnGetCurrentHour()))
            {
                Logger::getInstance()->FnLog("Allow to download CD files.", logFileName_, "LCSC");
                LastCDUploadTime_ = Common::getInstance()->FnGetCurrentHour();
                processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::ALLOW_DOWNLOAD);
            }
            else if (HasCDFileToUpload_ == true)
            {
                Logger::getInstance()->FnLog("Already downloaded, proceed to next upload.", logFileName_, "LCSC");
                processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CONTINUE_UPLOAD);
            }
        }
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::ALLOW_DOWNLOAD)
    {
        if (FnDownloadCDFiles())
        {
            Logger::getInstance()->FnLog("CD files downloaded.", logFileName_, "LCSC");
            HasCDFileToUpload_ = true;
            processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILES_DOWNLOADED);
        }
        else
        {
            Logger::getInstance()->FnLog("No CD files to download.", logFileName_, "LCSC");
            processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::NO_CDFILES_DOWNLOADED);
        }
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::CONTINUE_UPLOAD)
    {
        Logger::getInstance()->FnLog("Continue upload LCSC files.", logFileName_, "LCSC");
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILES_DOWNLOADED);
    }
}

void LCSCReader::handleDownloadCDFilesState(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (event == UPLOAD_LCSC_FILES_EVENT::CDFILES_DOWNLOADED)
    {
        std::string localLCSCFolder = LOCAL_LCSC_FOLDER_PATH;//operation::getInstance()->tParas.gsLocalLCSC;
        std::filesystem::path folder(localLCSCFolder);
        int fileCount = 0;
        std::string cdFileName;

        for (const auto& entry : std::filesystem::directory_iterator(folder))
        {
            std::string filename = entry.path().filename().string();

            if (((filename.size() >= 4) && (filename.substr(filename.size() - 4) != ".lcs"))
                && ((filename.size() >= 6) && (filename.substr(filename.size() - 6) != ".cdack"))
                && ((filename.size() >= 4) && (filename.substr(filename.size() - 4) != ".cfg")))
            {
                fileCount = fileCount + 1;
                cdFileName = entry.path();
            }
        }

        if (fileCount > 0)
        {
            uploadLcscFileName_ = cdFileName;
            Logger::getInstance()->FnLog("Found the CD files to be uploaded.", logFileName_, "LCSC");
            processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILES_UPLOADING, cdFileName);
        }
        else
        {
            Logger::getInstance()->FnLog("Get LCSC device status before generating CD ack file.", logFileName_, "LCSC");
            processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS);
        }
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::NO_CDFILES_DOWNLOADED)
    {
        Logger::getInstance()->FnLog("No CD files to be uploaded.", logFileName_, "LCSC");
    }
}

void LCSCReader::handleUploadCDFilesState(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (event == UPLOAD_LCSC_FILES_EVENT::CDFILES_UPLOADING)
    {
        Logger::getInstance()->FnLog("Uploading the CD file.", logFileName_, "LCSC");
        FnUploadCDFile2(str);
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS)
    {
        Logger::getInstance()->FnLog("Getting LCSC device status.", logFileName_, "LCSC");
        FnSendGetStatusCmd();
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOADED)
    {
        Logger::getInstance()->FnLog("Upload " + uploadLcscFileName_ + " successfully.");
        Logger::getInstance()->FnLog("Upload " + uploadLcscFileName_ + " successfully.", logFileName_, "LCSC");
        std::filesystem::remove(uploadLcscFileName_.c_str());
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED)
    {
        Logger::getInstance()->FnLog("Upload " + uploadLcscFileName_ + " failed.");
        Logger::getInstance()->FnLog("Upload " + uploadLcscFileName_ + " failed.", logFileName_, "LCSC");
        std::filesystem::remove(uploadLcscFileName_.c_str());
    }
}

void LCSCReader::handleGenerateCDAckFilesState(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    if (event == UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_OK)
    {
        Logger::getInstance()->FnLog("Get LCSC device status successfully, now proceed to generate CD ACK file.", logFileName_, "LCSC");

        std::string serial_num = "";
        std::string firmware_version = "";
        std::string bl1_version = "";
        std::string bl2_version = "";
        std::string bl3_version = "";
        std::string cil1_version = "";
        std::string cil2_version = "";
        std::string cil3_version = "";
        std::string cfg_version = "";

        std::string eventData = str;
        if (!eventData.empty())
        {
            try
            {
                std::vector<std::string> subVector = Common::getInstance()->FnParseString(eventData, ',');
                for (unsigned int i = 0; i < subVector.size(); i++)
                {
                    std::string pair = subVector[i];
                    std::string param = Common::getInstance()->FnBiteString(pair, '=');
                    std::string value = pair;

                    if (param == "serialNum")
                    {
                        serial_num = value;
                    }
                    else if (param == "firmwareVersion")
                    {
                        firmware_version = value;
                    }
                    else if (param == "bl1Version")
                    {
                        bl1_version = value;
                    }
                    else if (param == "bl2Version")
                    {
                        bl2_version = value;
                    }
                    else if (param == "bl3Version")
                    {
                        bl3_version = value;
                    }
                    else if (param == "cil1Version")
                    {
                        cil1_version = value;
                    }
                    else if (param == "cil2Version")
                    {
                        cil2_version = value;
                    }
                    else if (param == "cil3Version")
                    {
                        cil3_version = value;
                    }
                    else if (param == "cfgVersion")
                    {
                        cfg_version = value;
                    }
                }
            }
            catch (const std::exception& ex)
            {
                std::ostringstream oss;
                oss << "Exception : " << ex.what();
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "EVT");
            }
        }

        bool ret = FnGenerateCDAckFile(serial_num, firmware_version, bl1_version, bl2_version, bl3_version, cil1_version, cil2_version, cil3_version, cfg_version);
        if (ret == true)
        {
            HasCDFileToUpload_ = false;
            Logger::getInstance()->FnLog("Generate CD Ack file successfully", logFileName_, "LCSC");
            std::string localLCSCFolder = LOCAL_LCSC_FOLDER_PATH;//operation::getInstance()->tParas.gsLocalLCSC;
            std::filesystem::path folder(localLCSCFolder);
            LastCDUploadDate_ = 0;

            for (const auto& entry : std::filesystem::directory_iterator(folder))
            {
                std::string filename = entry.path().filename().string();

                if (((filename.size() >= 4) && (filename.substr(filename.size() - 4) != ".lcs"))
                    && ((filename.size() >= 6) && (filename.substr(filename.size() - 6) != ".cdack")))
                {
                    std::filesystem::remove(entry.path());
                    std::stringstream ss;
                    ss << "File " << filename << " deleted";
                    Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");
                }

                if ((filename.size() >= 6) && (filename.substr(filename.size() - 6) == ".cdack"))
                {
                    if (FnMoveCDAckFile())
                    {
                        LastCDUploadDate_ = Common::getInstance()->FnGetCurrentDay();
                    }
                }
            }
        }
        else
        {
            Logger::getInstance()->FnLog("Generate CD Ack file failed", logFileName_, "LCSC");
        }

    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_FAILED)
    {
        Logger::getInstance()->FnLog("Get LCSC device status failed, cannot proceed to generate CD ACK file", logFileName_, "LCSC");
    }
}

void LCSCReader::FnUploadLCSCCDFiles()
{
    processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION);
}

void LCSCReader::FnUploadCDFile2(std::string path)
{
    std::string CDFPath_ = path;

    if (!CDFPath_.empty())
    {
        if ((CDFPath_.size() >= 4) && (CDFPath_.substr(CDFPath_.size() - 4) == ".zip"))
        {
            std::stringstream ss;
            ss << "Uploading the CFG file :" << CDFPath_ << " to LCSC device.";
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

            int ret = FnSendUploadCFGFile(CDFPath_);
            if (ret == -1)
            {
                handleCmdErrorOrTimeout(LCSC_CMD::UPLOAD_CFG_FILE, mCSCEvents::sSendcmdfail);
                processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
            }
        }
        else if ((CDFPath_.size() >= 4) && (CDFPath_.substr(CDFPath_.size() - 4) == ".sys"))
        {
            std::stringstream ss;
            ss << "Uploading the CIL file :" << CDFPath_ << " to LCSC device.";
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

            int ret = FnSendUploadCILFile(CDFPath_);
            if (ret == -1)
            {
                handleCmdErrorOrTimeout(LCSC_CMD::UPLOAD_CIL_FILE, mCSCEvents::sSendcmdfail);
                processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
            }
        }
        else if ((CDFPath_.size() >= 4) && (CDFPath_.substr(CDFPath_.size() - 4) == ".blk"))
        {
            std::stringstream ss;
            ss << "Uploading the BL file :" << CDFPath_ << " to LCSC device.";
            Logger::getInstance()->FnLog(ss.str());
            Logger::getInstance()->FnLog(ss.str(), logFileName_, "LCSC");

            int ret = FnSendUploadBLFile(CDFPath_);
            if (ret == -1)
            {
                handleCmdErrorOrTimeout(LCSC_CMD::UPLOAD_BL_FILE, mCSCEvents::sSendcmdfail);
                processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
            }
        }
        else if ((CDFPath_.size() >= 4) && (CDFPath_.substr(CDFPath_.size() - 4) == ".lbs"))
        {
            // Temp: Need to implement update FW
        }
    }
}

void LCSCReader::processTrans(const std::vector<uint8_t>& payload)
{
    Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

    // Transaction Record 1
    std::vector<uint8_t> transRecordVec1;
    std::string transRecord1 = "";
    std::string can = "";
    std::string lastTransHeader = "";
    std::string lastCreditTransTRP = "";
    std::string balanceBeforeTrans = "";
    std::string badDebtCounter = "";
    std::string MAC1 = "";
    std::string transAmt = "";

    transRecordVec1.assign(payload.begin() + 45, payload.begin() + 45 + 30);
    transRecord1 = Common::getInstance()->FnVectorUint8ToBinaryString(transRecordVec1);
    can = Common::getInstance()->FnConvertBinaryStringToString(transRecord1.substr(10, 64));
    lastTransHeader = Common::getInstance()->FnConvertBinaryStringToString(transRecord1.substr(74, 64));
    lastCreditTransTRP = Common::getInstance()->FnConvertBinaryStringToString(transRecord1.substr(138, 32));
    balanceBeforeTrans = Common::getInstance()->FnConvertBinaryStringToString(transRecord1.substr(171, 24));
    badDebtCounter = Common::getInstance()->FnConvertBinaryStringToString(transRecord1.substr(195, 8));
    MAC1 = Common::getInstance()->FnConvertBinaryStringToString(transRecord1.substr(208, 32));

    // Transaction Record 2
    std::vector<uint8_t> transRecordVec2;
    std::string transRecord2 = "";
    std::string transStatus = "";
    std::string autoLoadAmt = "";
    std::string counter = "";
    std::string signedCert = "";
    std::string balanceAfterTrans = "";
    std::string lastTransDebitOp = "";

    transRecordVec2.assign(payload.begin() + 77, payload.begin() + 77 + 30);
    transRecord2 = Common::getInstance()->FnVectorUint8ToBinaryString(transRecordVec2);
    transStatus = Common::getInstance()->FnConvertBinaryStringToString("000" + transRecord2.substr(2, 5));
    autoLoadAmt = Common::getInstance()->FnConvertBinaryStringToString(transRecord2.substr(16, 24));
    counter = Common::getInstance()->FnConvertBinaryStringToString(transRecord2.substr(40, 64));
    signedCert = Common::getInstance()->FnConvertBinaryStringToString(transRecord2.substr(104, 64));
    balanceAfterTrans = Common::getInstance()->FnConvertBinaryStringToString(transRecord2.substr(168, 24));
    lastTransDebitOp = Common::getInstance()->FnConvertBinaryStringToString(transRecord2.substr(192, 8));

    // TRP
    std::vector<uint8_t> TRPVec;
    TRPVec.assign(payload.begin() + 109, payload.begin() + 109 + 4);

    uint32_t amt = 0;
    if (Common::getInstance()->FnConvertStringToHexString(transStatus) == "05")
    {
        amt = Common::getInstance()->FnConvertStringToDecimal(balanceBeforeTrans) - Common::getInstance()->FnConvertStringToDecimal(balanceAfterTrans) + Common::getInstance()->FnConvertStringToDecimal(autoLoadAmt);
    }
    else
    {
        amt = Common::getInstance()->FnConvertStringToDecimal(balanceBeforeTrans) - Common::getInstance()->FnConvertStringToDecimal(balanceAfterTrans);
    }

    transAmt = Common::getInstance()->FnConvertHexStringToString(Common::getInstance()->FnPadLeft0_Uint32(6, amt));
    balanceBeforeTrans = Common::getInstance()->FnConvertHexStringToString(Common::getInstance()->FnPadLeft0_Uint32(6, Common::getInstance()->FnConvertStringToDecimal(balanceBeforeTrans)));
    balanceAfterTrans = Common::getInstance()->FnConvertHexStringToString(Common::getInstance()->FnPadLeft0_Uint32(6, Common::getInstance()->FnConvertStringToDecimal(balanceAfterTrans)));
    autoLoadAmt = Common::getInstance()->FnConvertHexStringToString(Common::getInstance()->FnPadLeft0_Uint32(6, Common::getInstance()->FnConvertStringToDecimal(autoLoadAmt)));

    if (lastDebitTime_.empty())
    {
        lastDebitTime_ = Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss();
    }

    std::string transRecord = "D" + can + '\t' + Common::getInstance()->FnConvertuint8ToString(TRPVec.back()) + transAmt + Common::getInstance()->FnConvertHexStringToString(lastDebitTime_);

    for (int i = 0; i < 7; i++)
    {
        transRecord += '\0';
    }

    transRecord = transRecord + signedCert + counter + Common::getInstance()->FnConvertVectorUint8ToString(TRPVec)
                    + balanceAfterTrans + lastCreditTransTRP + lastTransHeader + lastTransDebitOp + balanceBeforeTrans
                    + badDebtCounter + autoLoadAmt + transStatus;

    for (int i = 0; i < 13; i++)
    {
        transRecord += static_cast<char>(0xFF);
    }

    transRecord += std::string(3, static_cast<char>(0));

    for (int i = 0; i < 8; i++)
    {
        transRecord += static_cast<char>(0xFF);
    }

    for (int i = 0; i < 13; i++)
    {
        transRecord += static_cast<char>(0);
    }

    transRecord += MAC1;

    for (int i = 0; i < 4; i++)
    {
        transRecord += static_cast<char>(0);
    }

    transRecord += std::string(3, static_cast<char>(0x20));

    writeLCSCTrans(transRecord);
}

void LCSCReader::writeLCSCTrans(const std::string& data)
{
    try
    {
        Logger::getInstance()->FnLog(__func__, logFileName_, "LCSC");

        std::string settleFile = "";
        std::string remoteSettleFile = "";
        std::string detail = "";
        std::string header = "";
        std::string fileName = "";

        if (!(boost::filesystem::exists(LOCAL_LCSC_SETTLEMENT_FOLDER_PATH)))
        {
            std::ostringstream oss;
            oss << "Settle folder: " << LOCAL_LCSC_SETTLEMENT_FOLDER_PATH << " Not Found, Create it.";
            Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

            if (!(boost::filesystem::create_directories(LOCAL_LCSC_SETTLEMENT_FOLDER_PATH)))
            {
                std::ostringstream oss;
                oss << "Failed to create directory: " << LOCAL_LCSC_SETTLEMENT_FOLDER_PATH;
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            }
        }

        fileName = operation::getInstance()->tParas.gsCPOID + "_" + operation::getInstance()->tParas.gsCPID + "_"
                    + Common::getInstance()->FnGetDateTimeFormat_yyyymmdd() + "_" + Common::getInstance()->FnPadLeft0(2, operation::getInstance()->gtStation.iSID)
                    + Common::getInstance()->FnGetDateTimeFormat_hh() + ".lcs";
        settleFile = LOCAL_LCSC_SETTLEMENT_FOLDER_PATH + "/" + fileName;

        detail = data;

        // Write to local
        Logger::getInstance()->FnLog("Write settlement to local.", logFileName_, "LCSC");

        std::fstream file;
        file.open(settleFile, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            // File does not exist, create and write
            file.clear();
            file.open(settleFile, std::ios::out | std::ios::binary);
            if (!file.is_open())
            {
                Logger::getInstance()->FnLog("Error opening file for writing settlement to " + settleFile, logFileName_, "LCSC");
                Logger::getInstance()->FnLog("Settlement Data: " + detail, logFileName_, "LCSC");
                return;
            }

            // Create header
            header = "H" + operation::getInstance()->tParas.gsCPID + Common::getInstance()->FnConvertHexStringToString(Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss()) + Common::getInstance()->FnPadLeftSpace(40, fileName);
            header.append(67, ' '); // Padding with 67 spaces

            // Write the header and detail to the file
            file.write(header.c_str(), header.size());
            file.write(detail.c_str(), detail.size());
        }
        else
        {
            // File exists, append the details
            file.close();
            file.open(settleFile, std::ios::out | std::ios::binary | std::ios::app);
            if (!file.is_open())
            {
                Logger::getInstance()->FnLog("Error opening file for writing settlement to " + settleFile, logFileName_, "LCSC");
                Logger::getInstance()->FnLog("Settlement Data: " + detail, logFileName_, "LCSC");
                return;
            }

            // Now to the end of file and append the details
            file.write(detail.c_str(), detail.size());
        }

        file.close();
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
