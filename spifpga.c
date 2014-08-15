/*
 * Simple synchronous userspace interface to SPI devices
 *
 * Copyright (C) 2006 SWAPP
 *  Andrea Paterniani <a.paterniani@swapp-eng.it>
 * Copyright (C) 2007 David Brownell (simplification, cleanup)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/mm.h>

#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#include <asm/uaccess.h>


/*
 * This supports access to SPI devices using normal userspace I/O calls.
 * Note that while traditional UNIX/POSIX I/O semantics are half duplex,
 * and often mask message boundaries, full SPI support requires full duplex
 * transfers.  There are several kinds of internal message boundaries to
 * handle chipselect management and other protocol options.
 *
 * SPI has a character major number assigned.  We allocate minor numbers
 * dynamically using a bitmask.  You must use hotplug tools, such as udev
 * (or mdev with busybox) to create and destroy the /dev/spidevB.C device
 * nodes, since there is no fixed association of minor numbers with any
 * particular SPI bus or device.
 */
#define SPIFPGA_MAJOR            0  /* auto-assign */
#define N_SPI_MINORS            32  /* ... up to 128 */
#define MAX_MMAP_SIZE       0x4000000

static DECLARE_BITMAP(minors, N_SPI_MINORS);
static int Major;


/* Bit masks for spi_device.mode management.  Note that incorrect
 * settings for some settings can cause *lots* of trouble for other
 * devices on a shared bus:
 *
 *  - CS_HIGH ... this device will be active when it shouldn't be
 *  - 3WIRE ... when active, it won't behave as it should
 *  - NO_CS ... there will be no explicit message boundaries; this
 *  is completely incompatible with the shared bus model
 *  - READY ... transfers may proceed when they shouldn't.
 *
 * REVISIT should changing those flags be privileged?
 */
#define SPI_MODE_MASK       (SPI_CPHA | SPI_CPOL | SPI_CS_HIGH \
                | SPI_LSB_FIRST | SPI_3WIRE | SPI_LOOP \
                | SPI_NO_CS | SPI_READY)

struct spidev_data {
    dev_t               devt;
    spinlock_t          spi_lock;
    struct spi_device   *spi;
    struct list_head    device_entry;

    /* buffer is NULL unless this device is open (users > 0) */
    struct mutex        buf_lock;
    unsigned            users;
    u8                  *buffer;
};

struct fpga_data {
    unsigned char cmd;
    unsigned int addr;
    unsigned int dout;
    unsigned int din;
    unsigned char resp;
} __attribute__((packed));

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static unsigned int bufsiz = 2048;
module_param(bufsiz, uint, S_IRUGO);
MODULE_PARM_DESC(bufsiz, "data bytes in biggest supported SPI message");

static const struct file_operations main_fops;
static const struct file_operations spidev_fops;
static const struct file_operations spifpga_fops;

/*-------------------------------------------------------------------------*/

/* The main opening routine. This runs anytime /dev/spidevA.B or /dev/spifpgaA.B
 * are opened. It opens the file, sets file->f_op to the appropriate operations
 * set and then returns.
 */

static int main_open(struct inode *inode, struct file *filp)
{
    struct spidev_data  *spidev;
    int         status = -ENXIO;

    printk(KERN_INFO "Open request on file\n");

    mutex_lock(&device_list_lock);

    list_for_each_entry(spidev, &device_list, device_entry) {
        printk(KERN_INFO "spidev: %d,%d. Inode %d,%d\n",MAJOR(spidev->devt), MINOR(spidev->devt), MAJOR(inode->i_rdev), MINOR(inode->i_rdev));
        //if (spidev->devt/2 == inode->i_rdev/2) {
        if (spidev->devt/2 == inode->i_rdev/2) {
            status = 0;
            break;
        }
    }
    if (status == 0) {
        if (inode->i_rdev%2 == 0) {
            printk(KERN_INFO "Got an spidev file\n");
            filp->f_op = &spidev_fops;
        }
        else {
            printk(KERN_INFO "Got an spifpga file\n");
            filp->f_op = &spifpga_fops;
        }
        if (!spidev->buffer) {
            spidev->buffer = kmalloc(bufsiz, GFP_KERNEL);
            if (!spidev->buffer) {
                dev_dbg(&spidev->spi->dev, "open/ENOMEM\n");
                status = -ENOMEM;
            }
        }
        if (status == 0) {
            spidev->users++;
            filp->private_data = spidev;
        }
    } else
        pr_debug("spidev: nothing for minor %d\n", iminor(inode));

    mutex_unlock(&device_list_lock);
    printk(KERN_INFO "Leaving open with status %d\n", status);
    return status;
}

