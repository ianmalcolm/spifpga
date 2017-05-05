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
	unsigned int data[100],*pdata;
	int length = 4;
	int index;
	int c;
	const char delim = ',';
	char *token;

	opterr = 0;
	pdata = data;

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
				token = strtok(optarg, delim);
				while(token!=NULL){
					*pdata++ = strtol(token,NULL,0);
					token = strtok(NULL,delim);
				}
				length = pdata - data;
				break;
			case 'l':
				if (writeFlag=false) {
					length = strtol(optarg,NULL,0);
				}
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
		ret = bulk_read(fd, addr, length, data);
		for (index=0; index<length; index++){
			fprintf(stdout,"0x%x,", data[index]);
		}
		fprintf(stderr,"\nRead response was %u\n",ret);
	} else if (writeFlag) {
		ret = bulk_write(fd, addr, length, data);
		fprintf(stderr,"Write response was %u\n",ret);
	}

	close(fd);
	return 0;

}

void help(){
	printf ("SPI command line tool for Jasper workflow.\n");
	printf ("Parameters:\n");
	printf ("\t-a: address\n");
	printf ("\t-r: read\n");
	printf ("\t-l: length in byte, default is 4 bytes\n");
	printf ("\t-w: write, followed by some data\n");
	printf ("\t    Data are delimited by ','\n");
	printf ("\t    the cap of data length is 100\n");
	printf ("\t    -l is ignored when -w and data are presented\n");
	printf ("Usage:\n");
	printf ("\tSPI read: spifpga_user -a addr -r\n");
	printf ("\tSPI write: spifpga_user -a addr -w data\n");
}
