#include "boost/algorithm/string.hpp"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <future>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <string>
#include <sstream>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#endif

#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/serial_port.hpp>
#include "common.h"
#include "event_manager.h"
#include "ini_parser.h"
#include "lcsc.h"
#include "log.h"
#include "mount.h"
#include "operation.h"
#include "ping.h"
#include "thread_pool_helper.h"

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

uint16_t CscPacket::getCrc() const
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

    const std::string msgCode = code ? "RESP" : "CMD";
    const std::string msgType = LCSCReader::getInstance()->getCommandTypeString(type);

    oss << "LCSC: [RX] Packet"
        << " | Type=" << msgType
        << " | Code=" << msgCode
        << " | Length=" << len
        << " | PayloadBytes=" << payload.size()
        << '\n';

    oss << "  Attention [1 byte] = 0x"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<unsigned int>(attn) << '\n';

    oss << "  Code [1 bit]       = "
        << std::dec << static_cast<unsigned int>(code ? 1 : 0)
        << " (" << msgCode << ")\n";

    oss << "  Type [7 bits]      = 0x"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<unsigned int>(type & 0x7F)
        << " (" << msgType << ")\n";

    oss << "  Length [2 bytes]   = 0x"
        << std::setw(4) << static_cast<unsigned int>(len)
        << std::dec << " (" << len << ")\n";

    oss << "  Payload [" << payload.size() << " bytes] = "
        << Common::getInstance()->FnGetDisplayVectorCharToHexString(payload)
        << '\n';

    oss << "  CRC [2 bytes]      = 0x"
        << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
        << static_cast<unsigned int>(crc);

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

LCSCReader::LCSCReader()
    : rspTimer_(ioContext_),
      serialWriteDelayTimer_(ioContext_),
      serialWriteTimer_(ioContext_),
      logFileName_("lcsc")
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

LCSCReader::~LCSCReader()
{
    // Normal shutdown should be performed through FnLCSCReaderClose().
    // This is only a best-effort fallback for process/static destruction.
    acceptingWork_.store(false);
    stopping_.store(true);
    workGuard_.reset();
    ioContext_.stop();

    if (ioContextThread_.joinable() &&
        ioContextThread_.get_id() != std::this_thread::get_id())
    {
        ioContextThread_.join();
    }

    if (filePool_)
    {
        filePool_->stop();
        filePool_->join();
        filePool_.reset();
    }

    pSerialPort_.reset();
}

LCSCReader* LCSCReader::getInstance()
{
    static LCSCReader instance;
    return &instance;
}

int LCSCReader::FnLCSCReaderInit(unsigned int baudRate, const std::string& comPortName)
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    Logger::getInstance()->FnCreateLogFile(logFileName_);

    if (moduleRunning_.load() || ioContextThread_.joinable())
    {
        Logger::getInstance()->FnLog("LCSC: [INIT] Ignored | Reason=Already running", logFileName_, "LCSC");
        return 1;
    }

    resetRuntimeState();

    ioContext_.restart();
    workGuard_.emplace(ioContext_.get_executor());

    try
    {
        // File/network filesystem jobs must not block the single LCSC I/O
        // thread. Two workers are enough for settlement writes plus the CD
        // file workflow without creating a large thread footprint.
        filePool_ = ThreadPoolHelper::create(2, "LCSC_FILE");

        pSerialPort_ = std::make_unique<boost::asio::serial_port>(ioContext_);

        pSerialPort_->open(comPortName);
        pSerialPort_->set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        pSerialPort_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        pSerialPort_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
        pSerialPort_->set_option(boost::asio::serial_port_base::character_size(8));

        if (!boost::filesystem::exists(LOCAL_LCSC_FOLDER_PATH))
        {
            if (!boost::filesystem::create_directories(LOCAL_LCSC_FOLDER_PATH))
            {
                Logger::getInstance()->FnLog("LCSC: [INIT] Directory create failed | Path=" + LOCAL_LCSC_FOLDER_PATH, logFileName_, "LCSC");
            }
        }

        if (!pSerialPort_->is_open())
        {
            Logger::getInstance()->FnLog("LCSC: [INIT] Failed | Port=" + comPortName + " | Reason=Serial port not open", logFileName_, "LCSC");

            pSerialPort_.reset();
            workGuard_.reset();

            if (filePool_)
            {
                filePool_->stop();
                filePool_->join();
                filePool_.reset();
            }

            return static_cast<int>(mCSCEvents::iCommPortError);
        }

        stopping_.store(false);
        acceptingWork_.store(true);

        if (!startIoContextThread())
        {
            acceptingWork_.store(false);
            stopping_.store(true);

            boost::system::error_code ec;
            pSerialPort_->close(ec);
            pSerialPort_.reset();
            workGuard_.reset();

            if (filePool_)
            {
                filePool_->stop();
                filePool_->join();
                filePool_.reset();
            }

            return static_cast<int>(mCSCEvents::iCommPortError);
        }

        Logger::getInstance()->FnLog(
            "LCSC: [INIT] Success | Port=" + comPortName +
                " | Baud=" + std::to_string(baudRate),
            logFileName_,
            "LCSC");

        // Start all asynchronous protocol work only after the object is fully
        // initialized and the module-owned I/O thread is running.
        boost::asio::post(
            ioContext_,
            [this]()
            {
                if (stopping_.load())
                {
                    return;
                }

                startRead();
                FnSendGetLoginCmd();
            });

        return 1;
    }
    catch (const boost::filesystem::filesystem_error& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("FnLCSCReaderInit, Filesystem exception: ") + e.what());
    }
    catch (const boost::system::system_error& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("FnLCSCReaderInit, Boost.Asio exception: ") + e.what());
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("FnLCSCReaderInit, Exception: ") + e.what());
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("FnLCSCReaderInit, Exception: Unknown Exception");
    }

    acceptingWork_.store(false);
    stopping_.store(true);

    if (pSerialPort_)
    {
        boost::system::error_code ec;
        pSerialPort_->close(ec);
    }

    if (ioContextThread_.joinable())
    {
        // Initialization has already failed; this is an emergency cleanup
        // path, so stopping the context is preferable to leaving a live
        // thread behind.
        ioContext_.stop();
        ioContextThread_.join();
        moduleRunning_.store(false);
    }

    pSerialPort_.reset();
    workGuard_.reset();

    if (filePool_)
    {
        filePool_->stop();
        filePool_->join();
        filePool_.reset();
    }

    return static_cast<int>(mCSCEvents::iCommPortError);
}

bool LCSCReader::startIoContextThread()
{
    if (ioContextThread_.joinable())
    {
        return true;
    }

    try
    {
        ioContextThread_ =
            std::thread(
                [this]()
                {
#if defined(__linux__)
                    ::pthread_setname_np(::pthread_self(), "LCSC_IO");
#endif
                    ioContext_.run();
                });

        moduleRunning_.store(true);
        return true;
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLog(
            std::string("LCSC: [THREAD] Start failed | Error=") +
                e.what(),
            logFileName_,
            "LCSC");
        return false;
    }
}

void LCSCReader::FnLCSCReaderClose()
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);

    if (ioContext_.get_executor().running_in_this_thread())
    {
        Logger::getInstance()->FnLog(
            "LCSC: [SHUTDOWN] Rejected | Reason=Called from LCSC I/O thread",
            logFileName_,
            "LCSC");
        return;
    }

    acceptingWork_.store(false);
    stopping_.store(true);

    if (!ioContextThread_.joinable())
    {
        if (pSerialPort_)
        {
            boost::system::error_code ec;
            pSerialPort_->cancel(ec);
            pSerialPort_->close(ec);
            pSerialPort_.reset();
        }

        workGuard_.reset();

        if (filePool_)
        {
            filePool_->join();
            filePool_.reset();
        }

        resetRuntimeState();
        moduleRunning_.store(false);
        stopping_.store(false);
        return;
    }

    Logger::getInstance()->FnLog("LCSC: [SHUTDOWN] Begin", logFileName_, "LCSC");

    auto shutdownPromise = std::make_shared<std::promise<void>>();
    auto shutdownFuture = shutdownPromise->get_future();

    boost::asio::post(
        ioContext_,
        [this, shutdownPromise]()
        {
            shutdownOnIoThread();
            shutdownPromise->set_value();
        });

    shutdownFuture.wait();

    // Keep the context alive until cancellation/close has been issued on the
    // I/O thread, then allow all already-queued completion handlers to drain.
    workGuard_.reset();

    if (ioContextThread_.joinable())
    {
        ioContextThread_.join();
    }

    // Wait for already-started blocking jobs while stopping_ is still true.
    // Their completion paths therefore cannot restart the FSM during close.
    if (filePool_)
    {
        filePool_->join();
        filePool_.reset();
    }

    pSerialPort_.reset();
    resetRuntimeState();

    moduleRunning_.store(false);
    stopping_.store(false);

    Logger::getInstance()->FnLog("LCSC: [SHUTDOWN] Completed", logFileName_, "LCSC");
}

void LCSCReader::shutdownOnIoThread()
{
    boost::system::error_code ec;

    rspTimer_.cancel(ec);
    serialWriteDelayTimer_.cancel(ec);
    serialWriteTimer_.cancel(ec);

    if (pSerialPort_)
    {
        pSerialPort_->cancel(ec);
        pSerialPort_->close(ec);
    }

    commandQueue_.clear();
    chunkCommandQueue_.clear();

    // Do not clear writeQueue_ here. async_write() may still reference
    // writeQueue_.front(). The queue is cleared only after io_context.run()
    // has drained and the I/O thread has been joined.
    writeInProgress_ = false;
    writeTimedOut_ = false;
    currentState_ = STATE::IDLE;
    currentUploadLcscFilesState_ = UPLOAD_LCSC_FILES_STATE::IDLE;
}

void LCSCReader::resetRuntimeState()
{
    commandQueue_.clear();
    chunkCommandQueue_.clear();

    while (!writeQueue_.empty())
    {
        writeQueue_.pop();
    }

    currentCmd_ = LCSC_CMD::GET_STATUS_CMD;
    currentState_ = STATE::IDLE;
    currentUploadLcscFilesState_ = UPLOAD_LCSC_FILES_STATE::IDLE;

    writeInProgress_ = false;
    writeTimedOut_ = false;

    rxState_ = RX_STATE::RX_START;
    resetRxBuffer();

    continueReadFlag_.store(false);
    LCSCCard_In.store(0);
    lastSerialReadTime_ = std::chrono::steady_clock::now();
}

