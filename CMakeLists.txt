cmake_minimum_required(VERSION 3.12)

project(LinuxPBS)

# Find ODBC
find_package(ODBC REQUIRED)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# Set the root directory for the target environment
set(CMAKE_FIND_ROOT_PATH ../../../../SDK_2022.1/sysroots/cortexa72-cortexa53-xilinx-linux)

# Specify the sysroot directory
set(CMAKE_SYSROOT ../../../../SDK_2022.1/sysroots/cortexa72-cortexa53-xilinx-linux)

add_definitions(-DFMT_HEADER_ONLY)

# Add include directories
include_directories(../../../../SDK_2022.1/sysroots/cortexa72-cortexa53-xilinx-linux/usr/include)
include_directories(../../../../SDK_2022.1/sysroots/cortexa72-cortexa53-xilinx-linux/usr/include/spdlog)
include_directories(../../../../SDK_2022.1/sysroots/cortexa72-cortexa53-xilinx-linux/usr/include/boost)

# Add link directories
link_directories(../../../../SDK_2022.1/sysroots/cortexa72-cortexa53-xilinx-linux/usr/local/lib)
link_directories(../../../../SDK_2022.1/sysroots/cortexa72-cortexa53-xilinx-linux/usr/lib)

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
    dio.cpp
    operation.cpp
    tcp.cpp
    lpr.cpp
    ksm_reader.cpp
    printer.cpp
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