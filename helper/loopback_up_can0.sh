#!/bin/sh
sudo ip link set can0 type can bitrate 500000 restart-ms 100 loopback on
#sudo ip link set can0 type can bitrate 500000 dbitrate 4000000 dsample-point 0.8
sudo ifconfig can0 txqueuelen 65535
sudo ip link set can0 up
ifconfig can0