void LCSCReader::setCurrentCmd(LCSCReader::LCSC_CMD cmd)
{
    currentCmd_ = cmd;
}

LCSCReader::LCSC_CMD LCSCReader::getCurrentCmd() const
{
    return currentCmd_;
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
        case LCSC_CMD::LOGOUT:
        {
            returnStr = "LOGOUT";
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

std::string LCSCReader::toHexString(const std::vector<uint8_t>& data) const
{
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');

    for (const std::uint8_t byte : data)
    {
        oss << std::setw(2)
            << static_cast<unsigned int>(byte);
    }

    return oss.str();
}

void LCSCReader::enqueueCommand(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> data)
{
    if (!acceptingWork_.load())
    {
        return;
    }

    boost::asio::dispatch(
        ioContext_,
        [this, cmd, data = std::move(data)]() mutable
        {
            enqueueCommandOnIoThread(cmd, std::move(data));
        });
}

void LCSCReader::enqueueCommandToFront(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> data)
{
    if (!acceptingWork_.load())
    {
        return;
    }

    boost::asio::dispatch(
        ioContext_,
        [this, cmd, data = std::move(data)]() mutable
        {
            enqueueCommandToFrontOnIoThread(cmd, std::move(data));
        });
}

void LCSCReader::enqueueChunkCommand(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> data)
{
    if (!acceptingWork_.load())
    {
        return;
    }

    boost::asio::dispatch(
        ioContext_,
        [this, cmd, data = std::move(data)]() mutable
        {
            enqueueChunkCommandOnIoThread(cmd, std::move(data));
        });
}

void LCSCReader::enqueueCommandOnIoThread(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> data)
{
    if (stopping_.load() ||
        !pSerialPort_ ||
        !pSerialPort_->is_open())
    {
        Logger::getInstance()->FnLog(
            "LCSC: [QUEUE] Rejected | Cmd=" +
                getCommandString(cmd) +
                " | Reason=Serial port not open",
            logFileName_,
            "LCSC");
        return;
    }

    commandQueue_.emplace_back(cmd, std::move(data));

    Logger::getInstance()->FnLog(
        "LCSC: [QUEUE] Enqueued | Cmd=" +
            getCommandString(cmd) +
            " | Size=" +
            std::to_string(commandQueue_.size()),
        logFileName_,
        "LCSC");

    if (currentState_ == STATE::IDLE)
    {
        checkCommandQueue();
    }
}

void LCSCReader::enqueueCommandToFrontOnIoThread(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> data)
{
    if (stopping_.load() ||
        !pSerialPort_ ||
        !pSerialPort_->is_open())
    {
        Logger::getInstance()->FnLog(
            "LCSC: [QUEUE] Rejected | Cmd=" +
                getCommandString(cmd) +
                " | Reason=Serial port not open",
            logFileName_,
            "LCSC");
        return;
    }

    if (!commandQueue_.empty() &&
        commandQueue_.front().cmd == cmd)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [QUEUE] Ignored | Cmd=" +
                getCommandString(cmd) +
                " | Reason=Already at front",
            logFileName_,
            "LCSC");
        return;
    }

    commandQueue_.emplace_front(cmd, std::move(data));

    Logger::getInstance()->FnLog(
        "LCSC: [QUEUE] Enqueued front | Cmd=" +
            getCommandString(cmd) +
            " | Size=" +
            std::to_string(commandQueue_.size()),
        logFileName_,
        "LCSC");

    if (currentState_ == STATE::IDLE)
    {
        checkCommandQueue();
    }
}

void LCSCReader::enqueueChunkCommandOnIoThread(LCSCReader::LCSC_CMD cmd, std::shared_ptr<void> data)
{
    if (stopping_.load() ||
        !pSerialPort_ ||
        !pSerialPort_->is_open())
    {
        return;
    }

    chunkCommandQueue_.emplace_back(cmd, std::move(data));
}

void LCSCReader::checkCommandQueue()
{
    if (stopping_.load() ||
        currentState_ != STATE::IDLE ||
        commandQueue_.empty())
    {
        return;
    }

    const LCSC_CMD cmd = commandQueue_.front().cmd;

    if (isChunkedCommand(cmd))
    {
        processEvent(EVENT::CHUNK_COMMAND_ENQUEUED);
    }
    else
    {
        processEvent(EVENT::COMMAND_ENQUEUED);
    }
}

void LCSCReader::clearChunkCommandQueue()
{
    const std::size_t queueSize = chunkCommandQueue_.size();

    chunkCommandQueue_.clear();

    Logger::getInstance()->FnLog(
        "LCSC: [QUEUE] Chunk queue cleared | Count=" +
            std::to_string(queueSize),
        logFileName_,
        "LCSC");
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
        // A complete serial frame has arrived, but it has not been validated
        // yet. Stay in this state until handleReceivedCmd() classifies it.
        {EVENT::RESPONSE_RECEIVED                       , &LCSCReader::handleWaitingForResponseState                    , STATE::WAITING_FOR_RESPONSE                   },
        {EVENT::RESPONSE_HANDLED                        , &LCSCReader::handleWaitingForResponseState                    , STATE::IDLE                                   },
        {EVENT::RESPONSE_REJECTED                       , &LCSCReader::handleWaitingForResponseState                    , STATE::IDLE                                   },
        {EVENT::RESPONSE_TIMEOUT                        , &LCSCReader::handleWaitingForResponseState                    , STATE::IDLE                                   }
    }},
    {STATE::SENDING_CHUNK_COMMAND_REQUEST_ASYNC,
    {
        {EVENT::WRITE_COMPLETED                         , &LCSCReader::handleSendingChunkCommandRequestAsyncState       , STATE::WAITING_FOR_CHUNK_COMMAND_RESPONSE     },
        {EVENT::WRITE_FAILED                            , &LCSCReader::handleSendingChunkCommandRequestAsyncState       , STATE::IDLE                                   },
        {EVENT::WRITE_TIMEOUT                           , &LCSCReader::handleSendingChunkCommandRequestAsyncState       , STATE::IDLE                                   },
        // Chunk preparation can fail before async_write() is started.
        {EVENT::CHUNK_COMMAND_ERROR                     , &LCSCReader::handleSendingChunkCommandRequestAsyncState       , STATE::IDLE                                   }
    }},
    {STATE::WAITING_FOR_CHUNK_COMMAND_RESPONSE,
    {
        {EVENT::RESPONSE_RECEIVED                       , &LCSCReader::handleWaitingForChunkCommandResponseState        , STATE::WAITING_FOR_CHUNK_COMMAND_RESPONSE     },
        {EVENT::RESPONSE_REJECTED                       , &LCSCReader::handleWaitingForChunkCommandResponseState        , STATE::IDLE                                   },
        {EVENT::RESPONSE_TIMEOUT                        , &LCSCReader::handleWaitingForChunkCommandResponseState        , STATE::IDLE                                   },
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
        case EVENT::RESPONSE_HANDLED:
        {
            returnStr = "RESPONSE_HANDLED";
            break;
        }
        case EVENT::RESPONSE_REJECTED:
        {
            returnStr = "RESPONSE_REJECTED";
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
        case EVENT::EVENT_COUNT:
        default:
        {
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
        case STATE::STATE_COUNT:
        default:
        {
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
            //------- added on 15/07/2026
            LCSCCard_In = 1;
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
        default:
        {
            break;
        }
    }

    return retStr;
}

void LCSCReader::processEvent(EVENT event)
{
    boost::asio::post(
        ioContext_,
        [this, event]()
        {
            if (stopping_.load())
            {
                return;
            }

            const int currentStateIndex = static_cast<int>(currentState_);

            if (currentStateIndex < 0 ||
                currentStateIndex >= static_cast<int>(STATE::STATE_COUNT))
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [FSM] Invalid state | Recovery=IDLE",
                    logFileName_,
                    "LCSC");

                currentState_ = STATE::IDLE;
                checkCommandQueue();
                return;
            }

            const auto& stateTransitions = stateTransitionTable[currentStateIndex].transitions;

            for (const auto& transition : stateTransitions)
            {
                if (transition.event != event)
                {
                    continue;
                }

                const STATE previousState = currentState_;

                Logger::getInstance()->FnLog(
                    "LCSC: [FSM] Transition | From=" +
                        stateToString(previousState) +
                        " | Event=" +
                        eventToString(event) +
                        " | To=" +
                        stateToString(
                            transition.nextState),
                    logFileName_,
                    "LCSC");

                if (transition.eventHandler != nullptr)
                {
                    (this->*transition.eventHandler)(event);
                }

                currentState_ = transition.nextState;

                if (currentState_ == STATE::IDLE)
                {
                    // Post the next queue check so any event generated by the
                    // handler above observes the completed state transition.
                    boost::asio::post(
                        ioContext_,
                        [this]()
                        {
                            if (!stopping_.load())
                            {
                                checkCommandQueue();
                            }
                        });
                }

                return;
            }

            Logger::getInstance()->FnLog(
                "LCSC: [FSM] Event ignored | State=" +
                    stateToString(currentState_) +
                    " | Event=" +
                    eventToString(event),
                logFileName_,
                "LCSC");
        });
}

bool LCSCReader::isChunkedCommand(LCSCReader::LCSC_CMD cmd)
{
    switch (cmd)
    {
        case LCSC_CMD::UPLOAD_CFG_FILE:
        case LCSC_CMD::UPLOAD_CIL_FILE:
        case LCSC_CMD::UPLOAD_BL_FILE:
            return true;

        default:
            return false;
    }
}

bool LCSCReader::isChunkedCommandType(LCSCReader::LCSC_CMD_TYPE cmd)
{
    switch (cmd)
    {
        case LCSC_CMD_TYPE::BL_UPLOAD:
        case LCSC_CMD_TYPE::CIL_UPLOAD:
        case LCSC_CMD_TYPE::CFG_UPLOAD:
            return true;

        default:
            return false;
    }
}

void LCSCReader::popFromCommandQueueAndEnqueueWrite()
{
    if (commandQueue_.empty())
    {
        Logger::getInstance()->FnLog("LCSC: [QUEUE] Empty", logFileName_, "LCSC");
        return;
    }

    CommandWithData cmdData = std::move(commandQueue_.front());
    commandQueue_.pop_front();

    setCurrentCmd(cmdData.cmd);

    Logger::getInstance()->FnLog(
        "LCSC: [QUEUE] Dequeued | Cmd=" +
            getCommandString(cmdData.cmd) +
            " | Remaining=" +
            std::to_string(commandQueue_.size()),
        logFileName_,
        "LCSC");

    enqueueWrite(prepareCmd(cmdData.cmd, std::move(cmdData.data)));
}

void LCSCReader::popFromChunkCommandQueueAndEnqueueWrite()
{
    try
    {
        if (commandQueue_.empty())
        {
            Logger::getInstance()->FnLog(
                "LCSC: [QUEUE] Empty | Type=Chunk command",
                logFileName_,
                "LCSC");
            return;
        }

        CommandWithData cmdData = std::move(commandQueue_.front());
        commandQueue_.pop_front();

        setCurrentCmd(cmdData.cmd);

        auto chunksData = std::static_pointer_cast<std::vector<std::vector<uint8_t>>>(cmdData.data);

        if (!chunksData ||
            chunksData->empty())
        {
            Logger::getInstance()->FnLog(
                "LCSC: [CHUNK] Prepare failed | Cmd=" +
                    getCommandString(cmdData.cmd) +
                    " | Reason=No chunk data",
                logFileName_,
                "LCSC");

            processEvent(EVENT::CHUNK_COMMAND_ERROR);
            return;
        }

        for (const auto& chunk : *chunksData)
        {
            enqueueChunkCommandOnIoThread(cmdData.cmd, std::make_shared<std::vector<uint8_t>>(chunk));
        }

        Logger::getInstance()->FnLog(
            "LCSC: [CHUNK] Prepared | Cmd=" +
                getCommandString(cmdData.cmd) +
                " | Chunks=" +
                std::to_string(
                    chunkCommandQueue_.size()),
            logFileName_,
            "LCSC");

        sendNextChunkCommandData();
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLogExceptionError(std::string("popFromChunkCommandQueueAndEnqueueWrite, Exception: ") + e.what());

        processEvent( EVENT::CHUNK_COMMAND_ERROR);
    }
    catch (...)
    {
        Logger::getInstance()->FnLogExceptionError("popFromChunkCommandQueueAndEnqueueWrite, Exception: Unknown Exception");

        processEvent(EVENT::CHUNK_COMMAND_ERROR);
    }
}

void LCSCReader::sendNextChunkCommandData()
{
    if (chunkCommandQueue_.empty())
    {
        Logger::getInstance()->FnLog(
            "LCSC: [CHUNK] No pending chunk",
            logFileName_,
            "LCSC");
        return;
    }

    CommandWithData cmdData = std::move(chunkCommandQueue_.front());
    chunkCommandQueue_.pop_front();

    enqueueWrite(prepareCmd(cmdData.cmd, std::move(cmdData.data)));
}

void LCSCReader::handleIdleState(LCSCReader::EVENT event)
{
    if (event == EVENT::COMMAND_ENQUEUED)
    {
        popFromCommandQueueAndEnqueueWrite();
        startSerialWriteTimer(NORMAL_CMD_WRITE_TIMEOUT);
    }
    else if (event == EVENT::CHUNK_COMMAND_ENQUEUED)
    {
        popFromChunkCommandQueueAndEnqueueWrite();
        startSerialWriteTimer(CHUNK_CMD_WRITE_TIMEOUT);
    }
}

void LCSCReader::handleSendingRequestAsyncState(LCSCReader::EVENT event)
{
    if (event == EVENT::WRITE_COMPLETED)
    {
        boost::system::error_code ec;
        serialWriteTimer_.cancel(ec);
        startResponseTimer();

        Logger::getInstance()->FnLog(
            "LCSC: [TX] Completed | Cmd=" +
                getCommandString(getCurrentCmd()) +
                " | Action=Wait response",
            logFileName_,
            "LCSC");
    }
    else if (event == EVENT::WRITE_FAILED)
    {
        boost::system::error_code ec;
        serialWriteTimer_.cancel(ec);

        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sSendcmdfail);
    }
    else if (event == EVENT::WRITE_TIMEOUT)
    {
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sSendcmdfail);
    }
}

