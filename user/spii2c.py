import os
import time

I2C_BASE_ADDR = 0x00010000

# Please visit the urlfor more details of the low level i2c operating through
# SPI
# https://github.com/jack-h/mlib_devel/blob/jasper_devel/jasper_library/hdl_sources/i2c/i2c_master_top.v

PRER_ADDR_L = 0b000 << 2 # clock prescale register
PRER_ADDR_H = 0b001 << 2
CTR_ADDR = 0b010 << 2 # control register
RXR_ADDR = 0b011 << 2 # receive register
SR_ADDR = 0b100 << 2 # status register
TXR_ADDR_R = 0b101 << 2 # transmit register
# Strange that to read/write TXR, we have to use different addr
TXR_ADDR_W = 0b011 << 2
CR_ADDR_R = 0b110 << 2 # command register
# Strange that to read/write CR, we have to use different addr
CR_ADDR_W = 0b100 << 2

# I2C command
CMD_START = 1 << 7
CMD_STOP = 1 << 6
CMD_READ = 1 << 5
CMD_WRITE = 1 << 4
CMD_ACK = 1 << 3
CMD_IACK = 1 << 0

CORE_EN = 1 << 7 # i2c core enable
INT_EN = 1 << 6 # interrupt enable

WRITE_BIT = 0
READ_BIT = 1

LED0_ADDR = 0x21
LED1_ADDR = 0x20
TEMP_ADDR = 0x40
ID0_ADDR = 0x49
ID1_ADDR = 0x50

def read(addr):
    cmd = "./spifpga_user -a 0x%x -r" % (I2C_BASE_ADDR + addr)
    print cmd
    os.system(cmd)
    print "\n"

def write(addr,data):
    cmd = "./spifpga_user -a 0x%x -w 0x%x" % (I2C_BASE_ADDR + addr, data)
    print cmd
    os.system(cmd)
    print "\n"

