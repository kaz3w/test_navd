#!/bin/sh
dmesg | grep -i '\(can\|spi\)'
echo "------------------------------------"
ip -details -statistics link show can0

