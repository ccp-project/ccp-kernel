#include "lfq.h"

void debug_buf(const char *buf) {
	char out[256];
	char *tmp = out;
        int wrote = sprintf(tmp, "buf=%p\n", buf); 
        tmp += wrote;
	for(int i=0; i<64; i++) {
		sprintf(tmp, "|%2d", i);
		tmp += 3;
	}
	sprintf(tmp, "|\n");
	printk( KERN_DEBUG "%s", out);
	tmp = out;
	for(int i=0; i<64; i++) {
		sprintf(tmp, "|%02x", buf[i]);
		tmp += 3;
	}
	sprintf(tmp, "|\n");
	printk( KERN_DEBUG "%s", out);
}

int init_lfq(struct lfq *q, bool blocking) {
    q->buf       = __MALLOC__(BUF_LEN);
    if (!q->buf) {
        return -1;
    }
    q->msg_list  = __MALLOC__(BACKLOG * sizeof(char *));
    if (!q->msg_list) {
        ___FREE___(q->buf);
        return -1;
    }
    q->free_list = __MALLOC__(BACKLOG * sizeof(char *));
    if (!q->free_list) {
        ___FREE___(q->buf);
        ___FREE___(q->msg_list);
        return -1;
    }

    for (int i=0; i<BACKLOG; i++) {
        q->free_list[i] = &(q->buf[i * MAX_MSG_LEN]);
        q->msg_list[i] = NULL;
    }

    q->read_head  = 
    q->write_head = 
    q->free_head  = 0;
    q->free_tail  = BACKLOG-1;

    q->blocking = blocking;
    if (blocking) {
#ifdef __KERNEL__
        init_waitqueue_head(&q->nonempty);
#else
        pthread_mutex_init(&q->wait_lock, NULL);
        pthread_cond_init(&q->nonempty, NULL);
#endif
    }

    return 0;
}

void free_lfq(struct lfq *q) {
    ___FREE___(q->buf);
    ___FREE___(q->msg_list);
    ___FREE___(q->free_list);
}

void init_pipe(struct pipe *p, bool blocking) {
    init_lfq(&p->ccp_write_queue, blocking);
    init_lfq(&p->dp_write_queue, blocking);
}

void free_pipe(struct pipe *p) {
    free_lfq(&p->ccp_write_queue);
    free_lfq(&p->dp_write_queue);
    ___FREE___(p);
}

char* _lfq_acquire_free_block(struct lfq *q) {
    idx_t head, new_head;
    for (;;) {
        head = q->free_head;
        new_head = (head+1) % BACKLOG;
        if (new_head == q->free_tail) {
            return NULL; // Free list is (technically, almost) empty
        }
        if (CAS(&(q->free_head), head, new_head)) {
            break;
        }
    }

    if(new_head == 0) {
        new_head = BACKLOG;
    }

    return q->free_list[new_head-1];
}

void _lfq_return_block(struct lfq *q, char *block) {
    idx_t tail, new_tail;
    for (;;) {
        tail = q->free_tail;
        new_tail = (tail+1) % BACKLOG;
        //ASSERT(new_tail <= q->free_head);
        if (CAS(&(q->free_tail), tail, new_tail)) {
            break;
        }
    }

    if(new_tail == 0) {
        new_tail = BACKLOG;
    }

    PDEBUG("[reader  ] returned block to %d\n", new_tail);

    q->free_list[new_tail - 1] = block;
}

uint16_t read_portus_msg_size(char *buf) {
    return *(((uint16_t *)buf)+1);
}

inline bool ready_for_reading(struct lfq *q) {
    return (q->read_head != q->write_head) && (q->msg_list[q->read_head] != NULL);
}

