#include <net/tcp.h>

#include "tcp_ccp.h"

#define CCP_MULTICAST_GROUP 22
#define MAX_NUM_CONNECTIONS 100

// array of active connections
struct ccp_connection* ccp_active_connections;

// netlink socket
struct sock *ccp_nl_sk;

void set_ccp_nl_sk(struct sock *sk) {
    ccp_nl_sk = sk;
}

void free_ccp_nl_sk(void) {
    netlink_kernel_release(ccp_nl_sk);
}

// initialize the ccp active connections list
// return -1 on allocation failure, should abort loading module
int ccp_init_connection_map(void) {
    ccp_active_connections = kmalloc(MAX_NUM_CONNECTIONS * sizeof(struct ccp_connection), GFP_KERNEL);
    if (!ccp_active_connections) {
        return -1;
    }

    memset(ccp_active_connections, 0, MAX_NUM_CONNECTIONS * sizeof(struct ccp_connection));

    return 0;
}

// free the memory upon module unloading
void ccp_free_connection_map(void) {
    kfree(ccp_active_connections);
    ccp_active_connections = NULL;
}

// put a new connection into the active connections list
// returns the index at which the connection was placed
// index should be used as the ccp socket id
// return 0 on error
uint16_t ccp_connection_start(struct sock *sk) {
    uint16_t sid;
    struct ccp_connection *conn;
    printk(KERN_INFO "Entering %s\n", __FUNCTION__);

    // linear search to find empty place
    // index = 0 means free/unused
    for (sid = 0; sid < MAX_NUM_CONNECTIONS; sid++) {
        conn = &ccp_active_connections[sid];
        if (conn->index == 0) {
            // found a free slot
            conn->index = sid+1;
            conn->sk = sk;
            return sid+1;
        }
    }

    return 0;
}

// lookup existing connection by its ccp socket id
// return NULL on error
struct sock *ccp_connection_lookup(uint16_t sid) {
    struct ccp_connection *conn;
    //printk(KERN_INFO "Entering %s\n", __FUNCTION__);
    // bounds check
    if (sid == 0 || sid > MAX_NUM_CONNECTIONS) {
        printk(KERN_INFO "index out of bounds: %d", sid);
        return NULL;
    }

    conn = &ccp_active_connections[sid-1];
    if (conn->index != sid) {
        printk(KERN_INFO "index mismatch: sid %d, index %d", sid, conn->index);
        return NULL;
    }

    return conn->sk;
}

// after connection ends, free its slot in the ccp table
void ccp_connection_free(uint16_t sid) {
    struct ccp_connection *conn;
    printk(KERN_INFO "Entering %s\n", __FUNCTION__);
    // bounds check
    if (sid == 0 || sid > MAX_NUM_CONNECTIONS) {
        printk(KERN_INFO "index out of bounds: %d", sid);
        return;
    }

    conn = &ccp_active_connections[sid-1];
    if (conn->index != sid) {
        printk(KERN_INFO "index mismatch: sid %d, index %d", sid, conn->index);
        return;
    }

    conn->index = 0;
    conn->sk = NULL;

    return;
}

// callback from userspace ccp
// all messages will be PatternMsg
// lookup ccp socket id, install new pattern
void nl_recv(struct sk_buff *skb) {
    int ok;
    struct sock *sk;
    struct CcpMsgHeader hdr;
    struct PatternMsg msg;
    struct PatternEvent *sequence;
    struct nlmsghdr *nlh = nlmsg_hdr(skb);

    ok = readMsg(&hdr, &msg, (char*)nlmsg_data(nlh));  
    if (ok < 0) {
        return;
    }

    sk = ccp_connection_lookup(hdr.SocketId);
    if (sk == NULL) {
        return;
    }

    sequence = kmalloc(msg.numStates * sizeof(struct PatternEvent), GFP_KERNEL);
    if (!sequence) {
        return;
    }

    ok = readPattern(sequence, msg.pattern, msg.numStates);
    if (ok < 0) {
        return;
    }

    installPattern(sk, msg.numStates, sequence);
}