/*-------------------------------------------------------------------------*/

/*
 * We can't use the standard synchronous wrappers for file I/O; we
 * need to protect against async removal of the underlying spi_device.
 */

static void spidev_complete(void *arg)
{
    complete(arg);
}

static ssize_t
spidev_sync(struct spidev_data *spidev, struct spi_message *message)
{
    DECLARE_COMPLETION_ONSTACK(done);
    int status;

    message->complete = spidev_complete;
    message->context = &done;

    spin_lock_irq(&spidev->spi_lock);
    if (spidev->spi == NULL)
        status = -ESHUTDOWN;
    else
        status = spi_async(spidev->spi, message);
    spin_unlock_irq(&spidev->spi_lock);

    if (status == 0) {
        wait_for_completion(&done);
        status = message->status;
        if (status == 0)
            status = message->actual_length;
    }
    return status;
}

static inline ssize_t
spidev_sync_write(struct spidev_data *spidev, size_t len)
{
    struct spi_transfer t = {
            .tx_buf     = spidev->buffer,
            .len        = len,
        };
    struct spi_message  m;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    return spidev_sync(spidev, &m);
}

static inline ssize_t
spidev_sync_read(struct spidev_data *spidev, size_t len)
{
    struct spi_transfer t = {
            .rx_buf     = spidev->buffer,
            .len        = len,
        };
    struct spi_message  m;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    return spidev_sync(spidev, &m);
}

/*-------------------------------------------------------------------------*/

/* Read-only message with current device setup */
static ssize_t
spifpga_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct spidev_data  *spidev;
    struct spi_message msg;
    struct spi_transfer *t;
    struct spi_transfer *t_temp;
    ssize_t         status = 0;
    ssize_t         bytes_sent = 0;
    struct fpga_data *fcmd;
    struct fpga_data *frsp;
    struct fpga_data *fcmd_temp;
    struct fpga_data *frsp_temp;
    int i, j, p, c, d, n_transfers, n_pages, transfer_per_page;
    unsigned int TEST = 0xdeadbeef;
    
    spidev = filp->private_data;

    printk(KERN_INFO "Got read command for %d bytes\n", (int)count);

    mutex_lock(&spidev->buf_lock);

    n_transfers = (int)count / 4; 
    transfer_per_page = bufsiz / 14;
    n_pages = (n_transfers + transfer_per_page - 1) / transfer_per_page;
    printk(KERN_INFO "N transfers: %d, N_pages: %d, transfers per page %d\n", n_transfers, n_pages, transfer_per_page);
    

    /*
     * Build the message we are to send from each 32 bit word of user data
     */
    fcmd = kcalloc(transfer_per_page, sizeof(struct fpga_data), GFP_KERNEL);
    if (!fcmd) {
        mutex_unlock(&spidev->buf_lock);
        return -ENOMEM;
    }
    frsp = kcalloc(transfer_per_page, sizeof(struct fpga_data), GFP_KERNEL);
    if (!frsp) {
        kfree(fcmd);
        mutex_unlock(&spidev->buf_lock);
        return -ENOMEM;
    }
    t = kcalloc(transfer_per_page, sizeof(struct spi_transfer), GFP_KERNEL);
    if (!t) {
        kfree(fcmd);
        kfree(frsp);
        mutex_unlock(&spidev->buf_lock);
        return -ENOMEM;
    }

    c = 0;
    d = 0;
    for (p = 0; p < n_pages; p++) {
        spi_message_init(&msg);
        for (i = 0, t_temp = t, fcmd_temp = fcmd, frsp_temp = frsp; i<transfer_per_page; i++, t_temp++, fcmd_temp++, frsp_temp++) {
            fcmd_temp->cmd  = 0x78; // write, all byte enables = 1
            fcmd_temp->din  = TEST; // dummy bytes whilst slave sends data back
            fcmd_temp->dout  = 0; // dummy bytes whilst slave sends data back
            fcmd_temp->resp = 0; // dummy bytes whilst slave sends data back
            //printk(KERN_INFO "Getting address %d\n", (int)(*f_pos + i));
            fcmd_temp->addr = (unsigned int)*f_pos + 4*i;
            //printk(KERN_INFO "sizeof fpga_data is %d\n", (int)sizeof(struct fpga_data));
            //for (j=0; j<(sizeof(struct fpga_data)); j++) {
            //    printk(KERN_INFO "%d, %d\n", j, *((char *)fcmd_temp+j));
            //}
            t_temp->len = 14;
            t_temp->tx_buf = fcmd_temp;
            t_temp->rx_buf = frsp_temp;
            t_temp->cs_change = 1;
            spi_message_add_tail(t_temp, &msg);
            status = bytes_sent;
            if (++c == n_transfers)
                break;
        }
         
        //spidev_sync_write(spidev, sizeof(struct fpga_data));
        status = spidev_sync(spidev, &msg);
        if (status < 0) {
            kfree(fcmd);
            kfree(t);
            mutex_unlock(&spidev->buf_lock);
            return status;
        }
        for (i = 0, frsp_temp = frsp; i<transfer_per_page; i++, frsp_temp++) {
            if (copy_to_user(buf + 4*i, &frsp_temp->din, 4) != 0)
               return -EFAULT;
            if (++d == n_transfers)
                break;
        }
    }

    //spidev->buffer = NULL;
    kfree(t);
    kfree(fcmd);
    kfree(frsp);
    mutex_unlock(&spidev->buf_lock);
    return count;
}

