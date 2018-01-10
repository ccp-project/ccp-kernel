#ifndef TCP_CCP_H
#define TCP_CCP_H

#include <linux/net.h>

/* -----------------------------------------------------------------------------
 * Serialization definitions
 * -----------------------------------------------------------------------------
 */

#define CREATE  0
#define MEASURE 1
#define DROP    2
#define PATTERN 3

#define BIGGEST_MSG_SIZE 256
#define MAX_STRING_SIZE 250

/* (type, len, socket_id) header
 * -----------------------------------
 * | Msg Type | Len (B)  | Uint32    |
 * | (1 B)    | (1 B)    | (32 bits) |
 * -----------------------------------
 * total: 6 Bytes
 */
struct __attribute__((packed, aligned(2))) CcpMsgHeader {
    uint8_t Type;
    uint8_t Len;
    uint32_t SocketId;
};

/* 
 * CREATE:  1 u32, 0 u64, str
 */
struct __attribute__((packed, aligned(4))) CreateMsg {
    uint32_t startSeq;
    char congAlg[MAX_STRING_SIZE];
};

/* 
 * MEASURE: 3 u32, 2 u64, no str
 */
struct __attribute__((packed, aligned(4))) MeasureMsg {
    uint32_t ackNo;
    uint32_t rtt;
    uint32_t loss;
    uint64_t rin;
    uint64_t rout;
};

enum drop_type {
    NO_DROP,
    DROP_TIMEOUT,
    DROP_DUPACK,
    DROP_ECN
};

/* 
 * DROP:    0 u32, 0 u64, str
 */
struct __attribute__((packed, aligned(4))) DropMsg {
    char type[MAX_STRING_SIZE];
};

/* 
 * PATTERN:  1 u32, 0 u64, str
 */
struct __attribute__((packed, aligned(4))) PatternMsg {
    uint32_t numStates;
    char pattern[MAX_STRING_SIZE];
};

#define SETRATEABS 0 
#define SETCWNDABS 1 
#define SETRATEREL 2 
#define WAITABS    3
#define WAITREL    4
#define REPORT     5

struct __attribute__((packed, aligned(2))) PatternEvent {
    uint8_t type;
    uint8_t size;
    uint32_t val;
};

int readHeader(struct CcpMsgHeader *hdr, char *buf);

/* return: number of bytes written to buf
 */
int serializeHeader(char *buf, int bufsize, struct CcpMsgHeader *hdr);

/* return: number of bytes written to buf
 */
int serializeMeasureMsg(char *buf, int bufsize, struct MeasureMsg *msg);

/* return: number of bytes written to buf
 */
int writeCreateMsg(
    char *buf, 
    int bufsize,
    uint32_t sid, 
    uint32_t startSeq, 
    char* str
);

/* return: number of bytes written to buf
 */
int writeMeasureMsg(
    char *buf, 
    int bufsize,
    uint32_t sid, 
    uint32_t ackNo, 
    uint32_t rtt,
    uint32_t loss,
    uint64_t rin,
    uint64_t rout
);

/* return: number of bytes written to buf
 */
int writeDropMsg(
    char *buf, 
    int bufsize,
    uint32_t sid, 
    char* str
);

/* return: size of msg
 */
int readMsg(
    struct CcpMsgHeader *hdr, 
    struct PatternMsg *msg,
    char *buf 
);

/* Events serialized in the string in PatternMsg
 * if no val in event, set to 0
 *
 * seq: array of PatternEvents
 * return: 0 if ok, -1 otherwise
 */
int readPattern(
    struct PatternEvent *seq,
    char *pattern,
    int numEvents
);

/* -----------------------------------------------------------------------------
 * CCP Netlink Communication Management
 * -----------------------------------------------------------------------------
 */

struct ccp_measurement {
    u32 ack;
    u32 rtt;
    u32 loss;
    u64 rin;
    u64 rout;
};

void set_ccp_nl_sk(struct sock *sk);
void free_ccp_nl_sk(void);

// CCP connection lookup
struct ccp_connection {
    // the index of this array element
    uint16_t index; 

    // the tcp connection
    // (NOT the netlink socket, that is global ccp_nl_sk)
    struct sock *sk;  
};

int ccp_init_connection_map(void);
void ccp_free_connection_map(void);
uint16_t ccp_connection_start(struct sock *sk);
void ccp_connection_free(uint16_t sid);
struct sock *ccp_connection_lookup(uint16_t sid);

void nl_recv(struct sk_buff *skb);

int nl_send_conn_create(
    uint16_t ccp_index, 
    uint32_t startSeq
);

void nl_send_measurement(
    uint16_t ccp_index, 
    struct ccp_measurement mmt
);

int nl_send_drop_notif(
    uint16_t ccp_index,
    enum drop_type dtype
);

/* -----------------------------------------------------------------------------
 * CCP TCP state
 * -----------------------------------------------------------------------------
 */

struct ccp {
    // control
    u32 rate;
    struct PatternEvent *pattern;
    uint8_t currPatternEvent;
    uint8_t numPatternEvents;
    u32 next_event_time;
    enum drop_type last_drop_state;
    uint32_t num_loss;

    // measurement
    struct ccp_measurement mmt;
	u64 prev_rin;
	u64 prev_rout;
	u32 prev_mmt_time;
    // communication
    uint16_t ccp_index;
};

#define MTU 1500
#define S_TO_US 1000000

void ccp_set_pacing_rate(struct sock *sk);

/* -----------------------------------------------------------------------------
 * CCP Sending State Machine Management 
 * -----------------------------------------------------------------------------
 */

void sendStateMachine(
    struct sock *sk
);

void installPattern(
    struct sock *sk,
    int numEvents,
    struct PatternEvent *seq
);

#endif
