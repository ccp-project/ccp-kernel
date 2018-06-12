#ifndef _LFQ_H_
#define _LFQ_H_

#ifdef __KERNEL__
    #include <linux/slab.h>
    #include <linux/sched.h>
    #include <linux/wait.h>
    #include <linux/uaccess.h>

    #ifndef __MALLOC__
            #define __MALLOC__(size) kmalloc(size, GFP_KERNEL)
    #endif
    #ifndef ___FREE___
            #define ___FREE___(p)      kfree(p)
    #endif
    #define CAS(a,o,n)       cmpxchg(a,o,n) == o
    #define ASSERT(cond)
    #ifndef COPY_TO_USER
            #define COPY_TO_USER(dst, src, n) copy_to_user(dst, src, n)
    #endif
    #ifndef COPY_FROM_USER
            #define COPY_FROM_USER(dst, src, n) copy_from_user(dst, src, n)
    #endif
#else
    #include <stdbool.h>
    #include <stdlib.h>
    #include <string.h>
    #include <stdio.h>
    #include <stdint.h>
    #include <errno.h>
    #include <assert.h>
    #include <pthread.h>

    #ifndef __MALLOC__
        #define __MALLOC__(size) malloc(size)
    #endif
    #ifndef ___FREE___
        #define ___FREE___(p)      free(p)
    #endif
    #define CAS(a,o,n)       __sync_bool_compare_and_swap(a,o,n)
    #define ASSERT(cond) assert(cond)
    #ifndef COPY_TO_USER
            #define COPY_TO_USER(dst, src, n) memcpy(dst, src, n)
    #endif
    #ifndef COPY_FROM_USER
            #define COPY_FROM_USER(dst, src, n) memcpy(dst, src, n)
    #endif
#endif


#ifdef __DEBUG__
    #ifdef __KERNEL__
         /* This one if debugging is on, and kernel space */
        #define PDEBUG(fmt, args...) printk( KERN_DEBUG "ccp-kpipe: " fmt, ## args)
    #else
        /* This one for user space */
        #define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
    #endif
#else
    /* Debugging off */
    #define PDEBUG(fmt, args...) 
#endif

#ifndef max
#define max(a,b) \
 ({ __typeof__ (a) _a = (a); \
         __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#define min(a,b) \
 ({ __typeof__ (a) _a = (a); \
         __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })
#endif

#define idx_t uint16_t 
#define KERNELSPACE 0
#define USERSPACE 1

// Must be a divisor of max val of id_t
#define BACKLOG 1024
#define MAX_MSG_LEN 512
#define BUF_LEN (BACKLOG*MAX_MSG_LEN)

struct lfq {
    char *buf;
    char **msg_list;
    char **free_list;

    idx_t read_head, write_head;
    idx_t free_head, free_tail;

    bool blocking;
#ifdef __KERNEL__
    wait_queue_head_t nonempty;
#else
    pthread_cond_t nonempty;
    pthread_mutex_t wait_lock;
#endif
};

struct pipe {
    struct lfq ccp_write_queue;
    struct lfq dp_write_queue;
};

int init_lfq(struct lfq *q, bool blocking);
void free_lfq(struct lfq *q);
void init_pipe(struct pipe *p, bool blocking);
void free_pipe(struct pipe *p);

char* _lfq_acquire_free_block(struct lfq *q);
void _lfq_return_block(struct lfq *q, char *block);
uint16_t read_portus_msg_size(char *buf);

ssize_t lfq_read(struct lfq *q, char *buf, size_t bytes_to_read, int reader_t);
ssize_t lfq_write(struct lfq *q, const char *buf, size_t bytes_to_write, int id, int writer_t);
ssize_t ccp_write(struct pipe *p, const char *buf, size_t bytes_to_write, int id);
ssize_t ccp_read(struct pipe *p, char *buf, size_t bytes_to_read);
ssize_t dp_write(struct pipe *p, const char *buf, size_t bytes_to_write, int id);
ssize_t dp_read(struct pipe *p, char *buf, size_t bytes_to_read);

#endif