/* Write-only message with current device setup */
static ssize_t
spifpga_write(struct file *filp, const char __user *buf,
        size_t count, loff_t *f_pos)
{
    struct spidev_data  *spidev;
    struct spi_message msg;
    struct spi_transfer *t;
    struct spi_transfer *t_temp;
    ssize_t         status = 0;
    ssize_t         bytes_sent = 0;
    struct fpga_data *fcmd;
    struct fpga_data *fcmd_temp;
    int i, j, p, c, n_transfers, n_pages, transfer_per_page;
    printk(KERN_INFO "made it!\n");

    spidev = filp->private_data;

    mutex_lock(&spidev->buf_lock);

    printk(KERN_INFO "Write command on spifpga at position %d\n", (int)*f_pos);
    printk(KERN_INFO "Write command for %d bytes\n", count);
    /* transfers not in multiples of 4 bytes have the ends chopped */
    n_transfers = (int)count / 4; 
    transfer_per_page = bufsiz / 14;
    n_pages = (n_transfers + transfer_per_page - 1) / transfer_per_page;
    printk(KERN_INFO "N transfers: %d, N_pages: %d, transfers per page %d\n", n_transfers, n_pages, transfer_per_page);

    fcmd = kcalloc(transfer_per_page, sizeof(*fcmd_temp), GFP_KERNEL);
    if (!fcmd) {
        //mutex_unlock(&spidev->buf_lock);
        return -ENOMEM;
    }

    t = kcalloc(transfer_per_page, sizeof(*t_temp), GFP_KERNEL);
    if (!t) {
        mutex_unlock(&spidev->buf_lock);
        return -ENOMEM;
    }

    /*
     * Build the message we are to send from each 32 bit word of user data
     */
    c = 0;
    for (p = 0; p < n_pages; p++) {
        spi_message_init(&msg);
        for (i = 0, t_temp = t, fcmd_temp = fcmd; i<transfer_per_page; i++, t_temp++, fcmd_temp++) {
            fcmd_temp->cmd  = 0xF8; // write, all byte enables = 1
            fcmd_temp->din  = 0; // dummy bytes whilst slave sends data back
            fcmd_temp->resp = 0; // dummy bytes whilst slave sends data back
            //printk(KERN_INFO "Getting address %d\n", (int)(*f_pos + i));
            fcmd_temp->addr = (unsigned int)*f_pos + 4*i;
            //printk(KERN_INFO "copying data %d\n", (int)(buf + i));
            if (copy_from_user(&fcmd_temp->dout, buf + 4*i, 4) != 0) {
                status = -EFAULT;
                break;
            }
            //printk(KERN_INFO "data is %d\n", fcmd_temp->dout);
            ////printk(KERN_INFO "sizeof fpga_data is %d\n", (int)sizeof(struct fpga_data));
            //for (j=0; j<(sizeof(struct fpga_data)); j++) {
            //    printk(KERN_INFO "%d, %d\n", j, *((char *)fcmd_temp+j));
            //}
            t_temp->len = 14;
            t_temp->tx_buf = fcmd_temp;
            t_temp->rx_buf = NULL;
            t_temp->cs_change = 1;
            spi_message_add_tail(t_temp, &msg);
            status = bytes_sent;
            if (++c == n_transfers)
                break;
        }
         
        //spidev_sync_write(spidev, sizeof(struct fpga_data));
        status = spidev_sync(spidev, &msg);
        if (status < 0) {
            kfree(fcmd);
            kfree(t);
            mutex_unlock(&spidev->buf_lock);
            return status;
        }
            
    }

    kfree(fcmd);
    kfree(t);
    mutex_unlock(&spidev->buf_lock);
    return count;
}

