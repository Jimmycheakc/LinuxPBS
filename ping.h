#pragma once

// C++ Libraries
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

template <typename TP>
TP str2num( std::string const& value );

template <typename TP>
std::string num2str( TP const& value );

int Execute_Command( const std::string&  command,
                     std::string&        output,
                     const std::string&  mode );

bool Ping( const std::string& address,
           const int&         max_attempts,
           std::string&       details );

bool PingWithTimeOut( const std::string& address,
           const float&   max_TimeOutInSeconds,
           std::string&       details );



