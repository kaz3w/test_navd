#!/usr/bin/env python3
# 
import spidev

read_addr = 0x00 
write_addr = 0x1E 
write_data = 0x08

#初期設定
spi = spidev.SpiDev()
spi.open(0,0)
spi.mode = 3  #このデバイスはSPI mode3で動作
spi.max_speed_hz = 1000000

#アドレス"read_addr "の値を読み出す
read_data = spi.xfer2([0x80 | read_addr,0x00]) 
print('read_data = 0x{:02x}'.format(read_data[1]))  # デバイスID 11100101 が読めるはず
# read_data = 0xe5

#アドレス"write_addr "に対してwrite_dataを書き込む
spi.xfer2([write_addr, write_data])

#正しく書き込めたことを確認
read_data = spi.xfer2([0x80 | write_addr,0x00])
print('write_data = 0x{:02x}'.format(read_data[1]))
# write_data = 0x08
