#pragma once

#include <string>

// Passive synchronous utility.
//
// This module deliberately owns no io_context, thread, work guard, strand,
// timer, or coroutine. The caller chooses which thread executes the blocking
// ping operation.
//
// Existing public signatures are preserved for drop-in compatibility.
bool Ping(
    const std::string& address,
    const int& max_attempts,
    std::string& details);

bool PingWithTimeOut(
    const std::string& address,
    const float& max_TimeOutInSeconds,
    std::string& details);



