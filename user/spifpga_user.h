#define DEVICE "/dev/spidev0.0"
#define MAX_SPEED 4000000
#define DELAY 1
#define BITS 8
#define MAX_BURST_SIZE 256
#define BYTES_PER_WORD 4

struct fpga_spi_cmd {
    unsigned char cmd;
    unsigned int addr;
    unsigned int din;
    unsigned int dout;
    unsigned char resp;
} __attribute__((packed));

int config_spi();
int write_word(int fd, unsigned int addr, unsigned int val);
int read_word(int fd, unsigned int addr, unsigned int *val);
int bulk_read(int fd, unsigned int start_addr, unsigned int n_bytes, unsigned int *buf);
int bulk_write(int fd, unsigned int start_addr, unsigned int n_bytes, unsigned int *buf);