void LCSCReader::handleWaitingForResponseState(LCSCReader::EVENT event)
{
    if (event == EVENT::RESPONSE_TIMEOUT)
    {
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sTimeout);
    }
    else if (event == EVENT::RESPONSE_RECEIVED)
    {
        const auto frame = getRxBuffer();
        resetRxBuffer();
        handleReceivedCmd(frame);
    }
    else if (event == EVENT::RESPONSE_HANDLED)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [RSP] Completed | Cmd=" +
                getCommandString(getCurrentCmd()),
            logFileName_,
            "LCSC");
    }
    else if (event == EVENT::RESPONSE_REJECTED)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [RSP] Rejected | Cmd=" +
                getCommandString(getCurrentCmd()),
            logFileName_,
            "LCSC");
    }
}

void LCSCReader::handleSendingChunkCommandRequestAsyncState(EVENT event)
{
    if (event == EVENT::WRITE_COMPLETED)
    {
        boost::system::error_code ec;
        serialWriteTimer_.cancel(ec);
        startResponseTimer();

        Logger::getInstance()->FnLog(
            "LCSC: [CHUNK] Write completed | Cmd=" +
                getCommandString(getCurrentCmd()) +
                " | Remaining=" +
                std::to_string(
                    chunkCommandQueue_.size()),
            logFileName_,
            "LCSC");
    }
    else if (event == EVENT::WRITE_FAILED)
    {
        boost::system::error_code ec;
        serialWriteTimer_.cancel(ec);

        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sSendcmdfail);
        clearChunkCommandQueue();
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
    }
    else if (event == EVENT::WRITE_TIMEOUT)
    {
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sSendcmdfail);
        clearChunkCommandQueue();
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
    }
    else if (event == EVENT::CHUNK_COMMAND_ERROR)
    {
        boost::system::error_code ec;
        serialWriteTimer_.cancel(ec);

        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::rNotRespCmd);
        clearChunkCommandQueue();
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
    }
}

void LCSCReader::handleWaitingForChunkCommandResponseState(EVENT event)
{
    if (event == EVENT::RESPONSE_TIMEOUT)
    {
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::sTimeout);
        clearChunkCommandQueue();
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
    }
    else if (event == EVENT::RESPONSE_RECEIVED)
    {
        const auto frame = getRxBuffer();
        resetRxBuffer();
        handleReceivedCmd(frame);
    }
    else if (event == EVENT::RESPONSE_REJECTED)
    {
        clearChunkCommandQueue();
    }
    else if (event == EVENT::SEND_NEXT_CHUNK_COMMAND)
    {
        sendNextChunkCommandData();

        // Every chunk needs its own write timeout.
        startSerialWriteTimer(CHUNK_CMD_WRITE_TIMEOUT);
    }
    else if (event == EVENT::ALL_CHUNK_COMMAND_COMPLETED)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [CHUNK] Upload completed | Cmd=" +
                getCommandString(getCurrentCmd()),
            logFileName_,
            "LCSC");
    }
    else if (event == EVENT::CHUNK_COMMAND_ERROR)
    {
        handleCmdErrorOrTimeout(getCurrentCmd(), mCSCEvents::rNotRespCmd);
        clearChunkCommandQueue();
    }
}

void LCSCReader::startSerialWriteTimer(int seconds)
{
    if (stopping_.load())
    {
        return;
    }

    boost::system::error_code ec;
    serialWriteTimer_.cancel(ec);

    serialWriteTimer_.expires_after(std::chrono::seconds(seconds));

    serialWriteTimer_.async_wait(
        [this](
            const boost::system::error_code& error)
        {
            handleSerialWriteTimeout(error);
        });
}

void LCSCReader::handleSerialWriteTimeout(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted ||
        stopping_.load())
    {
        return;
    }

    if (error)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [TX] Write timer error | Cmd=" +
                getCommandString(getCurrentCmd()) +
                " | Error=" +
                error.message(),
            logFileName_,
            "LCSC");
    }
    else
    {
        Logger::getInstance()->FnLog(
            "LCSC: [TX] Timeout | Cmd=" +
                getCommandString(getCurrentCmd()),
            logFileName_,
            "LCSC");
    }

    if (!writeInProgress_)
    {
        processEvent(EVENT::WRITE_TIMEOUT);
        return;
    }

    // Do not destroy writeQueue_.front() while async_write() may still be
    // using it. Mark the timeout and cancel the serial operation. The actual
    // WRITE_TIMEOUT event is generated from writeEnd() after Asio releases
    // the buffer.
    writeTimedOut_ = true;

    if (pSerialPort_ &&
        pSerialPort_->is_open())
    {
        boost::system::error_code ec;
        pSerialPort_->cancel(ec);
    }
}

void LCSCReader::startResponseTimer()
{
    if (stopping_.load())
    {
        return;
    }

    boost::system::error_code ec;
    rspTimer_.cancel(ec);

    rspTimer_.expires_after(std::chrono::seconds(2));

    rspTimer_.async_wait(
        [this](
            const boost::system::error_code& error)
        {
            handleCmdResponseTimeout(error);
        });
}

void LCSCReader::handleCmdResponseTimeout(const boost::system::error_code& error)
{
    if (error == boost::asio::error::operation_aborted ||
        stopping_.load())
    {
        return;
    }

    if (error)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [RSP] Timer error | Cmd=" +
                getCommandString(getCurrentCmd()) +
                " | Error=" +
                error.message(),
            logFileName_,
            "LCSC");
    }
    else
    {
        Logger::getInstance()->FnLog(
            "LCSC: [RSP] Timeout | Cmd=" +
                getCommandString(getCurrentCmd()),
            logFileName_,
            "LCSC");
    }

    processEvent(EVENT::RESPONSE_TIMEOUT);
}

uint16_t LCSCReader::CRC16_CCITT(const uint8_t* inStr, std::size_t length)
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
    if (stopping_.load() ||
        !pSerialPort_ ||
        !pSerialPort_->is_open())
    {
        return;
    }

    pSerialPort_->async_read_some(
        boost::asio::buffer(
            readBuffer_,
            readBuffer_.size()),
        [this](
            const boost::system::error_code& error,
            std::size_t bytesTransferred)
        {
            readEnd(error, bytesTransferred);
        });
}

