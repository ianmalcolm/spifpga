#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "spifpga_user.h"

void help();

int main(int argc, char **argv)
{

	bool writeFlag = false;
	bool readFlag = false;
	bool addrFlag=false;
	unsigned int addr = 0;
	unsigned int data = 0;
	int index;
	int c;

	opterr = 0;

	while ((c = getopt (argc, argv, "a:rw:c")) != -1)
		switch (c) {
			case 'a':
				addrFlag = true;
				addr = strtol(optarg,NULL,0);
				break;
			case 'r':
				readFlag = true;
				break;
			case 'w':
				writeFlag = true;
				data = strtol(optarg,NULL,0);
				break;
			case '?':
				help();
				return 1;
			default:
				abort ();
		}

	for (index = optind; index < argc; index++) {
		help();
		return 1;
	}

	if ((readFlag && writeFlag) ||
			(!readFlag && !writeFlag) ||
			(!addrFlag)) {
		help();
		return 1;
	}


	int fd, ret;

	fd = config_spi();
	if (fd < 1)
	{
		fprintf(stderr,"Failed to configure SPI\n");
		return fd;
	}

	if (readFlag) {
		ret = read_word(fd, addr, &data);
		fprintf(stdout,"0x%x\n", data);
		fprintf(stderr,"Read response was %u\n",ret);
	} else if (writeFlag) {
		ret = write_word(fd, addr, data);
		fprintf(stderr,"Write response was %u\n",ret);
	}

	close(fd);
	return 0;

}

void help(){
	printf ("SPI command line tool for HERA project.\n");
	printf ("Usage:\n");
	printf ("\tSPI read: spifpga_user -a addr -r\n");
	printf ("\tSPI write: spifpga_user -a addr -w data\n");
}
