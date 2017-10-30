#include <linux/string.h>

#include "tcp_ccp.h"

int readHeader(struct CcpMsgHeader *hdr, char *buf) {
    memcpy(hdr, buf, sizeof(struct CcpMsgHeader));
    switch (hdr->Type) {
    case CREATE:
    case MEASURE:
    case DROP:
    case PATTERN:
        return sizeof(struct CcpMsgHeader);
    default:
        return -1;
    }
}

int serializeHeader(char *buf, int bufsize, struct CcpMsgHeader *hdr) {
    switch (hdr->Type) {
    case CREATE:
    case MEASURE:
    case DROP:
    case PATTERN:
        break;
    default:
        return -1;
    }

    if (bufsize < sizeof(struct CcpMsgHeader)) {
        return -2;
    }

    memcpy(buf, hdr, sizeof(struct CcpMsgHeader));
    return sizeof(struct CcpMsgHeader);
}

/* buf is pointer to message buffer after the header has been written
 * bufsize is the remaining size in the buffer
 */
int serializeMeasureMsg(char *buf, int bufsize, struct MeasureMsg *msg) {
    if (bufsize < sizeof(struct MeasureMsg)) {
        return -2;
    }

    memcpy(buf, msg, bufsize);
    return sizeof(struct MeasureMsg);
}

int writeCreateMsg(
    char *buf, 
    int bufsize,
    uint32_t sid, 
    uint32_t startSeq, 
    char* congAlg
) {
    int ok;
    int congAlgLen = strlen(congAlg) + 1;
    struct CcpMsgHeader hdr = {
        .Type = CREATE, 
        .Len = 10 + congAlgLen, 
        .SocketId = sid,
    };
    
    ok = serializeHeader(buf, bufsize, &hdr);
    if (ok < 0) {
        return ok;
    }

    // advance write head by header size
    buf += ok;

    memcpy(buf, &startSeq, sizeof(uint32_t));

    // advance write head by uint32_t size
    buf += sizeof(uint32_t);
    ok += sizeof(uint32_t);

    memset(buf, 0, congAlgLen);
    strncpy(buf, congAlg, congAlgLen);
    ok += congAlgLen;

    return ok;
}

int writeMeasureMsg(
    char *buf, 
    int bufsize,
    uint32_t sid, 
    uint32_t ackNo, 
    uint32_t rtt,
    uint32_t loss,
    uint64_t rin,
    uint64_t rout
) {
    int ok;
    size_t ret;
    struct CcpMsgHeader hdr = {
        .Type = MEASURE, 
        .Len = 34,
        .SocketId = sid,
    };

    struct MeasureMsg msg = {
        .ackNo = ackNo,
        .rtt = rtt,
        .loss = loss,
        .rin = rin,
        .rout = rout,
    };

    ok = serializeHeader(buf, bufsize, &hdr);
    if (ok < 0) {
        return ok;
    }

    buf += ok;
    ret = ok;
    ok = serializeMeasureMsg(buf, bufsize - ok, &msg);
    if (ok < 0) {
        return -2;
    }

    return ret + ok;
}

int writeDropMsg(
    char *buf, 
    int bufsize,
    uint32_t sid, 
    char* str
) {
    int ok;
    int dropMsgLen = strlen(str) + 1;
    struct CcpMsgHeader hdr = {
        .Type = DROP, 
        .Len = 6 + dropMsgLen, 
        .SocketId = sid,
    };

    if (bufsize < sizeof(struct CcpMsgHeader) + dropMsgLen) {
        return -1;
    }

    ok = serializeHeader(buf, bufsize, &hdr);
    if (ok < 0) {
        return ok;
    }

    // advance write head by header size
    buf += ok;

    memset(buf, 0, dropMsgLen);
    strncpy(buf, str, dropMsgLen);

    return ok + dropMsgLen;
}

int readMsg(
    struct CcpMsgHeader *hdr, 
    struct PatternMsg *msg,
    char *buf
) {
    int ok;
    ok = readHeader(hdr, buf);
    if (ok < 0) {
        return ok;
    }

    buf += ok;
    if (hdr->Type != PATTERN) {
        return -1;
    }

    memcpy(msg, buf, hdr->Len - 6);
    return hdr->Len;
}

int readPattern(
    struct PatternEvent *seq,
    char *pattern,
    int numEvents
) {
    int i;
    for (i = 0; i < numEvents; i++) {
        memcpy(&(seq[i]), pattern, sizeof(struct PatternEvent));
        if (seq[i].size == 2 && seq[i].type == REPORT) {
            pattern += 2;
            seq[i].val = 0;
            continue;
        } else if (seq[i].size != 6) {
            // only report events are 2 bytes
            // all other events are 6 bytes
            return -1;
        }

        pattern += seq[i].size;
    }

    return 0;
}
