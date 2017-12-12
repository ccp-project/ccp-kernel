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
#define INSTRUCTION 4

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

/*
 * INSTRUCTION: 4 u8s - Opcode, Result, Left, Right
 */
struct __attribute__((packed, aligned(4))) InstructionMsg {
    uint8_t opcode;
    uint8_t result_register;
    uint8_t left_register;
    uint8_t right_register;
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

/* Instructions serialized in the string in InstructionMsg
 * if no val in instructions, set to 0
 */
int readInstruction(
    struct InstructionMsg *seq,
    char *instr,
    int numEvents //
);
/* -----------------------------------------------------------------------------
 * CCP Fold Function Primitives
 * -----------------------------------------------------------------------------
 */
// TODO: more than u64 functions
//
// limits on the number of signals and instructions

#define MAX_FOLD_INSTRUCTIONS 20 // allocated NOT within the ccp struct

// limits on how many registers the user can send down
#define MAX_STATE_REG 5
#define MAX_TMP_REG 5 // for intermediate computations
#define NUM_REG 10

// rate sample primitives
#define ACK 0
#define RTT 1
#define LOSS 2
#define RIN 3
#define ROUT 4

enum RegType64 {
    STATE_REG,
    TMP_REG,
    PRIMITIVE_REG,
    CONST_REG
};

enum FoldOp {
    ADD64, // (add a b) return a+b
    DIV64, // (div a b) return a/b (integer division)
    EQUIV64, // (eq a b) return a == b
    EWMA64, // (ewma a b) return old * (a/10) + b * (1-(a/10)) old is return reg
    GT64, // (> a b) return a > b
    LT64, // (< a b) return a < b
    MAX64, // (max a b) return max(a,b)
    MIN64, // (min a b) return min(a,b)
    MUL64, // (mul a b) return a * b
    SUB64, // (sub a b) return a - b
    BIND64, // add a to store
    IFCNT64, // if (a) add 1 to store
    IFNOTCNT64, // if not a, add 1 to store
};

// for bind, ifcnt and ifnotcnt, operations are directly inline
u64 myadd64(u64 a, u64 b);
u64 mydiv64(u64 a, u64 b);
u64 myequiv64(u64 a, u64 b);
u64 myewma64(u64 a, u64 b, u64 c); // when parsed - ewma explicitly has 3 args - b old, c new, a alpha
u64 mygt64(u64 a, u64 b);
u64 mylt64(u64 a, u64 b);
u64 mymax64(u64 a, u64 b);
u64 mymin64(u64 a, u64 b);
u64 mymul64(u64 a, u64 b);
u64 mysub64(u64 a, u64 b);

struct Register {
    enum RegType64 type;
    int index;
    u64 value;
};

struct InitialValue {
    struct Register reg;
    u64 value;
};

// for EWMA, IFCNT: store register is implicit 'old' state argument
struct Instruction64 {
    enum FoldOp op;
    struct Register r1;
    struct Register r2; // 2 arguments
    struct Register rStore; // store register
};

// Maps instructions to their corresponding instruction map
struct ccp_instruction_list {
    // index of this array element, corresponds to index in connection map
    uint16_t index;
    // array of instructions
    struct Instruction64 fold_instructions[MAX_FOLD_INSTRUCTIONS];
    // number of instructions
    int num_instructions;
    // state registers
    u64 state_registers[MAX_STATE_REG];
    // tmp registers
    u64 tmp_registers[MAX_TMP_REG];
};

/* -----------------------------------------------------------------------------
 * CCP Netlink Communication Management
 * -----------------------------------------------------------------------------
 */

struct ccp_primitives {
    u64 ack;
    u64 rtt;
    u64 loss;
    u64 rin;
    u64 rout;
};
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
int ccp_init_fold_map(void);
void ccp_free_fold_map(void);
uint16_t ccp_connection_start(struct sock *sk);
void ccp_connection_free(uint16_t sid);
struct sock *ccp_connection_lookup(uint16_t sid);
struct ccp_instruction_list *ccp_instruction_list_lookup(uint16_t sid);

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
    u32 rate; // 4 B
    struct PatternEvent *pattern; // 6 B
    uint8_t currPatternEvent; // 1 B
    uint8_t numPatternEvents; // 1 B
    u32 next_event_time; // 4 B
    enum drop_type last_drop_state; // 1 b

    // measurement
    struct ccp_primitives mmt; // 40 B
    
    // communication
    uint16_t ccp_index; // 2 B

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
void load_dummy_instr(struct ccp_instruction_list *instructions);

#endif
