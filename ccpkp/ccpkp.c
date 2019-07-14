/*
 * Character device for IPC between user-space and kernel-space CCP proccesses
 *
 * Frank Cangialosi <frankc@csail.mit.edu>
 * Created: April, 2018
 * Version 2
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

#include <asm/uaccess.h>

#include "ccpkp.h"

#define DEV_NAME "ccpkp"

struct ccpkp_dev *ccpkp_dev;
int ccpkp_major;

// NOTE: hack for now since there's only one ccp.
//       if we want to support multiple ccps, datapath
//       will need a way to differentiate between them
int curr_ccp_id; 

ccp_recv_handler libccp_read_msg;
#define RECVBUF_LEN 4096
char recvbuf[RECVBUF_LEN];

static struct file_operations ccpkp_fops = 
{
    .owner    = THIS_MODULE,
    .open     = ccpkp_user_open,
    .read     = ccpkp_user_read,
    .write    = ccpkp_user_write,
    .release  = ccpkp_user_release
};

int ccpkp_init(ccp_recv_handler handler) {
    int result, err;
    int devno;
    dev_t dev = 0;

    libccp_read_msg = handler;

    result = alloc_chrdev_region(&dev, 0, 1, DEV_NAME);
    ccpkp_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "ccp-kpipe: failed to register\n");
        return result;
    }

    ccpkp_dev = kmalloc(1 * sizeof(struct ccpkp_dev), GFP_KERNEL);
    if (!ccpkp_dev) {
        result = -ENOMEM;
        goto fail;
    }
    memset(ccpkp_dev, 0, 1 * sizeof(struct ccpkp_dev));
    
    mutex_init(&(ccpkp_dev->mux));
    devno = MKDEV(ccpkp_major, 0);
    cdev_init(&ccpkp_dev->cdev, &ccpkp_fops);
    ccpkp_dev->cdev.owner = THIS_MODULE;
    ccpkp_dev->cdev.ops = &ccpkp_fops;
    err = cdev_add(&ccpkp_dev->cdev, devno, 1);
    if (err) {
        printk(KERN_NOTICE "ccp-kpipe: error %d adding cdev\n", err);
    }
    
    printk(KERN_INFO "ccp-kpipe: device (%d) created successfully\n", ccpkp_major);


    return 0;

fail:
    ccpkp_cleanup();
    return result;
}

void ccpkp_cleanup(void) {
    dev_t devno = MKDEV(ccpkp_major, 0);

    if (ccpkp_dev) {
        // TODO free all queue buffers
        cdev_del(&ccpkp_dev->cdev);
        kfree(ccpkp_dev);
    }
    unregister_chrdev_region(devno, 1);
    ccpkp_dev = NULL;

    printk(KERN_INFO "ccp-kpipe: goodbye\n");
}

int ccpkp_user_open(struct inode *inp, struct file *fp) {
    // Create new pipe for this CCP
    struct kpipe *pipe = kmalloc(sizeof(struct kpipe), GFP_KERNEL);
    int i, ccp_id; 
#ifndef ONE_PIPE
    bool user_read_nonblock = fp->f_flags & O_NONBLOCK;
#endif

    memset(pipe, 0, sizeof(struct kpipe));
    if (!pipe) {
        return -ENOMEM;
    }

    PDEBUG("init lfq");
    if (init_lfq(&pipe->ccp_write_queue, false) < 0) {
        return -ENOMEM;
    }
#ifndef ONE_PIPE
    PDEBUG("init lfq");
    if (init_lfq(&pipe->dp_write_queue, !user_read_nonblock) < 0) {
        return -ENOMEM;
    }
#endif
    
    // Store pointer to pipe in struct file
    fp->private_data = pipe;

    if (mutex_lock_interruptible(&ccpkp_dev->mux)) {
        // We were interrupted (e.g. by a signal),
        // Let the kernel figure out what to do, maybe restart syscall
        return -ERESTARTSYS;
    }
    // TODO this gets decremented later, need to get last allocated instead
    PDEBUG("got lock, getting id");
    ccp_id = ccpkp_dev->num_ccps;
    if (ccp_id >= MAX_CCPS) {
        ccp_id = -1;
        for (i = 0; i < MAX_CCPS; i++) {
            if (ccpkp_dev->pipes[i] == NULL) {
                ccp_id = i;
                break;
            }
        }
        if (ccp_id == -1) {
            printk(KERN_WARNING "ccp-kpipe: max ccps registered\n");
            return -ENOMEM;
        }
    }
    ccpkp_dev->pipes[ccp_id] = pipe;
    pipe->ccp_id = ccp_id;
    ccpkp_dev->num_ccps++;
    mutex_unlock(&ccpkp_dev->mux);
    PDEBUG("init done");

    return 0;
}

void kpipe_cleanup(struct kpipe *pipe) {
    free_lfq(&pipe->ccp_write_queue);
    #ifndef ONE_PIPE
    free_lfq(&pipe->dp_write_queue);
    #endif
    kfree(pipe);
}

int ccpkp_user_release(struct inode *inp, struct file *fp) {
    struct kpipe *pipe = fp->private_data;
    int ccp_id = pipe->ccp_id;

    if (mutex_lock_interruptible(&ccpkp_dev->mux)) {
        return -ERESTARTSYS;
    }
    ccpkp_dev->pipes[pipe->ccp_id] = NULL;
    ccpkp_dev->num_ccps--;
    mutex_unlock(&ccpkp_dev->mux);
    
    kpipe_cleanup(pipe);
    fp->private_data = NULL;

    printk(KERN_INFO "ccp-kpipe: ccp %d closed\n", ccp_id);
    return 0;
}

ssize_t ccpkp_user_read(struct file *fp, char *buf, size_t bytes_to_read, loff_t *offset) {
    struct kpipe *pipe = fp->private_data;
#ifdef ONE_PIPE
    struct lfq *q = &(pipe->ccp_write_queue);
#else
    struct lfq *q = &(pipe->dp_write_queue);
#endif
    PDEBUG("user wants to read %lu bytes", bytes_to_read);
    return lfq_read(q, buf, bytes_to_read, USERSPACE);
}

// module stores pointer to corresponding ccp kpipe for each socket
ssize_t ccpkp_kernel_read(struct kpipe *pipe, char *buf, size_t bytes_to_read) {
#ifdef ONE_PIPE
    printk("error: compiled with a single pipe for test purposes. recompile with ONE_PIPE=n\n");
    return 0;
#endif
    struct lfq *q = &(pipe->ccp_write_queue);
    PDEBUG("kernel wants to read %lu bytes", bytes_to_read);
    return lfq_read(q, buf, bytes_to_read, KERNELSPACE);
}

ssize_t ccpkp_user_write(struct file *fp, const char *buf, size_t bytes_to_write, loff_t *offset) {
    struct kpipe *pipe = fp->private_data;
    struct lfq *q = &(pipe->ccp_write_queue);
    PDEBUG("user wants to write %lu bytes", bytes_to_write);
    return lfq_write(q, buf, bytes_to_write, 0, USERSPACE);
}


// module stores pointer to corresponding ccp kpipe for each socket
ssize_t ccpkp_kernel_write(struct kpipe *pipe, const char *buf, size_t bytes_to_write, int id) {
#ifdef ONE_PIPE
    printk("error: compiled with a single pipe for test purposes. recompile with ONE_PIPE=n\n");
    return 0;
#endif
    struct lfq *q = &(pipe->dp_write_queue);
    PDEBUG("kernel wants to write %lu bytes", bytes_to_write);
    return lfq_write(q, buf, bytes_to_write, id, KERNELSPACE);
}



void ccpkp_try_read(void) {
    ssize_t bytes_read;
    bytes_read = ccpkp_kernel_read(ccpkp_dev->pipes[curr_ccp_id], recvbuf, RECVBUF_LEN);
    if (bytes_read > 0) {
        PDEBUG("kernel read %ld bytes", bytes_read);
        libccp_read_msg(recvbuf, bytes_read);
    }
}

int ccpkp_sendmsg(
        struct ccp_connection *conn,
        char *buf,
        int bytes_to_write
) {
    if (bytes_to_write < 0) {
        return -1;
    }
    PDEBUG("kernel->user trying to write %d bytes", bytes_to_write);
    return ccpkp_kernel_write(ccpkp_dev->pipes[curr_ccp_id], buf, (size_t) bytes_to_write, (int) conn->index+1);
}
