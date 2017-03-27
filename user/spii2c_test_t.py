from spii2c import *

# There is no wait in this script because there are print functions between spi
# operations. The script is usually executed on raspberry pi. Print function
# generates enough delay so there is no need to insert time.sleep()

# This test script is for Si7051 Temperature sensor
TEMP_ADDR = 0x40

print "Initialise clock frequency\n"
write(PRER_ADDR_L, 0xC8)
write(PRER_ADDR_H, 0x00)

print "Enable I2C\n"
write(CTR_ADDR, CORE_EN)

print "Send addr and type of operation (write) to transmit register TXR\n"
write(TXR_ADDR_W, (TEMP_ADDR << 1) + WRITE_BIT)

print("Send start and write command to command register...\n")
write(CR_ADDR_W,CMD_START|CMD_WRITE)

print "Send payload 0xe3 (which is the hold master mode) to transmit register\n"
write(TXR_ADDR_W, 0xe3)

print("Send write and stop command to command register\n")
write(CR_ADDR_W,CMD_WRITE|CMD_STOP)

#############################################################

print "Send addr and type of operation (read) to transmit register TXR\n"
write(TXR_ADDR_W, (TEMP_ADDR << 1) + READ_BIT)

print("Send start and write command to command register...\n")
write(CR_ADDR_W,CMD_START|CMD_WRITE)

print("Send read and ACK command (ACK=0) to command register...\n")
write(CR_ADDR_W,CMD_READ)

print("Get the response from receive register RXR\n")
read(RXR_ADDR)

print("Send the 2nd read and ACK command to command register...\n")
write(CR_ADDR_W,CMD_READ)

print("Get the 2nd response from receive register RXR\n")
read(RXR_ADDR)

print("Send the 3rd read, NACK and STOP command to command register...\n")
write(CR_ADDR_W,CMD_READ|CMD_NACK|CMD_STOP)

print("Get the 3rd response from receive register RXR\n")
read(RXR_ADDR)