static loff_t spifpga_llseek(struct file *filp, loff_t offset, int origin)
{

        loff_t newpos;
        printk(KERN_INFO "lseeking\n");

        ///* only read-only opens are allowed to seek */
        //if ((filp->f_flags & O_ACCMODE) != O_RDONLY)
        //        return -EINVAL;

        switch (origin) {
        case SEEK_SET: /* seek relative to the beginning of the file */
                newpos = offset;
                break;
        case SEEK_CUR: /* seek relative to current position in the file */
                newpos = filp->f_pos + offset;
                break;
        case SEEK_END: /* seek relative to the end of the file */
                newpos = MAX_MMAP_SIZE - offset;
                break;
        default:
                return -EINVAL;
        }

        filp->f_pos = newpos;
        return newpos;
}

void spifpga_vma_open(struct vm_area_struct *vma)
{
        printk(KERN_NOTICE "spifpga VMA open, virt %lx, phys %lx",
                        vma->vm_start, vma->vm_pgoff << PAGE_SHIFT);
}

void spifpga_vma_close(struct vm_area_struct *vma)
{
        printk(KERN_NOTICE "spifpga VMA close");
}

int spifpga_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
        printk(KERN_NOTICE "got a VMA fault\n");
        return 0;
}




/*-------------------------------------------------------------------------*/

/* Read-only message with current device setup */
static ssize_t
spidev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct spidev_data  *spidev;
    ssize_t         status = 0;

    /* chipselect only toggles at start or end of operation */
    if (count > bufsiz)
        return -EMSGSIZE;

    spidev = filp->private_data;

    mutex_lock(&spidev->buf_lock);
    status = spidev_sync_read(spidev, count);
    if (status > 0) {
        unsigned long   missing;

        missing = copy_to_user(buf, spidev->buffer, status);
        if (missing == status)
            status = -EFAULT;
        else
            status = status - missing;
    }
    mutex_unlock(&spidev->buf_lock);

    return status;
}

/* Write-only message with current device setup */
static ssize_t
spidev_write(struct file *filp, const char __user *buf,
        size_t count, loff_t *f_pos)
{
    struct spidev_data  *spidev;
    ssize_t         status = 0;
    unsigned long       missing;

    printk(KERN_INFO "Write command on spidev\n");
    /* chipselect only toggles at start or end of operation */
    if (count > bufsiz)
        return -EMSGSIZE;

    spidev = filp->private_data;

    mutex_lock(&spidev->buf_lock);
    missing = copy_from_user(spidev->buffer, buf, count);
    if (missing == 0) {
        status = spidev_sync_write(spidev, count);
    } else
        status = -EFAULT;
    mutex_unlock(&spidev->buf_lock);

    return status;
}