void LCSCReader::readEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    lastSerialReadTime_ = std::chrono::steady_clock::now();

    if (stopping_.load())
    {
        return;
    }

    if (!error)
    {
        const std::vector<uint8_t> data(readBuffer_.begin(), readBuffer_.begin() + static_cast<std::ptrdiff_t>(bytesTransferred));

        if (isRxResponseComplete(data))
        {
            processEvent(EVENT::RESPONSE_RECEIVED);
        }

        startRead();
        return;
    }

    if (error == boost::asio::error::operation_aborted)
    {
        // serial_port::cancel() is also used to terminate an overdue write.
        // The read operation is cancelled at the same time, so re-arm it
        // unless the module itself is shutting down.
        if (pSerialPort_ &&
            pSerialPort_->is_open())
        {
            startRead();
        }
        return;
    }

    Logger::getInstance()->FnLog(
        "LCSC: [RX] Read error | Error=" +
            error.message(),
        logFileName_,
        "LCSC");

    // Preserve the original behaviour of keeping the receive loop alive for
    // recoverable serial errors.
    if (pSerialPort_ &&
        pSerialPort_->is_open())
    {
        startRead();
    }
}

bool LCSCReader::isRxResponseComplete(const std::vector<uint8_t>& dataBuff)
{
    for (const uint8_t data : dataBuff)
    {
        switch (rxState_)
        {
            case RX_STATE::RX_START:
            {
                if (data == 0xA5)
                {
                    resetRxBuffer();

                    rxBuffer_[rxNum_++] = data;
                    rxState_ = RX_STATE::RX_RECEIVING;
                }
                break;
            }

            case RX_STATE::RX_RECEIVING:
            {
                if (rxNum_ >= rxBuffer_.size())
                {
                    Logger::getInstance()->FnLog(
                        "LCSC: [RX] Frame rejected | Reason=Buffer overflow",
                        logFileName_,
                        "LCSC");

                    resetRxBuffer();
                    rxState_ = RX_STATE::RX_START;

                    // A5 may also be the beginning of a new frame.
                    if (data == 0xA5)
                    {
                        rxBuffer_[rxNum_++] = data;
                        rxState_ = RX_STATE::RX_RECEIVING;
                    }
                    break;
                }

                rxBuffer_[rxNum_++] = data;

                if (rxNum_ < 4)
                {
                    break;
                }

                const std::uint16_t dataLen = (static_cast<std::uint16_t>(rxBuffer_[2]) << 8) | static_cast<std::uint16_t>(rxBuffer_[3]);

                if (dataLen < 6 ||
                    dataLen > rxBuffer_.size())
                {
                    Logger::getInstance()->FnLog(
                        "LCSC: [RX] Frame rejected | Reason=Invalid length | Length=" +
                            std::to_string(dataLen),
                        logFileName_,
                        "LCSC");

                    resetRxBuffer();
                    rxState_ = RX_STATE::RX_START;
                    break;
                }

                if (rxNum_ == dataLen)
                {
                    // This protocol has one outstanding request at a time.
                    // Stop at the first complete frame so later bytes in the
                    // same read cannot overwrite the frame before the posted
                    // RESPONSE_RECEIVED event consumes it.
                    rxState_ = RX_STATE::RX_START;
                    return true;
                }
                else if (rxNum_ > dataLen)
                {
                    Logger::getInstance()->FnLog(
                        "LCSC: [RX] Frame rejected | Reason=Length overflow | Expected=" +
                            std::to_string(dataLen) +
                            " | Received=" +
                            std::to_string(rxNum_),
                        logFileName_,
                        "LCSC");

                    resetRxBuffer();
                    rxState_ = RX_STATE::RX_START;
                }

                break;
            }
        }
    }

    return false;
}

void LCSCReader::enqueueWrite(const std::vector<uint8_t>& data)
{
    boost::asio::dispatch(
        ioContext_,
        [this, data]()
        {
            if (stopping_.load())
            {
                return;
            }

            const bool wasWriting = writeInProgress_;

            writeQueue_.push(data);

            if (!wasWriting)
            {
                startWrite();
            }
        });
}

void LCSCReader::startWrite()
{
    if (stopping_.load())
    {
        return;
    }

    if (writeQueue_.empty())
    {
        writeInProgress_ = false;
        return;
    }

    if (!pSerialPort_ ||
        !pSerialPort_->is_open())
    {
        writeInProgress_ = false;

        Logger::getInstance()->FnLog(
            "LCSC: [TX] Failed | Cmd=" +
                getCommandString(getCurrentCmd()) +
                " | Reason=Serial port not open",
            logFileName_,
            "LCSC");

        processEvent(EVENT::WRITE_FAILED);
        return;
    }

    writeInProgress_ = true;
    writeTimedOut_ = false;

    const auto& data = writeQueue_.front();

    Logger::getInstance()->FnLog(
        "LCSC: [TX] Frame | Cmd=" +
            getCommandString(getCurrentCmd()) +
            " | Bytes=" +
            std::to_string(data.size()) +
            " | Hex=" +
            toHexString(data),
        logFileName_,
        "LCSC");

    boost::asio::async_write(
        *pSerialPort_,
        boost::asio::buffer(data),
        [this](
            const boost::system::error_code& error,
            std::size_t bytesTransferred)
        {
            writeEnd(error, bytesTransferred);
        });
}

void LCSCReader::writeEnd(const boost::system::error_code& error, std::size_t bytesTransferred)
{
    boost::system::error_code timerEc;
    serialWriteTimer_.cancel(timerEc);

    if (!writeQueue_.empty())
    {
        writeQueue_.pop();
    }

    writeInProgress_ = false;

    if (stopping_.load())
    {
        writeTimedOut_ = false;
        return;
    }

    if (writeTimedOut_)
    {
        writeTimedOut_ = false;
        processEvent(EVENT::WRITE_TIMEOUT);
        return;
    }

    if (!error)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [TX] Write completed | Cmd=" +
                getCommandString(getCurrentCmd()) +
                " | Bytes=" +
                std::to_string(bytesTransferred),
            logFileName_,
            "LCSC");

        processEvent(EVENT::WRITE_COMPLETED);
        return;
    }

    if (error == boost::asio::error::operation_aborted)
    {
        // Cancellation not associated with a timeout is expected only during
        // shutdown, which was handled above.
        return;
    }

    Logger::getInstance()->FnLog(
        "LCSC: [TX] Write failed | Cmd=" +
            getCommandString(getCurrentCmd()) +
            " | Error=" +
            error.message(),
        logFileName_,
        "LCSC");

    processEvent(EVENT::WRITE_FAILED);
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
    const auto rejectCurrentResponse =
        [this, &msgDataBuff](mCSCEvents status,
                            const std::string& reason)
        {
            boost::system::error_code ec;
            rspTimer_.cancel(ec);

            Logger::getInstance()->FnLog(
                "LCSC: [RX] Rejected | Cmd=" +
                    getCommandString(getCurrentCmd()) +
                    " | Reason=" + reason +
                    " | Bytes=" +
                    std::to_string(msgDataBuff.size()) +
                    " | Hex=" +
                    toHexString(msgDataBuff),
                logFileName_,
                "LCSC");

            handleCmdErrorOrTimeout(getCurrentCmd(), status);

            if (isChunkedCommand(getCurrentCmd()))
            {
                processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
            }

            processEvent(EVENT::RESPONSE_REJECTED);
        };

    if (msgDataBuff.size() < 6)
    {
        rejectCurrentResponse(mCSCEvents::rCorruptedCmd, "Frame too short");
        return;
    }

    const std::uint16_t declaredLength =
        (static_cast<std::uint16_t>(msgDataBuff[2]) << 8) |
        static_cast<std::uint16_t>(msgDataBuff[3]);

    if (msgDataBuff[0] != 0xA5)
    {
        rejectCurrentResponse(mCSCEvents::rCorruptedCmd, "Invalid attention code");
        return;
    }

    if (declaredLength != msgDataBuff.size())
    {
        rejectCurrentResponse(
            mCSCEvents::rCorruptedCmd,
            "Length mismatch (declared=" +
                std::to_string(declaredLength) +
                ", actual=" +
                std::to_string(msgDataBuff.size()) +
                ")");
        return;
    }

    CscPacket msg;
    msg.deserialize(msgDataBuff);

    const std::uint16_t calculatedCrc = CRC16_CCITT(msgDataBuff.data(), msgDataBuff.size() - 2);

    if (calculatedCrc != msg.getCrc())
    {
        std::ostringstream reason;
        reason << "CRC mismatch (expected=0x"
               << std::uppercase
               << std::hex
               << std::setw(4)
               << std::setfill('0')
               << calculatedCrc
               << ", received=0x"
               << std::setw(4)
               << msg.getCrc()
               << ')';

        rejectCurrentResponse(mCSCEvents::rCRCError, reason.str());
        return;
    }

    if (msg.getCode() != static_cast<std::uint8_t>(LCSC_CMD_CODE::RESPONSE))
    {
        rejectCurrentResponse(mCSCEvents::rNotRespCmd, "Packet code is not RESPONSE");
        return;
    }

    if (!isCurrentCmdResponse(getCurrentCmd(), msg.getType()))
    {
        rejectCurrentResponse(mCSCEvents::rNotRespCmd, "Response type mismatch (type=" + getCommandTypeString(msg.getType()) + ")");
        return;
    }

    // Every currently supported LCSC response starts with a result/status byte
    // and handleCmdResponse() indexes payload[0]. Reject an empty payload here
    // instead of allowing malformed input to reach the protocol parser.
    if (msg.getPayload().empty())
    {
        rejectCurrentResponse(mCSCEvents::rCorruptedCmd, "Empty response payload");
        return;
    }

    boost::system::error_code timerEc;
    rspTimer_.cancel(timerEc);

    Logger::getInstance()->FnLog(
        "LCSC: [RX] Parsed | Cmd=" +
            getCommandString(getCurrentCmd()) +
            " | Type=" +
            getCommandTypeString(msg.getType()) +
            " | Code=RESP" +
            " | Length=" +
            std::to_string(msg.getLength()) +
            " | PayloadBytes=" +
            std::to_string(msg.getPayload().size()),
        logFileName_,
        "LCSC");

    const std::string msgRsp = handleCmdResponse(msg);

    if (!msgRsp.empty())
    {
        const std::string eventName = getEventStringFromResponseCmdType(msg.getType());

        if (!eventName.empty())
        {
            EventManager::getInstance()->FnEnqueueEvent(eventName, msgRsp);

            Logger::getInstance()->FnLog(
                "LCSC: [EVENT] Queued | Name=" +
                    eventName +
                    " | Type=" +
                    getCommandTypeString(msg.getType()),
                logFileName_,
                "LCSC");
        }
    }

    // Chunk responses drive SEND_NEXT_CHUNK_COMMAND /
    // ALL_CHUNK_COMMAND_COMPLETED / CHUNK_COMMAND_ERROR from
    // handleCmdResponse(). A normal response is complete here.
    if (!isChunkedCommandType(static_cast<LCSC_CMD_TYPE>(msg.getType())))
    {
        processEvent(EVENT::RESPONSE_HANDLED);
    }
}

