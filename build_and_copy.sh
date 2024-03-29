#!/bin/bash

cd build/

source ~/workspace/x7v2_20222_D/environment-setup-cortexa72-cortexa53-xilinx-linux

make clean

# Run CMake with specified options
cmake .. -DBUILD_TEST=ON -DENABLE_CONSOLE_LOG=ON

# Compile the project using make with 4 parallel jobs
make -j4

# Copy the built binaries and configuration file to the remote server
scp linuxpbs root@192.168.2.151:/home/root/LinuxPBSSourceCode/LatestSourceCode/build/
scp LinuxPBS.ini root@192.168.2.151:/home/root/LinuxPBSSourceCode/LatestSourceCode/build/