ssize_t lfq_read(struct lfq *q, char *buf, size_t bytes_to_read, int reader_t) {

    if (q->blocking) {
wait_until_nonempty:
#ifndef __KERNEL__
        pthread_mutex_lock(&q->wait_lock);
#endif
        while (!ready_for_reading(q)) {
#ifdef __KERNEL__
            if (wait_event_interruptible(q->nonempty, ready_for_reading(q))) {
                return -ERESTARTSYS;
            }
#else
            pthread_cond_wait(&q->nonempty, &q->wait_lock);
#endif
        }
#ifndef __KERNEL__
        pthread_mutex_unlock(&q->wait_lock);
#endif
    } else {
        if (!ready_for_reading(q)) {
            return 0;
        }
    }

    int bytes_read = 0;

    PDEBUG("[reader  ] read=%d write=%d\n", q->read_head, q->write_head);

    idx_t old_r, new_r;
    int count = 1;
    for (;;) {
        old_r = new_r = q->read_head;
        int bytes_can_read = bytes_to_read;
        uint16_t bytes_in_block;
        while (bytes_can_read > 0) {
            if (q->msg_list[new_r] == NULL) {
                break;
            }
            bytes_in_block = read_portus_msg_size(q->msg_list[new_r]);
            bytes_can_read -= bytes_in_block;
            new_r = (new_r + 1) % BACKLOG;
            if (new_r == q->write_head) {
                 break;
            }
        }
        //PDEBUG("[reader  ] trying to move read from %d to %d\n", old_r, new_r);
        if (CAS(&(q->read_head), old_r, new_r)) {
            //PDEBUG("[reader  ] moved\n");
            break;
        }
        count++;
    }
    if (new_r < old_r) { // wrapped
        new_r += BACKLOG;
    }
    PDEBUG("reading from %d to %d\n", old_r, new_r);
    for (int i=old_r; i < new_r; i++) {
        int r = i % BACKLOG;
        char *block = q->msg_list[r];
        uint16_t bytes_in_block = read_portus_msg_size(block);
        PDEBUG("[reader  ] read #%d (@%ld) : %d bytes\n", r, block-q->buf, bytes_in_block);
        if (reader_t == USERSPACE) {
            COPY_TO_USER(buf, block, bytes_in_block);
        } else { // reader_t == KERNELSPACE
            memcpy(buf, block, bytes_in_block);
        }
        bytes_read += bytes_in_block;
        _lfq_return_block(q, block);
        q->msg_list[r] = NULL;
        buf += bytes_in_block;
    }
    
    if (bytes_read == 0) {
        goto wait_until_nonempty;
    }

    return bytes_read;
}


ssize_t lfq_write(struct lfq *q, const char *buf, size_t bytes_to_write, int id, int writer_t) {
    // Get free block
    char *block = _lfq_acquire_free_block(q);
    if (block == NULL) {
        PDEBUG("[writer %d] no free blocks available\n", id);
        return -1;
    }
    PDEBUG("[writer %d] acquired free block at %ld (head=%d, tail=%d)\n", id, block - q->buf, q->free_head, q->free_tail);

    // Copy data into block
    if (writer_t == USERSPACE) {
        COPY_FROM_USER(block, buf, bytes_to_write);
    } else { // writer_t == KERNELSPACE
        memcpy(block, buf, bytes_to_write);
    }

    // Get next position in queue
    idx_t old_i, new_i;
    int count = 1;
    for (;;) {
        old_i = q->write_head;
        new_i = (old_i + 1) % BACKLOG;
        if (new_i == q->read_head) {
            return 0; // TODO what do we want to do if there's no room?
        }
        if (CAS(&(q->write_head), old_i, new_i)) {
            break;
        }
        count++;
    }

    if (new_i == 0) {
        new_i = BACKLOG;
    }
    PDEBUG("[writer %d] secured queue #%d : %ld bytes\n", id, (new_i-1), bytes_to_write);

    // Assign block to acquired position
    q->msg_list[new_i-1] = block;

    if (q->blocking) {
#ifdef __KERNEL__
        wake_up_interruptible(&q->nonempty);
#else
        pthread_mutex_lock(&q->wait_lock);
        pthread_cond_signal(&q->nonempty);
        pthread_mutex_unlock(&q->wait_lock);
#endif
    }

    return bytes_to_write;
}

ssize_t ccp_write(struct pipe *p, const char *buf, size_t bytes_to_write, int id) {
    return lfq_write(&p->ccp_write_queue, buf, bytes_to_write, id, USERSPACE);
}
ssize_t ccp_read(struct pipe *p, char *buf, size_t bytes_to_read) {
    return lfq_read(&p->dp_write_queue, buf, bytes_to_read, USERSPACE);
}
ssize_t dp_write(struct pipe *p, const char *buf, size_t bytes_to_write, int id) {
    return lfq_write(&p->dp_write_queue, buf, bytes_to_write, id, KERNELSPACE);
}
ssize_t dp_read(struct pipe *p, char *buf, size_t bytes_to_read) {
    return lfq_read(&p->ccp_write_queue, buf, bytes_to_read, KERNELSPACE);
}
