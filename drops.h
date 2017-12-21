#ifndef CCP_DROPMSG_H
#define CCP_DROPMSG_H

#include <linux/string.h>

/* (type, len, socket_id) header
 * -----------------------------------
 * | Msg Type | Len (B)  | Uint32    |
 * | (1 B)    | (1 B)    | (32 bits) |
 * -----------------------------------
 * total: 6 Bytes
 */
struct __attribute__((packed, aligned(2))) CcpMsgHeader {
    u8 Type;
    u8 Len;
    u32 SocketId;
};

#define DROP    2
/* We only write Create, Measure, and Drop messages.
 */
extern int serialize_header(char *buf, int bufsize, struct CcpMsgHeader *hdr);

#define BIGGEST_MSG_SIZE 256
#define MAX_STRING_SIZE 250

/* DROP
 * str: the type of drop observed
 *      TODO remove the drop message, replace it with a returned register
 *      TODO and an urgent register
 */
struct __attribute__((packed, aligned(4))) DropMsg {
    char type[MAX_STRING_SIZE];
};

/* Write dr: DropMsg into buf with socketid sid.
 * buf should be preallocated, and bufsize should be its size.
 * TODO replace the existence of dropMsg with an implicit ("urgent") register
 */
int write_drop_msg(
    char *buf, 
    int bufsize,
    u32 sid, 
    struct DropMsg msg
) {
    int ret, ok;
    int dropMsgLen = strlen(msg.type) + 1;
    struct CcpMsgHeader hdr = {
        .Type = DROP, 
        .Len = 6 + dropMsgLen, 
        .SocketId = sid,
    };

    if (bufsize < sizeof(struct CcpMsgHeader) + dropMsgLen) {
        return -2;
    }

    ok = serialize_header(buf, bufsize, &hdr);
    if (ok < 0) {
        return ok;
    }

    buf += ok;
    ret = ok;
    memcpy(buf, &msg, hdr.Len);
    return hdr.Len;
}


int send_drop_notif(
    struct ccp_connection *dp,
    enum drop_type dtype
) {
    char msg[BIGGEST_MSG_SIZE];
    int ok;
    int msg_size;
    struct DropMsg dr;
    
    if (dp->index < 1) {
        return -1;
    }

    switch (dtype) {
        case DROP_TIMEOUT:
            strcpy(dr.type, "timeout");
            msg_size = write_drop_msg(msg, BIGGEST_MSG_SIZE, dp->index, dr);
            break;
        case DROP_DUPACK:
            strcpy(dr.type, "dupack");
            msg_size = write_drop_msg(msg, BIGGEST_MSG_SIZE, dp->index, dr);
            break;
        case DROP_ECN:
            strcpy(dr.type, "ecn");
            msg_size = write_drop_msg(msg, BIGGEST_MSG_SIZE, dp->index, dr);
            break;
        default:
            return -2;
    }
        
    ok = dp->send_msg(msg, msg_size);
    return ok;
}

#endif