void LCSCReader::handleUploadLcscFilesCmdResponse(const CscPacket& msg, const std::string& msgRsp)
{
    if ((msg.getType() == static_cast<uint8_t>(LCSC_CMD_TYPE::BL_UPLOAD))
        || (msg.getType() == static_cast<uint8_t>(LCSC_CMD_TYPE::CIL_UPLOAD))
        || (msg.getType() == static_cast<uint8_t>(LCSC_CMD_TYPE::CFG_UPLOAD)))
    {
        std::string cdFilesRsp = msgRsp;
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
                Logger::getInstance()->FnLog("LCSC: [CARD] Response ignored | Reason=Continuous read disabled", logFileName_, "LCSC");
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
                Logger::getInstance()->FnLog("LCSC: [CARD] Card not detected | Action=Retry", logFileName_, "LCSC");
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
                Logger::getInstance()->FnLog("LCSC: [CARD] Response ignored | Reason=Continuous read disabled", logFileName_, "LCSC");
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
    const std::string status = std::to_string(static_cast<int>(eventStatus));

    Logger::getInstance()->FnLog(
        "LCSC: [CMD] Failed | Cmd=" + getCommandString(cmd) +
            " | Status=" + status,
        logFileName_,
        "LCSC");

    // A GET_STATUS issued by the CD-upload FSM is a dependency of CDACK
    // generation. If the command itself times out or cannot be sent, the
    // upload FSM must also be released from GENERATE_CDACKFILES.
    if (cmd == LCSC_CMD::GET_STATUS_CMD &&
        currentUploadLcscFilesState_ == UPLOAD_LCSC_FILES_STATE::GENERATE_CDACKFILES)
    {
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_FAILED);
    }

    // Continuous card polling is intentionally retried instead of publishing
    // a timeout/failure event while card detection remains enabled.
    if (cmd == LCSC_CMD::GET_CARD_ID)
    {
        if (continueReadFlag_.load())
        {
            Logger::getInstance()->FnLog("LCSC: [CARD] No card response | Action=Retry CardID", logFileName_, "LCSC");
            enqueueCommandToFront(LCSC_CMD::GET_CARD_ID);
        }
        else
        {
            Logger::getInstance()->FnLog("LCSC: [CARD] No card response | Action=Ignore | Reason=Continuous read disabled", logFileName_, "LCSC");
        }
        return;
    }

    if (cmd == LCSC_CMD::CARD_BALANCE)
    {
        if (continueReadFlag_.load())
        {
            Logger::getInstance()->FnLog("LCSC: [CARD] No balance response | Action=Retry Balance", logFileName_, "LCSC");
            enqueueCommandToFront(LCSC_CMD::CARD_BALANCE);
        }
        else
        {
            Logger::getInstance()->FnLog("LCSC: [CARD] No balance response | Action=Ignore | Reason=Continuous read disabled", logFileName_, "LCSC");
        }
        return;
    }

    std::string eventName;

    switch (cmd)
    {
        case LCSC_CMD::GET_STATUS_CMD:
            eventName = "Evt_LcscReaderStatus";
            break;
        case LCSC_CMD::LOGIN_1:
        case LCSC_CMD::LOGIN_2:
            eventName = "Evt_LcscReaderLogin";
            break;
        case LCSC_CMD::LOGOUT:
            eventName = "Evt_handleLcscReaderLogout";
            break;
        case LCSC_CMD::CARD_DEDUCT:
            eventName = "Evt_handleLcscReaderGetCardDeduct";
            break;
        case LCSC_CMD::CARD_RECORD:
            eventName = "Evt_handleLcscReaderGetCardRecord";
            break;
        case LCSC_CMD::CARD_FLUSH:
            eventName = "Evt_handleLcscReaderGetCardFlush";
            break;
        case LCSC_CMD::GET_TIME:
            eventName = "Evt_handleLcscReaderGetTime";
            break;
        case LCSC_CMD::SET_TIME:
            eventName = "Evt_handleLcscReaderSetTime";
            break;
        case LCSC_CMD::UPLOAD_CFG_FILE:
            eventName = "Evt_handleLcscReaderUploadCFGFile";
            break;
        case LCSC_CMD::UPLOAD_CIL_FILE:
            eventName = "Evt_handleLcscReaderUploadCILFile";
            break;
        case LCSC_CMD::UPLOAD_BL_FILE:
            eventName = "Evt_handleLcscReaderUploadBLFile";
            break;
        case LCSC_CMD::GET_CARD_ID:
        case LCSC_CMD::CARD_BALANCE:
            // Handled above.
            break;
    }

    if (eventName.empty())
    {
        return;
    }

    const std::string payload = "msgStatus=" + status;

    EventManager::getInstance()->FnEnqueueEvent(eventName, payload);

    Logger::getInstance()->FnLog(
        "LCSC: [EVENT] Queued | Name=" + eventName +
            " | Cmd=" + getCommandString(cmd) +
            " | Status=" + status,
        logFileName_,
        "LCSC");
}

void LCSCReader::FnLCSCReaderStopRead()
{
    continueReadFlag_.store(false);

    Logger::getInstance()->FnLog("LCSC: [CARD] Continuous read disabled", logFileName_, "LCSC");
}

void LCSCReader::FnSendGetStatusCmd()
{
    enqueueCommand(LCSC_CMD::GET_STATUS_CMD);
}

void LCSCReader::FnSendGetLoginCmd()
{
    enqueueCommand(LCSC_CMD::LOGIN_1);
}

void LCSCReader::FnSendGetLogoutCmd()
{
    enqueueCommand(LCSC_CMD::LOGOUT);
}

void LCSCReader::FnSendGetCardIDCmd()
{
    continueReadFlag_.store(true);

    Logger::getInstance()->FnLog("LCSC: [CARD] Continuous read enabled | Mode=CardID", logFileName_, "LCSC");

    enqueueCommand(LCSC_CMD::GET_CARD_ID);
}

void LCSCReader::FnSendGetCardBalance()
{
    continueReadFlag_.store(true);

    Logger::getInstance()->FnLog("LCSC: [CARD] Continuous read enabled | Mode=Balance", logFileName_, "LCSC");

    enqueueCommand(LCSC_CMD::CARD_BALANCE);
}

void LCSCReader::FnSendCardDeduct(uint32_t amount)
{
    std::shared_ptr<void> req_data = std::make_shared<uint32_t>(amount);
    enqueueCommand(LCSC_CMD::CARD_DEDUCT, req_data);
}

void LCSCReader::FnSendCardRecord()
{
    enqueueCommand(LCSC_CMD::CARD_RECORD);
}

void LCSCReader::FnSendCardFlush(uint32_t seed)
{
    std::shared_ptr<void> req_data = std::make_shared<uint32_t>(seed);
    enqueueCommand(LCSC_CMD::CARD_FLUSH, req_data);
}

void LCSCReader::FnSendGetTime()
{
    enqueueCommand(LCSC_CMD::GET_TIME);
}

void LCSCReader::FnSendSetTime()
{
    enqueueCommand(LCSC_CMD::SET_TIME);
}

