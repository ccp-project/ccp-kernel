#ifndef CCP_SERIALIZE_H
#define CCP_SERIALIZE_H

#include <linux/kernel.h>
#include "tcp_ccp.h"

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
 * MEASURE: 2 u32, 2 u64, no str
 */
struct __attribute__((packed, aligned(4))) MeasureMsg {
    uint32_t ackNo;
    uint32_t rtt;
    uint64_t rin;
    uint64_t rout;
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

#endif