static int spidev_message(struct spidev_data *spidev,
        struct spi_ioc_transfer *u_xfers, unsigned n_xfers)
{
    struct spi_message  msg;
    struct spi_transfer *k_xfers;
    struct spi_transfer *k_tmp;
    struct spi_ioc_transfer *u_tmp;
    unsigned        n, total;
    u8          *buf;
    int         status = -EFAULT;

    spi_message_init(&msg);
    k_xfers = kcalloc(n_xfers, sizeof(*k_tmp), GFP_KERNEL);
    if (k_xfers == NULL)
        return -ENOMEM;

    /* Construct spi_message, copying any tx data to bounce buffer.
     * We walk the array of user-provided transfers, using each one
     * to initialize a kernel version of the same transfer.
     */
    buf = spidev->buffer;
    total = 0;
    for (n = n_xfers, k_tmp = k_xfers, u_tmp = u_xfers;
            n;
            n--, k_tmp++, u_tmp++) {
        k_tmp->len = u_tmp->len;

        total += k_tmp->len;
        if (total > bufsiz) {
            status = -EMSGSIZE;
            goto done;
        }

        if (u_tmp->rx_buf) {
            k_tmp->rx_buf = buf;
            if (!access_ok(VERIFY_WRITE, (u8 __user *)
                        (uintptr_t) u_tmp->rx_buf,
                        u_tmp->len))
                goto done;
        }
        if (u_tmp->tx_buf) {
            k_tmp->tx_buf = buf;
            if (copy_from_user(buf, (const u8 __user *)
                        (uintptr_t) u_tmp->tx_buf,
                    u_tmp->len))
                goto done;
        }
        buf += k_tmp->len;

        k_tmp->cs_change = !!u_tmp->cs_change;
        k_tmp->bits_per_word = u_tmp->bits_per_word;
        k_tmp->delay_usecs = u_tmp->delay_usecs;
        k_tmp->speed_hz = u_tmp->speed_hz;
#ifdef VERBOSE
        dev_dbg(&spidev->spi->dev,
            "  xfer len %zd %s%s%s%dbits %u usec %uHz\n",
            u_tmp->len,
            u_tmp->rx_buf ? "rx " : "",
            u_tmp->tx_buf ? "tx " : "",
            u_tmp->cs_change ? "cs " : "",
            u_tmp->bits_per_word ? : spidev->spi->bits_per_word,
            u_tmp->delay_usecs,
            u_tmp->speed_hz ? : spidev->spi->max_speed_hz);
#endif
        spi_message_add_tail(k_tmp, &msg);
    }

    status = spidev_sync(spidev, &msg);
    if (status < 0)
        goto done;

    /* copy any rx data out of bounce buffer */
    buf = spidev->buffer;
    for (n = n_xfers, u_tmp = u_xfers; n; n--, u_tmp++) {
        if (u_tmp->rx_buf) {
            if (__copy_to_user((u8 __user *)
                    (uintptr_t) u_tmp->rx_buf, buf,
                    u_tmp->len)) {
                status = -EFAULT;
                goto done;
            }
        }
        buf += u_tmp->len;
    }
    status = total;

done:
    kfree(k_xfers);
    return status;
}

