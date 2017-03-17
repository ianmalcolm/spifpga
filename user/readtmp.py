from spii2c import *
import time

print "Initialise PRER to proper value (0x00A5)...\n"
write(PRER_ADDR_L, 0xC8)
write(PRER_ADDR_H, 0x00)

print "Enable I2C...\n"
write(CTR_ADDR, CORE_EN)

print("Write to command register...\n")
write(CR_ADDR_W,CMD_START)

print "Write addr to transmit register TXR ...\n"
write(TXR_ADDR_W, (TEMP_ADDR << 1) + WRITE_BIT)

print("Write to command register...\n")
write(CR_ADDR_W,CMD_WRITE)

#print "Read status register...\n"
#read(SR_ADDR)

print "Write payload 0b10101010"
write(TXR_ADDR_W,0xe3)

print("Write to command register...\n")
write(CR_ADDR_W,CMD_WRITE)

#print "Read status register...\n"
#read(SR_ADDR)

print("Write to command register...\n")
write(CR_ADDR_W,CMD_STOP)

#############################################################

print("Write to command register...\n")
write(CR_ADDR_W,CMD_START)

print "Write addr to transmit register TXR ...\n"
write(TXR_ADDR_W, (TEMP_ADDR << 1) + READ_BIT)

print("Write to command register...\n")
write(CR_ADDR_W,CMD_WRITE)

print("Write to command register...\n")
write(CR_ADDR_W,CMD_READ)
read(RXR_ADDR)

print("Write to command register...\n")
write(CR_ADDR_W,CMD_READ)
read(RXR_ADDR)

print("Write to command register...\n")
write(CR_ADDR_W,CMD_READ)
read(RXR_ADDR)

print("Write to command register...\n")
write(CR_ADDR_W,CMD_STOP)



