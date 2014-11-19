#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "spifpga_user.h"

int main()
{
    int fd, ret;

    fd = config_spi();
    if (fd < 1)
    {
        printf("Failed to configure spi\n");
        return fd;
    }
    //write_word(fd, 0x00010014, 0xffffffff);
    int i=0, err_cnt=0;
    unsigned int *val, *wr_buf, *rd_buf;

    int ntrials=1024;

    //val = malloc(sizeof(unsigned int));
    //for (i=0; i<ntrials; i++)
    //{
    //    ret = bulk_write(fd, 0x00010004+4*i, 4, &i);
    //    if (ret != 143)
    //    {
    //        printf("write response was %u\n", ret);
    //    }
    //    ret = bulk_read(fd, 0x00010004+4*i, 4, val);
    //    if (ret != 143)
    //    {
    //        printf("read response was %u\n", ret);
    //    }
    //    
    //    if (*val != i)
    //    {
    //        printf("Read does not match write! (r:%u vs w:%u)\n", *val, i);
    //        err_cnt++;
    //    }
    //}

    //printf("Errors after %d single writes: %d\n", i, err_cnt);

    //free(val);

    printf("Trying bulk read of %d words\n", ntrials);
    wr_buf = calloc(ntrials, sizeof(unsigned int));
    rd_buf = calloc(ntrials, sizeof(unsigned int));
    for(i=0; i<ntrials; i++)
    {
        *(wr_buf+i) = i;
    }
    ret = bulk_write(fd, 0x00010004, 4*ntrials, wr_buf);
    printf("bulk write response was %u\n", ret);
    ret = bulk_read(fd, 0x00010004, 4*ntrials, rd_buf);
    for(i=0; i<ntrials; i++)
    {
        printf("Wrote %u, got back %u\n", *(wr_buf+i), *(rd_buf+i));
    }
    printf("bulk read response was %u\n", ret);
    free(wr_buf);
    free(rd_buf);

    close(fd);
    return 0;
}