static long
spidev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int         err = 0;
    int         retval = 0;
    struct spidev_data  *spidev;
    struct spi_device   *spi;
    u32         tmp;
    unsigned        n_ioc;
    struct spi_ioc_transfer *ioc;

    /* Check type and command number */
    if (_IOC_TYPE(cmd) != SPI_IOC_MAGIC)
        return -ENOTTY;

    /* Check access direction once here; don't repeat below.
     * IOC_DIR is from the user perspective, while access_ok is
     * from the kernel perspective; so they look reversed.
     */
    if (_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok(VERIFY_WRITE,
                (void __user *)arg, _IOC_SIZE(cmd));
    if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
        err = !access_ok(VERIFY_READ,
                (void __user *)arg, _IOC_SIZE(cmd));
    if (err)
        return -EFAULT;

    /* guard against device removal before, or while,
     * we issue this ioctl.
     */
    spidev = filp->private_data;
    spin_lock_irq(&spidev->spi_lock);
    spi = spi_dev_get(spidev->spi);
    spin_unlock_irq(&spidev->spi_lock);

    if (spi == NULL)
        return -ESHUTDOWN;

    /* use the buffer lock here for triple duty:
     *  - prevent I/O (from us) so calling spi_setup() is safe;
     *  - prevent concurrent SPI_IOC_WR_* from morphing
     *    data fields while SPI_IOC_RD_* reads them;
     *  - SPI_IOC_MESSAGE needs the buffer locked "normally".
     */
    mutex_lock(&spidev->buf_lock);

    switch (cmd) {
    /* read requests */
    case SPI_IOC_RD_MODE:
        retval = __put_user(spi->mode & SPI_MODE_MASK,
                    (__u8 __user *)arg);
        break;
    case SPI_IOC_RD_LSB_FIRST:
        retval = __put_user((spi->mode & SPI_LSB_FIRST) ?  1 : 0,
                    (__u8 __user *)arg);
        break;
    case SPI_IOC_RD_BITS_PER_WORD:
        retval = __put_user(spi->bits_per_word, (__u8 __user *)arg);
        break;
    case SPI_IOC_RD_MAX_SPEED_HZ:
        retval = __put_user(spi->max_speed_hz, (__u32 __user *)arg);
        break;

    /* write requests */
    case SPI_IOC_WR_MODE:
        retval = __get_user(tmp, (u8 __user *)arg);
        if (retval == 0) {
            u8  save = spi->mode;

            if (tmp & ~SPI_MODE_MASK) {
                retval = -EINVAL;
                break;
            }

            tmp |= spi->mode & ~SPI_MODE_MASK;
            spi->mode = (u8)tmp;
            retval = spi_setup(spi);
            if (retval < 0)
                spi->mode = save;
            else
                dev_dbg(&spi->dev, "spi mode %02x\n", tmp);
        }
        break;
    case SPI_IOC_WR_LSB_FIRST:
        retval = __get_user(tmp, (__u8 __user *)arg);
        if (retval == 0) {
            u8  save = spi->mode;

            if (tmp)
                spi->mode |= SPI_LSB_FIRST;
            else
                spi->mode &= ~SPI_LSB_FIRST;
            retval = spi_setup(spi);
            if (retval < 0)
                spi->mode = save;
            else
                dev_dbg(&spi->dev, "%csb first\n",
                        tmp ? 'l' : 'm');
        }
        break;
    case SPI_IOC_WR_BITS_PER_WORD:
        retval = __get_user(tmp, (__u8 __user *)arg);
        if (retval == 0) {
            u8  save = spi->bits_per_word;

            spi->bits_per_word = tmp;
            retval = spi_setup(spi);
            if (retval < 0)
                spi->bits_per_word = save;
            else
                dev_dbg(&spi->dev, "%d bits per word\n", tmp);
        }
        break;
    case SPI_IOC_WR_MAX_SPEED_HZ:
        retval = __get_user(tmp, (__u32 __user *)arg);
        if (retval == 0) {
            u32 save = spi->max_speed_hz;

            spi->max_speed_hz = tmp;
            retval = spi_setup(spi);
            if (retval < 0)
                spi->max_speed_hz = save;
            else
                dev_dbg(&spi->dev, "%d Hz (max)\n", tmp);
        }
        break;

    default:
        /* segmented and/or full-duplex I/O request */
        if (_IOC_NR(cmd) != _IOC_NR(SPI_IOC_MESSAGE(0))
                || _IOC_DIR(cmd) != _IOC_WRITE) {
            retval = -ENOTTY;
            break;
        }

        tmp = _IOC_SIZE(cmd);
        if ((tmp % sizeof(struct spi_ioc_transfer)) != 0) {
            retval = -EINVAL;
            break;
        }
        n_ioc = tmp / sizeof(struct spi_ioc_transfer);
        if (n_ioc == 0)
            break;

        /* copy into scratch area */
        ioc = kmalloc(tmp, GFP_KERNEL);
        if (!ioc) {
            retval = -ENOMEM;
            break;
        }
        if (__copy_from_user(ioc, (void __user *)arg, tmp)) {
            kfree(ioc);
            retval = -EFAULT;
            break;
        }

        /* translate to spi_message, execute */
        retval = spidev_message(spidev, ioc, n_ioc);
        kfree(ioc);
        break;
    }

    mutex_unlock(&spidev->buf_lock);
    spi_dev_put(spi);
    return retval;
}

