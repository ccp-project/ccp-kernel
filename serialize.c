#include <linux/string.h>

#include "serialize.h"

// kernel datapath should never have to write a CWND msg
int writeCwndMsg(char *buf, uint32_t sid, uint32_t cwnd) {
    struct UInt32AndUInt32 cwndMsg;
    void *b;
    
    cwndMsg.Type = CWND;
    cwndMsg.Len = 10;
    cwndMsg.Val1 = sid;
    cwndMsg.Val2 = cwnd;

    b = (void*) &cwndMsg;
    memcpy(buf, (char*) b, cwndMsg.Len);
    return 10;
}

int readCwndMsg(char *buf, struct UInt32AndUInt32 *msg) {
    memcpy(msg, buf, sizeof(struct UInt32AndUInt32));

    switch (msg->Type) {
        case CWND:
            break;
        default:
            msg = NULL;
            return -1;
    }
    return 10;
}

int writeAckMsg(char *buf, uint32_t sid, uint32_t ackNo, uint64_t rtt) {
    struct UInt32AndUInt32AndUInt64 ackMsg;
    void *b;
    
    ackMsg.Type = ACK;
    ackMsg.Len = 18;
    ackMsg.Val1 = sid;
    ackMsg.Val2 = ackNo;
    ackMsg.Val3 = rtt;

    b = (void*) &ackMsg;
    memcpy(buf, (char*) b, ackMsg.Len);
    return 18;
}

// kernel datapath should never have to read a ACK msg
int readAckMsg(char *buf, struct UInt32AndUInt32AndUInt64 *msg) {
    memcpy(msg, buf, sizeof(struct UInt32AndUInt32AndUInt64));

    switch (msg->Type) {
        case ACK:
            break;
        default:
            msg = NULL;
            return -1;
    }
    return 18;
}

int writeCreateMsg(
    char *buf, 
    uint32_t sid, 
    uint32_t startSeq, 
    char* str
) {
    int len;
    struct UInt32AndUInt32AndString crMsg;
    void *b;
    
    len = strlen(str) + 1;
    if (len > MAX_STRING_SIZE) {
        return -1;
    }

    crMsg.Type = CREATE;
    crMsg.Len  = len + 10;
    crMsg.Val  = sid;
    crMsg.Val2 = startSeq;

    memset(crMsg.Str, 0, MAX_STRING_SIZE);
    strncpy(crMsg.Str, str, len);

    b = (void*) &crMsg;
    memcpy(buf, (char*) b, len+10);
    return crMsg.Len;
}

// kernel datapath should never have to read a CREATE msg
int readCreateMsg(char *buf, struct UInt32AndUInt32AndString *msg) {
    memcpy(msg, buf, sizeof(struct UInt32AndUInt32AndString));

    switch (msg->Type) {
        case CREATE:
            break;
        default:
            msg = NULL;
            return -1;
    }
    return msg->Len;
}

int writeDropMsg(char *buf, uint32_t sid, char* str) {
    int len;
    struct UInt32AndString drMsg;
    void *b;
    
    len = strlen(str) + 1;
    if (len > MAX_STRING_SIZE) {
        return -1;
    }

    drMsg.Type = DROP;
    drMsg.Len = len + 6;
    drMsg.Val = sid,

    memset(drMsg.Str, 0, MAX_STRING_SIZE);
    strncpy(drMsg.Str, str, len);

    b = (void*) &drMsg;
    memcpy(buf, (char*) b, len+6);
    return drMsg.Len;
}

// kernel datapath should never have to read a DROP msg
int readDropMsg(char *buf, struct UInt32AndString *msg) {
    memcpy(msg, buf, sizeof(struct UInt32AndString));

    switch (msg->Type) {
        case DROP:
            break;
        default:
            msg = NULL;
            return -1;
    }
    return msg->Len;
}
