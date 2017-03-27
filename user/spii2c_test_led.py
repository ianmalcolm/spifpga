from spii2c import *

# This test script is for PCF8574TS, the Remote 8-bit I/O expander for I2C-bus

LED0_ADDR = 0x21
LED1_ADDR = 0x20

print "Initialise PRER for setting I2C clock frequency\n"
write(PRER_ADDR_L, 0xC8)
write(PRER_ADDR_H, 0x00)

print "Enable I2C\n"
write(CTR_ADDR, CORE_EN)

print "Send addr and the type of operation (write) to transmit register TXR\n"
write(TXR_ADDR_W, (LED0_ADDR << 1) + WRITE_BIT)

print("Send start and write command to command register simultaneously\n")
write(CR_ADDR_W,CMD_START|CMD_WRITE)

print "Send payload to transmit register\n"
write(TXR_ADDR_W,0b10101010)

print("Send write and stop command to command register\n")
write(CR_ADDR_W,CMD_WRITE|CMD_STOP)