std::vector<uint8_t> LCSCReader::readFile(const std::filesystem::path& filePath)
{
    try
    {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);

        if (!file.is_open())
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] Read failed | Path=" + filePath.string() +
                    " | Reason=Open failed",
                logFileName_,
                "LCSC");
            return {};
        }

        const std::streamsize fileSize = file.tellg();
        if (fileSize <= 0)
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] Read failed | Path=" + filePath.string() +
                    " | Reason=Invalid size | Bytes=" +
                    std::to_string(fileSize),
                logFileName_,
                "LCSC");
            return {};
        }

        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> fileData(static_cast<std::size_t>(fileSize));

        if (!file.read(reinterpret_cast<char*>(fileData.data()), fileSize))
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] Read failed | Path=" + filePath.string() +
                    " | Reason=I/O error | Bytes=" +
                    std::to_string(fileSize),
                logFileName_,
                "LCSC");
            return {};
        }

        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Read completed | Path=" + filePath.string() +
                " | Bytes=" + std::to_string(fileData.size()),
            logFileName_,
            "LCSC");

        return fileData;
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLog(
            std::string("LCSC: [FILE] Read exception | Path=") +
                filePath.string() + " | Error=" + e.what(),
            logFileName_,
            "LCSC");
        return {};
    }
    catch (...)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Read exception | Path=" + filePath.string() +
                " | Error=Unknown exception",
            logFileName_,
            "LCSC");
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
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Prepare upload | Cmd=UPLOAD_CFG_FILE | Path=" + path,
            logFileName_,
            "LCSC");

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
                oss << "LCSC: [FILE] Upload prepare failed | Path=" << filePath << " | Reason=Invalid or empty file";
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            }
        }
        else
        {
            // Todo: need to raise event - sendFailed
            std::ostringstream oss;
            oss << "LCSC: [FILE] Upload prepare failed | Path=" << filePath << " | Reason=File not found or not regular";
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
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Prepare upload | Cmd=UPLOAD_CIL_FILE | Path=" + path,
            logFileName_,
            "LCSC");

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
                oss << "LCSC: [FILE] Upload prepare failed | Path=" << filePath << " | Reason=Invalid or empty file";
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            }
        }
        else
        {
            // Todo: need to raise event - sendFailed
            std::ostringstream oss;
            oss << "LCSC: [FILE] Upload prepare failed | Path=" << filePath << " | Reason=File not found or not regular";
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
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Prepare upload | Cmd=UPLOAD_BL_FILE | Path=" + path,
            logFileName_,
            "LCSC");

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
                oss << "LCSC: [FILE] Upload prepare failed | Path=" << filePath << " | Reason=Invalid or empty file";
                Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
            }
        }
        else
        {
            // Todo: need to raise event - sendFailed
            std::ostringstream oss;
            oss << "LCSC: [FILE] Upload prepare failed | Path=" << filePath << " | Reason=File not found or not regular";
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
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] CDACK move failed | Reason=Mount failed | Share=" +
                    sharedFolderPath,
                logFileName_,
                "LCSC");
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

                    Logger::getInstance()->FnLog(
                        "LCSC: [FILE] CDACK moved | From=" +
                            entry.path().string() +
                            " | To=" + dest_file.string(),
                        logFileName_,
                        "LCSC");
                }
            }
        }
        else
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] CDACK move failed | Reason=Local folder unavailable | Path=" +
                    cdAckFilePath,
                logFileName_,
                "LCSC");
            return false;
        }

        return true;
    }
    else
    {
        Logger::getInstance()->FnLog("LCSC: [FILE] CDACK move failed | Reason=Ping failed", logFileName_, "LCSC");
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
        Logger::getInstance()->FnLog("LCSC: [FILE] SHA256 failed | Error=" + std::string(e.what()), logFileName_, "LCSC");
        return {};
    }
    catch (...)
    {
        Logger::getInstance()->FnLog("LCSC: [FILE] SHA256 failed | Error=Unknown exception", logFileName_, "LCSC");
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
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] CDACK directory failed | Path=" + cdAckFilePath +
                    " | Error=" + ec.message(),
                logFileName_,
                "LCSC");
            return false;
        }
        else
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] CDACK directory created | Path=" + cdAckFilePath,
                logFileName_,
                "LCSC");
        }
    }
    else
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] CDACK directory ready | Path=" + cdAckFilePath,
            logFileName_,
            "LCSC");
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

    std::string ackFileName = "";
    if (operation::getInstance()->tParas.giEPS == 3)
    {
        ackFileName = boost::algorithm::trim_copy(operation::getInstance()->tParas.gsCPOID) + "_" + operation::getInstance()->tParas.gsCPID + "_CSCR"
                    + "0" + terminalID + "_" + Common::getInstance()->FnGetDateTimeFormat_yyyymmdd_hhmmss() + ".cdack";
    }
    else
    {
        ackFileName = boost::algorithm::trim_copy(operation::getInstance()->tParas.gsCPOID) + "_" + operation::getInstance()->tParas.gsCPID + "_CR"
                    + "0" + terminalID + "_" + Common::getInstance()->FnGetDateTimeFormat_yyyymmdd_hhmmss() + ".cdack";
    }
    std::string sAckFile = cdAckFilePath + "/" + ackFileName;
    
    // Construct data contents
    std::string sHeader;
    std::string sData;
    std::string sDataO;

    if (operation::getInstance()->tParas.giEPS == 3)
    {
        sHeader = "H" + Common::getInstance()->FnPadRightSpace(58, ackFileName) + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss() + fwVer;
    }
    else
    {
        sHeader = "H" + Common::getInstance()->FnPadRightSpace(53, ackFileName) + Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss() + fwVer;
    }
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
        Logger::getInstance()->FnLog("LCSC: [FILE] CDACK generation failed | Reason=SHA256 failed", logFileName_, "LCSC");
    }

    sData = sData + hash + '\n';

    if (!(outFile << sData))
    {
        Logger::getInstance()->FnLog("LCSC: [FILE] CDACK write failed | Path=" + sAckFile, logFileName_, "LCSC");
        outFile.close();
        return false;
    }
    else
    {
        Logger::getInstance()->FnLog("LCSC: [FILE] CDACK written | Path=" + sAckFile, logFileName_, "LCSC");
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
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] CD download failed | Reason=Mount failed | Share=" +
                    sharedFolderPath,
                logFileName_,
                "LCSC");
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
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] CD download skipped | Reason=No remote files",
                logFileName_,
                "LCSC");
            return false;
        }

        // Create the output folder if it doesn't exist
        if (!std::filesystem::exists(outputFolderPath))
        {
            std::error_code ec;
            if (!std::filesystem::create_directories(outputFolderPath, ec))
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [FILE] CD local directory failed | Path=" +
                        outputFolderPath + " | Error=" + ec.message(),
                    logFileName_,
                    "LCSC");
                return false;
            }
            else
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [FILE] CD local directory created | Path=" +
                        outputFolderPath,
                    logFileName_,
                    "LCSC");
            }
        }
        else
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] CD local directory ready | Path=" +
                    outputFolderPath,
                logFileName_,
                "LCSC");
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
                    oss << "LCSC: [FILE] CD file downloaded | From=" <<
                            entry.path().string() << " | To=" << dest_file.string();
                    Logger::getInstance()->FnLog(oss.str());
                    Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");
                }
            }
        }
        else
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] CD download failed | Reason=Remote folder unavailable | Path=" +
                    folder.string(),
                logFileName_,
                "LCSC");
            return false;
        }

        std::ostringstream oss;
        oss << "LCSC: [FILE] CD download completed | Files=" << std::to_string(downloadTotal);
        Logger::getInstance()->FnLog(oss.str());
        Logger::getInstance()->FnLog(oss.str(), logFileName_, "LCSC");

        return true;
    }
    else
    {
        Logger::getInstance()->FnLog("LCSC: [FILE] CD download failed | Reason=Ping failed", logFileName_, "LCSC");
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
        // Upload completion only starts local-file cleanup. Keep the FSM busy
        // until the worker reports that cleanup has finished.
        {UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOADED                         , &LCSCReader::handleUploadCDFilesState                         , UPLOAD_LCSC_FILES_STATE::UPLOAD_CDFILES                         },
        {UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED                    , &LCSCReader::handleUploadCDFilesState                         , UPLOAD_LCSC_FILES_STATE::UPLOAD_CDFILES                         },
        {UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_COMPLETED                , &LCSCReader::handleUploadCDFilesState                         , UPLOAD_LCSC_FILES_STATE::IDLE                                   },
        {UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_FAILED                   , &LCSCReader::handleUploadCDFilesState                         , UPLOAD_LCSC_FILES_STATE::IDLE                                   }
    }},
    {UPLOAD_LCSC_FILES_STATE::GENERATE_CDACKFILES,
    {
        // GET_STATUS_OK only starts the blocking CDACK generation/finalize
        // job. Keep the upload FSM busy until that job reports completion.
        {UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_OK               , &LCSCReader::handleGenerateCDAckFilesState                    , UPLOAD_LCSC_FILES_STATE::MOVE_CDACKFILES                        },
        {UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_FAILED           , &LCSCReader::handleGenerateCDAckFilesState                    , UPLOAD_LCSC_FILES_STATE::IDLE                                   }
    }},
    {UPLOAD_LCSC_FILES_STATE::MOVE_CDACKFILES,
    {
        {UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_COMPLETED                , &LCSCReader::handleMoveCDAckFilesState                        , UPLOAD_LCSC_FILES_STATE::IDLE                                   },
        {UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_FAILED                   , &LCSCReader::handleMoveCDAckFilesState                        , UPLOAD_LCSC_FILES_STATE::IDLE                                   }
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
        case UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_COMPLETED:
        {
            returnStr = "CDFILE_CLEANUP_COMPLETED";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_FAILED:
        {
            returnStr = "CDFILE_CLEANUP_FAILED";
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
        case UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_COMPLETED:
        {
            returnStr = "CDACK_FINALIZE_COMPLETED";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_FAILED:
        {
            returnStr = "CDACK_FINALIZE_FAILED";
            break;
        }
        case UPLOAD_LCSC_FILES_EVENT::EVENT_COUNT:
        default:
        {
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
        case UPLOAD_LCSC_FILES_STATE::MOVE_CDACKFILES:
        {
            returnStr = "MOVE_CDACKFILES";
            break;
        }
        case UPLOAD_LCSC_FILES_STATE::STATE_COUNT:
        default:
        {
            break;
        }
    }

    return returnStr;
}

void LCSCReader::processUploadLcscFilesEvent(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    if (!acceptingWork_.load())
    {
        return;
    }

    boost::asio::post(
        ioContext_,
        [this, event, str]()
        {
            if (stopping_.load())
            {
                return;
            }

            const int currentStateIndex = static_cast<int>(currentUploadLcscFilesState_);

            if (currentStateIndex < 0 ||
                currentStateIndex >= static_cast<int>(UPLOAD_LCSC_FILES_STATE::STATE_COUNT))
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [UPLOAD_FSM] Invalid state | Recovery=IDLE",
                    logFileName_,
                    "LCSC");

                currentUploadLcscFilesState_ = UPLOAD_LCSC_FILES_STATE::IDLE;
                return;
            }

            const auto& stateTransitions = UploadLcscStateTransitionTable[currentStateIndex].transitions;

            for (const auto& transition : stateTransitions)
            {
                if (transition.event != event)
                {
                    continue;
                }

                if (event != UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION)
                {
                    Logger::getInstance()->FnLog(
                        "LCSC: [UPLOAD_FSM] Transition | From=" +
                            uploadLcscFilesStateToString(
                                currentUploadLcscFilesState_) +
                            " | Event=" +
                            uploadLcscFilesEventToString(
                                event) +
                            " | To=" +
                            uploadLcscFilesStateToString(
                                transition.nextState),
                        logFileName_,
                        "LCSC");
                }

                if (transition.lcscEventHandler != nullptr)
                {
                    (this->*transition.lcscEventHandler)(event, str);
                }

                currentUploadLcscFilesState_ = transition.nextState;
                return;
            }

            if (event != UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION)
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [UPLOAD_FSM] Event ignored | State=" +
                        uploadLcscFilesStateToString(
                            currentUploadLcscFilesState_) +
                        " | Event=" +
                        uploadLcscFilesEventToString(
                            event),
                    logFileName_,
                    "LCSC");
            }
        });
}

