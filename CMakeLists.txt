cmake_minimum_required(VERSION 3.12)

project(LinuxPBS)

# Find ODBC
find_package(ODBC REQUIRED)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# Add include directories
include_directories(/usr/local/include)
include_directories(/usr/local/include/spdlog)
include_directories(/usr/local/include/boost)

# Add link directories
link_directories(/usr/local/lib)
link_directories(/usr/lib)

# Lisy your source files
set(SOURCE_FILES
    common.cpp
    system_info.cpp
    log.cpp
    ini_parser.cpp
    crc.cpp
    gpio.cpp
    upt.cpp
    lcd.cpp
    led.cpp
    antenna.cpp
    event_manager.cpp
    event_handler.cpp
    lcsc.cpp
    ping.cpp
    udp.cpp
    ce_time.cpp
    odbc.cpp
    db.cpp
    main.cpp
)

# Conditionally include test.cpp
option(BUILD_TEST "Build test.cpp" OFF)
if (BUILD_TEST)
    list(APPEND SOURCE_FILES test.cpp)

    # Add BUILD_TEST as a preprocessor definition
    add_definitions(-DBUILD_TEST)
endif()

# Conditionally include console output log
option(ENABLE_CONSOLE_LOG "Output console log" OFF)
if (ENABLE_CONSOLE_LOG)
    add_definitions(-DCONSOLE_LOG_ENABLE)
endif()

# Copy LinuxPBS.ini to build directory
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/LinuxPBS.ini ${CMAKE_CURRENT_BINARY_DIR}/LinuxPBS.ini COPYONLY)

# Create an executable
add_executable(linuxpbs ${SOURCE_FILES})

# Link against libraries
target_link_libraries(linuxpbs spdlog boost_system boost_filesystem ch347 ps_par OpenSSL::Crypto ${ODBC_LIBRARIES})