// send IPC message to userspace ccp
static int nl_sendmsg(
    char *msg, 
    int msg_size
) {
    int res;
    struct sk_buff *skb_out;
    struct nlmsghdr *nlh;

    skb_out = nlmsg_new(
        msg_size,  // @payload: size of the message payload
        GFP_NOWAIT // @flags: the type of memory to allocate.
    );
    if (!skb_out) {
        printk(KERN_ERR "Failed to allocate new skb\n");
        return -1;
    }

    nlh = nlmsg_put(
        skb_out,    // @skb: socket buffer to store message in
        0,          // @portid: netlink PORTID of requesting application
        0,          // @seq: sequence number of message
        NLMSG_DONE, // @type: message type
        msg_size,   // @payload: length of message payload
        0           // @flags: message flags
    );

    memcpy(nlmsg_data(nlh), msg, msg_size);
    // https://www.spinics.net/lists/netdev/msg435978.html
    // "It is process context but with a spinlock (bh_lock_sock) held, so
    // you still can't sleep. IOW, you have to pass a proper gfp flag to
    // reflect this."
    // Use an allocation without __GFP_DIRECT_RECLAIM
    res = nlmsg_multicast(
        ccp_nl_sk,               // @sk: netlink socket to spread messages to
        skb_out,             // @skb: netlink message as socket buffer
        0,                   // @portid: own netlink portid to avoid sending to yourself
        CCP_MULTICAST_GROUP, // @group: multicast group id
        GFP_NOWAIT           // @flags: allocation flags
    );
    if (res < 0) {
        return res;
    }

    return 0;
}

// send create msg
int nl_send_conn_create(
    uint16_t ccp_index, 
    uint32_t startSeq
) {
    char msg[BIGGEST_MSG_SIZE];
    int ok;
    int msg_size;
    
    if (ccp_index < 1) {
        return -1;
    }

    printk(KERN_INFO "sending create: id=%u, startSeq=%u\n", ccp_index, startSeq);

    msg_size = writeCreateMsg(msg, BIGGEST_MSG_SIZE, ccp_index, startSeq, "reno");
    ok = nl_sendmsg(msg, msg_size);
    if (ok < 0) {
        printk(KERN_INFO "create notif failed: id=%u, err=%d\n", ccp_index, ok);
    }

    return ok;
}

// send datapath measurements
// acks, rtt, rin, rout
void nl_send_measurement(
    uint16_t ccp_index, 
    struct ccp_measurement mmt
) {
    char msg[BIGGEST_MSG_SIZE];
    int ok;
    int msg_size;
    
    if (ccp_index < 1) {
        return;
    }
        
    printk(KERN_INFO "sending measurement notif: id=%u, cumAck=%u, rtt=%u, rin=%llu, rout=%llu\n", ccp_index, mmt.ack, mmt.rtt, mmt.rin, mmt.rout);
    msg_size = writeMeasureMsg(msg, BIGGEST_MSG_SIZE, ccp_index, mmt.ack, mmt.rtt, mmt.rin, mmt.rout);
    // it's ok if this send fails
    // will auto-retry on the next ack
    ok = nl_sendmsg(msg, msg_size);
    if (ok < 0) {
        printk(KERN_INFO "mmt notif failed: id=%u, cumAck=%u, rtt=%u, rin=%llu, rout=%llu\n", ccp_index, mmt.ack, mmt.rtt, mmt.rin, mmt.rout);
    }
}

int nl_send_drop_notif(
    uint16_t ccp_index,
    enum drop_type dtype
) {
    char msg[BIGGEST_MSG_SIZE];
    int ok;
    int msg_size;
    
    if (ccp_index < 1) {
        pr_info("ccp_index malformed: %d\n", ccp_index);
        return -1;
    }

    printk(KERN_INFO "sending drop: id=%u, ev=%d\n", ccp_index, dtype);

    switch (dtype) {
        case DROP_TIMEOUT:
            msg_size = writeDropMsg(msg, BIGGEST_MSG_SIZE, ccp_index, "timeout");
            break;
        case DROP_DUPACK:
            msg_size = writeDropMsg(msg, BIGGEST_MSG_SIZE, ccp_index, "dupack");
            break;
        case DROP_ECN:
            msg_size = writeDropMsg(msg, BIGGEST_MSG_SIZE, ccp_index, "ecn");
            break;
        default:
            printk(KERN_INFO "sending drop: unknown event? id=%u, ev=%d != {%d, %d, %d}\n", ccp_index, dtype, DROP_TIMEOUT, DROP_DUPACK, DROP_ECN);
            return -2;
    }
        
    ok = nl_sendmsg(msg, msg_size);
    if (ok < 0) {
        printk(KERN_INFO "drop notif failed: id=%u, ev=%d, err=%d\n", ccp_index, dtype, ok);
    }

    return ok;
}