void LCSCReader::startDownloadCdFilesJob()
{
    if (stopping_.load() || !filePool_)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] CD download not started | Reason=Worker unavailable",
            logFileName_,
            "LCSC");

        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::NO_CDFILES_DOWNLOADED);
        return;
    }

    Logger::getInstance()->FnLog("LCSC: [FILE] CD download started", logFileName_, "LCSC");

    boost::asio::post(
        *filePool_,
        [this]()
        {
            bool downloaded = false;

            try
            {
                downloaded = FnDownloadCDFiles();
            }
            catch (const std::exception& e)
            {
                Logger::getInstance()->FnLog(
                    std::string("LCSC: [FILE] CD download exception | Error=") +
                        e.what(),
                    logFileName_,
                    "LCSC");
            }
            catch (...)
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [FILE] CD download exception | Error=Unknown exception",
                    logFileName_,
                    "LCSC");
            }

            if (stopping_.load() || !acceptingWork_.load())
            {
                return;
            }

            processUploadLcscFilesEvent(
                downloaded
                    ? UPLOAD_LCSC_FILES_EVENT::CDFILES_DOWNLOADED
                    : UPLOAD_LCSC_FILES_EVENT::NO_CDFILES_DOWNLOADED);
        });
}

void LCSCReader::startScanDownloadedCdFilesJob()
{
    if (stopping_.load() || !filePool_)
    {
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS);
        return;
    }

    const std::string localFolder = LOCAL_LCSC_FOLDER_PATH;

    boost::asio::post(
        *filePool_,
        [this, localFolder]()
        {
            std::string cdFileName;

            try
            {
                const std::filesystem::path folder(localFolder);

                if (std::filesystem::exists(folder) &&
                    std::filesystem::is_directory(folder))
                {
                    for (const auto& entry : std::filesystem::directory_iterator(folder))
                    {
                        const std::string filename = entry.path().filename().string();

                        if (((filename.size() >= 4) &&
                             (filename.substr(filename.size() - 4) != ".lcs")) &&
                            ((filename.size() >= 6) &&
                             (filename.substr(filename.size() - 6) != ".cdack")) &&
                            ((filename.size() >= 4) &&
                             (filename.substr(filename.size() - 4) != ".cfg")))
                        {
                            cdFileName = entry.path().string();
                            break;
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                Logger::getInstance()->FnLog(
                    std::string("LCSC: [FILE] Local CD scan failed | Error=") +
                        e.what(),
                    logFileName_,
                    "LCSC");
            }

            if (stopping_.load() || !acceptingWork_.load())
            {
                return;
            }

            boost::asio::post(
                ioContext_,
                [this, cdFileName]()
                {
                    if (stopping_.load())
                    {
                        return;
                    }

                    if (!cdFileName.empty())
                    {
                        uploadLcscFileName_ = cdFileName;

                        Logger::getInstance()->FnLog(
                            "LCSC: [FILE] CD file selected | Path=" +
                                cdFileName,
                            logFileName_,
                            "LCSC");

                        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILES_UPLOADING, cdFileName);
                    }
                    else
                    {
                        Logger::getInstance()->FnLog(
                            "LCSC: [FILE] No pending CD file | Action=Read device status",
                            logFileName_,
                            "LCSC");

                        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS);
                    }
                });
        });
}

void LCSCReader::startUploadCdFileJob(std::string path)
{
    if (stopping_.load() || !filePool_)
    {
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
        return;
    }

    Logger::getInstance()->FnLog(
        "LCSC: [FILE] Upload preparation started | Path=" + path,
        logFileName_,
        "LCSC");

    boost::asio::post(
        *filePool_,
        [this, path = std::move(path)]() mutable
        {
            try
            {
                FnUploadCDFile2(std::move(path));
            }
            catch (const std::exception& e)
            {
                Logger::getInstance()->FnLog(
                    std::string("LCSC: [FILE] Upload preparation exception | Error=") +
                        e.what(),
                    logFileName_,
                    "LCSC");

                if (!stopping_.load() && acceptingWork_.load())
                {
                    processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
                }
            }
            catch (...)
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [FILE] Upload preparation exception | Error=Unknown exception",
                    logFileName_,
                    "LCSC");

                if (!stopping_.load() && acceptingWork_.load())
                {
                    processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
                }
            }
        });
}

void LCSCReader::startCleanupCdFileJob(std::string path)
{
    if (path.empty())
    {
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_COMPLETED);
        return;
    }

    if (stopping_.load() || !filePool_)
    {
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_FAILED);
        return;
    }

    boost::asio::post(
        *filePool_,
        [this, path = std::move(path)]()
        {
            std::error_code ec;
            const bool removed = std::filesystem::remove(path, ec);

            if (ec)
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [FILE] Delete failed | Path=" + path +
                        " | Error=" + ec.message(),
                    logFileName_,
                    "LCSC");
            }
            else
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [FILE] Delete completed | Path=" + path +
                        " | Removed=" + (removed ? "true" : "false"),
                    logFileName_,
                    "LCSC");
            }

            if (stopping_.load() || !acceptingWork_.load())
            {
                return;
            }

            processUploadLcscFilesEvent(
                ec
                    ? UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_FAILED
                    : UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_COMPLETED);
        });
}

void LCSCReader::startFinalizeCdAckFilesJob(
    const std::string& serialNum,
    const std::string& fwVer,
    const std::string& bl1Ver,
    const std::string& bl2Ver,
    const std::string& bl3Ver,
    const std::string& cil1Ver,
    const std::string& cil2Ver,
    const std::string& cil3Ver,
    const std::string& cfgVer)
{
    if (stopping_.load() || !filePool_)
    {
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_FAILED);
        return;
    }

    Logger::getInstance()->FnLog("LCSC: [FILE] CDACK finalize started", logFileName_, "LCSC");

    boost::asio::post(
        *filePool_,
        [this,
         serialNum,
         fwVer,
         bl1Ver,
         bl2Ver,
         bl3Ver,
         cil1Ver,
         cil2Ver,
         cil3Ver,
         cfgVer]()
        {
            bool success = false;

            try
            {
                const bool generated =
                    FnGenerateCDAckFile(
                        serialNum,
                        fwVer,
                        bl1Ver,
                        bl2Ver,
                        bl3Ver,
                        cil1Ver,
                        cil2Ver,
                        cil3Ver,
                        cfgVer);

                if (generated)
                {
                    const std::filesystem::path folder(LOCAL_LCSC_FOLDER_PATH);

                    if (std::filesystem::exists(folder) &&
                        std::filesystem::is_directory(folder))
                    {
                        // Preserve .lcs and .cdack files. Remove the already
                        // consumed CD input files before moving the ACK file.
                        for (const auto& entry : std::filesystem::directory_iterator(folder))
                        {
                            const std::string filename = entry.path().filename().string();

                            if (((filename.size() >= 4) &&
                                 (filename.substr(filename.size() - 4) != ".lcs")) &&
                                ((filename.size() >= 6) &&
                                 (filename.substr(filename.size() - 6) != ".cdack")))
                            {
                                std::error_code removeEc;
                                std::filesystem::remove(entry.path(), removeEc);

                                if (removeEc)
                                {
                                    Logger::getInstance()->FnLog(
                                        "LCSC: [FILE] Cleanup failed | Path=" +
                                            entry.path().string() +
                                            " | Error=" +
                                            removeEc.message(),
                                        logFileName_,
                                        "LCSC");
                                }
                            }
                        }
                    }

                    // FnMoveCDAckFile() already moves every .cdack file in
                    // the directory. Call it once, not once per iterator item.
                    success = FnMoveCDAckFile();
                }
            }
            catch (const std::exception& e)
            {
                Logger::getInstance()->FnLog(
                    std::string("LCSC: [FILE] CDACK finalize failed | Error=") +
                        e.what(),
                    logFileName_,
                    "LCSC");
            }

            if (stopping_.load() || !acceptingWork_.load())
            {
                return;
            }

            processUploadLcscFilesEvent(
                success
                    ? UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_COMPLETED
                    : UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_FAILED);
        });
}

void LCSCReader::handleUploadLcscIdleState(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    if (event == UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION)
    {
        if ((operation::getInstance()->tParas.giCommPortLCSC > 0) &&
            !operation::getInstance()->tProcess.gbLoopApresent.load() &&
            (Common::getInstance()->FnGetCurrentHour() < 20))
        {
            if (!HasCDFileToUpload_ &&
                (LastCDUploadDate_ != Common::getInstance()->FnGetCurrentDay()) &&
                (LastCDUploadTime_ != Common::getInstance()->FnGetCurrentHour()))
            {
                LastCDUploadTime_ = Common::getInstance()->FnGetCurrentHour();

                Logger::getInstance()->FnLog(
                    "LCSC: [UPLOAD] Download allowed | LastDate=" +
                        std::to_string(LastCDUploadDate_) +
                        " | LastHour=" +
                        std::to_string(LastCDUploadTime_),
                    logFileName_,
                    "LCSC");

                processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::ALLOW_DOWNLOAD);
            }
            else if (HasCDFileToUpload_)
            {
                Logger::getInstance()->FnLog(
                    "LCSC: [UPLOAD] Continue pending files",
                    logFileName_,
                    "LCSC");

                processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CONTINUE_UPLOAD);
            }
        }
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::ALLOW_DOWNLOAD)
    {
        startDownloadCdFilesJob();
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::CONTINUE_UPLOAD)
    {
        processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CDFILES_DOWNLOADED);
    }
}

void LCSCReader::handleDownloadCDFilesState(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    if (event == UPLOAD_LCSC_FILES_EVENT::CDFILES_DOWNLOADED)
    {
        HasCDFileToUpload_ = true;

        Logger::getInstance()->FnLog(
            "LCSC: [FILE] CD download completed | Action=Scan local files",
            logFileName_,
            "LCSC");

        startScanDownloadedCdFilesJob();
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::NO_CDFILES_DOWNLOADED)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] CD download completed | Files=0",
            logFileName_,
            "LCSC");
    }
}

void LCSCReader::handleUploadCDFilesState(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    if (event == UPLOAD_LCSC_FILES_EVENT::CDFILES_UPLOADING)
    {
        startUploadCdFileJob(str);
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [UPLOAD] Request device status | Reason=Prepare CDACK",
            logFileName_,
            "LCSC");

        FnSendGetStatusCmd();
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOADED ||
             event == UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED)
    {
        const bool uploaded =
            event == UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOADED;
        const std::string path = uploadLcscFileName_;

        Logger::getInstance()->FnLog(
            std::string("LCSC: [UPLOAD] File ") +
                (uploaded ? "completed" : "failed") +
                " | Path=" + path +
                " | Action=Cleanup local file",
            logFileName_,
            "LCSC");

        startCleanupCdFileJob(path);
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_COMPLETED ||
             event == UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_FAILED)
    {
        const bool cleanupOk =
            event == UPLOAD_LCSC_FILES_EVENT::CDFILE_CLEANUP_COMPLETED;

        Logger::getInstance()->FnLog(
            std::string("LCSC: [UPLOAD] Local cleanup ") +
                (cleanupOk ? "completed" : "failed") +
                " | Path=" + uploadLcscFileName_,
            logFileName_,
            "LCSC");

        uploadLcscFileName_.clear();
    }
}