#ifdef CONFIG_COMPAT
static long
spidev_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return spidev_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define spidev_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int spidev_release(struct inode *inode, struct file *filp)
{
    struct spidev_data  *spidev;
    int         status = 0;

    mutex_lock(&device_list_lock);
    spidev = filp->private_data;
    filp->private_data = NULL;

    /* last close? */
    spidev->users--;
    if (!spidev->users) {
        int     dofree;

        kfree(spidev->buffer);
        spidev->buffer = NULL;

        /* ... after we unbound from the underlying device? */
        spin_lock_irq(&spidev->spi_lock);
        dofree = (spidev->spi == NULL);
        spin_unlock_irq(&spidev->spi_lock);

        if (dofree)
            kfree(spidev);
    }
    mutex_unlock(&device_list_lock);

    return status;
}


/*-------------------------------------------------------------------------*/

static const struct file_operations main_fops = {
    .owner =    THIS_MODULE,
    .open =     main_open,
};

static const struct file_operations spidev_fops = {
    .owner =    THIS_MODULE,
    /* REVISIT switch to aio primitives, so that userspace
     * gets more complete API coverage.  It'll simplify things
     * too, except for the locking.
     */
    .write =    spidev_write,
    .read =     spidev_read,
    .unlocked_ioctl = spidev_ioctl,
    .compat_ioctl = spidev_compat_ioctl,
    .release =  spidev_release,
    .llseek =   no_llseek,
};

static const struct file_operations spifpga_fops = {
    .owner =    THIS_MODULE,
    .write =    spifpga_write,
    .read =     spifpga_read,
    .unlocked_ioctl = spidev_ioctl,
    .compat_ioctl = spidev_compat_ioctl,
    .release =  spidev_release,
    .llseek =   spifpga_llseek,
};

/*-------------------------------------------------------------------------*/

/* The main reason to have this class is to make mdev/udev create the
 * /dev/spidevB.C character device nodes exposing our userspace API.
 * It also simplifies memory management.
 */

static struct class *spidev_class;

/*-------------------------------------------------------------------------*/

static int spidev_probe(struct spi_device *spi)
{
    struct spidev_data  *spidev;
    int         status = 0;
    int         status1 = 0;
    unsigned long       minor;
    printk(KERN_INFO "Probing spidev\n");

    /* Allocate driver data */
    spidev = kzalloc(sizeof(*spidev), GFP_KERNEL);
    if (!spidev)
        return -ENOMEM;

    /* Initialize the driver data */
    spidev->spi = spi;
    spin_lock_init(&spidev->spi_lock);
    mutex_init(&spidev->buf_lock);

    INIT_LIST_HEAD(&spidev->device_entry);

    /* If we can allocate a minor number, hook up this device.
     * Reusing minors is fine so long as udev or mdev is working.
     */
    mutex_lock(&device_list_lock);
    /* Let all the spidev-like interfaces have even minor numbers.
     * and give the fpga interfaces this number + 1
     */
    minor = find_first_zero_bit(minors, N_SPI_MINORS)*2;
    printk(KERN_INFO "Minor number is %ld\n", minor);
    if (minor < N_SPI_MINORS*2) {
        struct device *dev;

        spidev->devt = MKDEV(Major, minor);
        printk(KERN_INFO "Creating devices 1\n");
        /* Register two devices. One which acts exactly like
         * the original spidev device, and one which we will
         * use to send read/write commands to the FPGA
         */
        dev = device_create(spidev_class, &spi->dev, spidev->devt,
                    spidev, "spidev%d.%d",
                    spi->master->bus_num, spi->chip_select);
        status = PTR_ERR_OR_ZERO(dev);
        printk(KERN_INFO "Creating devices 2\n");
        dev = device_create(spidev_class, &spi->dev, MKDEV(Major,minor + 1),
                    spidev, "spifpga%d.%d",
                    spi->master->bus_num, spi->chip_select);
        status1 = PTR_ERR_OR_ZERO(dev);
    } else {
        dev_dbg(&spi->dev, "no minor number available!\n");
        status = -ENODEV;
    }
    if (status == 0) {
        set_bit(minor, minors);
        list_add(&spidev->device_entry, &device_list);
    }
    mutex_unlock(&device_list_lock);

    if (status == 0)
        spi_set_drvdata(spi, spidev);
    else
        kfree(spidev);

    /* This will mangle the error codes, but for now
     * do it anyway.
     */
    return status + status1;
}

