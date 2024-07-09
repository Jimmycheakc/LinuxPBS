#pragma once

#include <memory>
#include <stdint.h>

class CRC32
{

public:
    static const uint32_t mTable[];
    void Init();
    uint32_t Value();
    void Update(uint8_t* Data, uint32_t Length);
    CRC32();
    ~CRC32();

private:
    uint32_t mValue;
};