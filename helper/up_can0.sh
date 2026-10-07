#!/bin/sh
#sudo ip link set can0 type can bitrate 500000 restart-ms 100
sudo ip link set can0 type can bitrate 500000 restart-ms 100
#sudo ifconfig can0 txqueuelen 65536
sudo ip link set can0 up
ifconfig can0