static int spidev_remove(struct spi_device *spi)
{
    struct spidev_data  *spidev = spi_get_drvdata(spi);

    /* make sure ops on existing fds can abort cleanly */
    spin_lock_irq(&spidev->spi_lock);
    spidev->spi = NULL;
    spi_set_drvdata(spi, NULL);
    spin_unlock_irq(&spidev->spi_lock);

    /* prevent new opens */
    mutex_lock(&device_list_lock);
    list_del(&spidev->device_entry);
    device_destroy(spidev_class, MKDEV(Major,MINOR(spidev->devt)));
    device_destroy(spidev_class, MKDEV(Major,MINOR(spidev->devt) + 1));
    clear_bit(MINOR(spidev->devt), minors);
    if (spidev->users == 0)
        kfree(spidev);
    mutex_unlock(&device_list_lock);

    return 0;
}

static const struct of_device_id spidev_dt_ids[] = {
    { .compatible = "rohm,dh2228fv" },
    {},
};

MODULE_DEVICE_TABLE(of, spidev_dt_ids);

static struct spi_driver spidev_spi_driver = {
    .driver = {
        .name =     "spifpga",
        .owner =    THIS_MODULE,
        .of_match_table = of_match_ptr(spidev_dt_ids),
    },
    .probe =    spidev_probe,
    .remove =   spidev_remove,

    /* NOTE:  suspend/resume methods are not necessary here.
     * We don't do anything except pass the requests to/from
     * the underlying controller.  The refrigerator handles
     * most issues; the controller driver handles the rest.
     */
};

/*-------------------------------------------------------------------------*/

static int __init spidev_init(void)
{
    int status;

    /* Claim our 256 reserved device numbers.  Then register a class
     * that will key udev/mdev to add/remove /dev nodes.  Last, register
     * the driver which manages those device numbers.
     */
    BUILD_BUG_ON(2*N_SPI_MINORS > 256);
    printk(KERN_INFO "registering chrdev\n");
    Major = register_chrdev(SPIFPGA_MAJOR, "spifpga", &main_fops);
    printk(KERN_INFO "Major number is %d\n", Major);
    if (Major < 0)
        return Major;

    printk(KERN_INFO "Creating class\n");
    spidev_class = class_create(THIS_MODULE, "spifpga");
    if (IS_ERR(spidev_class)) {
        unregister_chrdev(Major, spidev_spi_driver.driver.name);
        return PTR_ERR(spidev_class);
    }

    printk(KERN_INFO "registering driver\n");
    status = spi_register_driver(&spidev_spi_driver);
    printk(KERN_INFO "STATUS: %d\n", status);
    if (status < 0) {
        class_destroy(spidev_class);
        unregister_chrdev(Major, spidev_spi_driver.driver.name);
    }
 

    return status;
}
module_init(spidev_init);

static void __exit spidev_exit(void)
{
    spi_unregister_driver(&spidev_spi_driver);
    class_unregister(spidev_class);
    class_destroy(spidev_class);
    //unregister_chrdev(Major, spidev_spi_driver.driver.name);
    printk(KERN_INFO "unregistering %s\n", spidev_spi_driver.driver.name);
    unregister_chrdev(Major, "spifpga");
}
module_exit(spidev_exit);

MODULE_AUTHOR("Andrea Paterniani, <a.paterniani@swapp-eng.it>");
MODULE_DESCRIPTION("User mode SPI device interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spifpga");
