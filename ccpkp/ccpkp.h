#ifndef _CCPKP_H_
#define _CCPKP_H_

#include <linux/slab.h>
#include <linux/cdev.h>
#include "lfq/lfq.h"
#include "../libccp/ccp.h"

#ifndef MAX_CCPS
#define MAX_CCPS 32
#endif

typedef int (*ccp_recv_handler)(char *msg, int msg_size);

struct kpipe {
    int    ccp_id;              /* Index of this pipe in pipes */
    struct lfq ccp_write_queue; /* Queue from user to kernel  */
    struct lfq dp_write_queue;  /* Queue from kernel to user  */
};

struct ccpkp_dev {
    int    num_ccps;
    struct kpipe *pipes[MAX_CCPS];
    struct cdev cdev;
    struct mutex mux;
};

int         ccpkp_init(ccp_recv_handler handler);
int         ccpkp_user_open(struct inode *, struct file *);
ssize_t     ccpkp_user_read(struct file *fp, char *buf, size_t bytes_to_read, loff_t *offset);
void        ccpkp_try_read(void);
ssize_t     ccpkp_kernel_read(struct kpipe *pipe, char *buf, size_t bytes_to_read);
ssize_t     ccpkp_user_write(struct file *fp, const char *buf, size_t bytes_to_write, loff_t *offset);
int         ccpkp_sendmsg(struct ccp_connection *conn, char *buf, int bytes_to_write);
ssize_t     ccpkp_kernel_write(struct kpipe *pipe, const char *buf, size_t bytes_to_read, int id);
int         ccpkp_user_release(struct inode *, struct file *);
void        ccpkp_cleanup(void);


#endif
