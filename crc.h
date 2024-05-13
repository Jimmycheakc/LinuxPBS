#pragma once

#include <memory>
#include <stdint.h>
#include <mutex>

class CRC32
{

public:
    static const uint32_t mTable[];
    static CRC32* getInstance();
    uint32_t Value();
    void Update(uint8_t* Data, uint32_t Length);

    /**
     * Singleton CRC32 should not be cloneable.
     */
    CRC32(CRC32& crc32) = delete;

    /**
     * Singleton CRC32 should not be assignable.
     */
    void operator=(const CRC32 &) = delete;

private:
    static CRC32* crc32_;
    static std::mutex mutex_;
    uint32_t mValue;
    CRC32();
};