void LCSCReader::handleGenerateCDAckFilesState(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string& str)
{
    if (event == UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_FAILED)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [UPLOAD] CDACK cancelled | Reason=Device status failed",
            logFileName_,
            "LCSC");
        return;
    }

    if (event != UPLOAD_LCSC_FILES_EVENT::GET_LCSC_DEVICE_STATUS_OK)
    {
        return;
    }

    std::string serialNum;
    std::string firmwareVersion;
    std::string bl1Version;
    std::string bl2Version;
    std::string bl3Version;
    std::string cil1Version;
    std::string cil2Version;
    std::string cil3Version;
    std::string cfgVersion;

    try
    {
        const std::vector<std::string> subVector =
            Common::getInstance()->FnParseString(str, ',');

        for (std::string pair : subVector)
        {
            const std::string param =
                Common::getInstance()->FnBiteString(pair, '=');
            const std::string value = pair;

            if (param == "serialNum")
            {
                serialNum = value;
            }
            else if (param == "firmwareVersion")
            {
                firmwareVersion = value;
            }
            else if (param == "bl1Version")
            {
                bl1Version = value;
            }
            else if (param == "bl2Version")
            {
                bl2Version = value;
            }
            else if (param == "bl3Version")
            {
                bl3Version = value;
            }
            else if (param == "cil1Version")
            {
                cil1Version = value;
            }
            else if (param == "cil2Version")
            {
                cil2Version = value;
            }
            else if (param == "cil3Version")
            {
                cil3Version = value;
            }
            else if (param == "cfgVersion")
            {
                cfgVersion = value;
            }
        }
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLog(
            std::string("LCSC: [UPLOAD] Status parse failed | Error=") +
                e.what(),
            logFileName_,
            "LCSC");

        processUploadLcscFilesEvent(
            UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_FAILED);
        return;
    }

    startFinalizeCdAckFilesJob(
        serialNum,
        firmwareVersion,
        bl1Version,
        bl2Version,
        bl3Version,
        cil1Version,
        cil2Version,
        cil3Version,
        cfgVersion);
}

void LCSCReader::handleMoveCDAckFilesState(LCSCReader::UPLOAD_LCSC_FILES_EVENT event, const std::string&)
{
    if (event == UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_COMPLETED)
    {
        HasCDFileToUpload_ = false;
        LastCDUploadDate_ = Common::getInstance()->FnGetCurrentDay();

        Logger::getInstance()->FnLog(
            "LCSC: [FILE] CDACK finalize completed | UploadDate=" +
                std::to_string(LastCDUploadDate_),
            logFileName_,
            "LCSC");
    }
    else if (event == UPLOAD_LCSC_FILES_EVENT::CDACK_FINALIZE_FAILED)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] CDACK finalize failed | Action=Retry on next cycle",
            logFileName_,
            "LCSC");
    }
}

void LCSCReader::FnUploadLCSCCDFiles()
{
    processUploadLcscFilesEvent(UPLOAD_LCSC_FILES_EVENT::CHECK_CONDITION);
}

void LCSCReader::FnUploadCDFile2(std::string path)
{
    const auto postFailure =
        [this](LCSC_CMD cmd)
        {
            if (stopping_.load() || !acceptingWork_.load())
            {
                return;
            }

            boost::asio::post(
                ioContext_,
                [this, cmd]()
                {
                    if (stopping_.load())
                    {
                        return;
                    }

                    handleCmdErrorOrTimeout(
                        cmd,
                        mCSCEvents::sSendcmdfail);
                    processUploadLcscFilesEvent(
                        UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
                });
        };

    if (path.empty())
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Upload prepare failed | Reason=Empty path",
            logFileName_,
            "LCSC");
        processUploadLcscFilesEvent(
            UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
        return;
    }

    LCSC_CMD cmd = LCSC_CMD::GET_STATUS_CMD;
    int ret = -1;

    if ((path.size() >= 4) &&
        (path.substr(path.size() - 4) == ".zip"))
    {
        cmd = LCSC_CMD::UPLOAD_CFG_FILE;
        ret = FnSendUploadCFGFile(path);
    }
    else if ((path.size() >= 4) &&
             (path.substr(path.size() - 4) == ".sys"))
    {
        cmd = LCSC_CMD::UPLOAD_CIL_FILE;
        ret = FnSendUploadCILFile(path);
    }
    else if ((path.size() >= 4) &&
             (path.substr(path.size() - 4) == ".blk"))
    {
        cmd = LCSC_CMD::UPLOAD_BL_FILE;
        ret = FnSendUploadBLFile(path);
    }
    else
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Upload prepare failed | Path=" + path +
                " | Reason=Unsupported extension",
            logFileName_,
            "LCSC");

        processUploadLcscFilesEvent(
            UPLOAD_LCSC_FILES_EVENT::CDFILE_UPLOAD_FAILED);
        return;
    }

    if (ret == -1)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Upload prepare failed | Cmd=" +
                getCommandString(cmd) +
                " | Path=" + path,
            logFileName_,
            "LCSC");

        postFailure(cmd);
        return;
    }

    Logger::getInstance()->FnLog(
        "LCSC: [FILE] Upload prepared | Cmd=" +
            getCommandString(cmd) +
            " | Path=" + path,
        logFileName_,
        "LCSC");
}

void LCSCReader::processTrans(const std::vector<uint8_t>& payload)
{
    Logger::getInstance()->FnLog(
        "LCSC: [TRANS] Build settlement record | PayloadBytes=" +
            std::to_string(payload.size()),
        logFileName_,
        "LCSC");

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
    if (operation::getInstance()->tParas.giEPS == 3)
    {
        MAC1 = std::string(4, '\0');
    }
    else
    {
        MAC1 = Common::getInstance()->FnConvertBinaryStringToString(transRecord1.substr(208, 32));
    }

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

    transRecord = transRecord + signedCert + counter + Common::getInstance()->FnConvertVectorUint8ToRawString(TRPVec)
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
    // This function is called from the LCSC I/O thread. Build/snapshot the
    // filename and header here, then move only blocking file access to the
    // worker pool. Do not read mutable module/FSM state from a pool thread.
    const std::string cpoId = operation::getInstance()->tParas.gsCPOID;
    const std::string cpId = operation::getInstance()->tParas.gsCPID;
    const int stationId = operation::getInstance()->gtStation.iSID;

    const std::string fileName =
        cpoId + "_" + cpId + "_" +
        Common::getInstance()->FnGetDateTimeFormat_yyyymmdd() + "_" +
        Common::getInstance()->FnPadLeft0(2, stationId) +
        Common::getInstance()->FnGetDateTimeFormat_hh() + ".lcs";

    const std::string settleFile =
        LOCAL_LCSC_SETTLEMENT_FOLDER_PATH + "/" + fileName;

    std::string header =
        "H" + cpId +
        Common::getInstance()->FnConvertHexStringToString(
            Common::getInstance()->FnGetDateTimeFormat_yyyymmddhhmmss()) +
        Common::getInstance()->FnPadLeftSpace(40, fileName);
    header.append(67, ' ');

    if (!filePool_ || stopping_.load())
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Settlement write skipped | Reason=Worker unavailable | Path=" +
                settleFile,
            logFileName_,
            "LCSC");
        return;
    }

    Logger::getInstance()->FnLog(
        "LCSC: [FILE] Settlement queued | Path=" + settleFile +
            " | Bytes=" + std::to_string(data.size()),
        logFileName_,
        "LCSC");

    boost::asio::post(
        *filePool_,
        [this,
         settleFile,
         header = std::move(header),
         detail = data]() mutable
        {
            writeLCSCTransBlocking(settleFile, std::move(header), std::move(detail));
        });
}

void LCSCReader::writeLCSCTransBlocking(std::string settleFile, std::string header, std::string detail)
{
    // filePool_ has two workers. Serialize the create/header/append sequence so
    // two settlement records cannot race on the same hourly .lcs file.
    std::lock_guard<std::mutex> lock(settlementFileMutex_);

    try
    {
        std::error_code fsEc;
        std::filesystem::create_directories(LOCAL_LCSC_SETTLEMENT_FOLDER_PATH, fsEc);

        if (fsEc)
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] Settlement directory failed | Path=" +
                    LOCAL_LCSC_SETTLEMENT_FOLDER_PATH +
                    " | Error=" + fsEc.message(),
                logFileName_,
                "LCSC");
            return;
        }

        const bool exists = std::filesystem::exists(settleFile, fsEc);
        if (fsEc)
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] Settlement stat failed | Path=" + settleFile +
                    " | Error=" + fsEc.message(),
                logFileName_,
                "LCSC");
            return;
        }

        std::ofstream file;
        if (exists)
        {
            file.open(settleFile, std::ios::binary | std::ios::app);
        }
        else
        {
            file.open(settleFile, std::ios::binary | std::ios::out);
        }

        if (!file.is_open())
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] Settlement open failed | Path=" + settleFile,
                logFileName_,
                "LCSC");
            return;
        }

        if (!exists)
        {
            file.write(header.data(), static_cast<std::streamsize>(header.size()));
        }

        file.write(detail.data(), static_cast<std::streamsize>(detail.size()));

        if (!file.good())
        {
            Logger::getInstance()->FnLog(
                "LCSC: [FILE] Settlement write failed | Path=" + settleFile,
                logFileName_,
                "LCSC");
            return;
        }

        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Settlement written | Path=" + settleFile +
                " | Bytes=" + std::to_string(detail.size()) +
                " | Mode=" + (exists ? "Append" : "Create"),
            logFileName_,
            "LCSC");
    }
    catch (const std::exception& e)
    {
        Logger::getInstance()->FnLog(
            std::string("LCSC: [FILE] Settlement exception | Path=") +
                settleFile + " | Error=" + e.what(),
            logFileName_,
            "LCSC");
    }
    catch (...)
    {
        Logger::getInstance()->FnLog(
            "LCSC: [FILE] Settlement exception | Path=" + settleFile +
                " | Error=Unknown exception",
            logFileName_,
            "LCSC");
    }
}
