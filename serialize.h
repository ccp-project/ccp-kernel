#ifndef CCP_SERIALIZE_H
#define CCP_SERIALIZE_H

#include <linux/kernel.h>

#define CREATE 0
#define ACK 1
#define DROP 2
#define CWND 3

// (uint32, uint32) serialized format
// -----------------------------------------------
// | Msg Type | Len (B)  | Uint32    | Uint32    |
// | (1 B)    | (1 B)    | (32 bits) | (32 bits) |
// -----------------------------------------------
struct __attribute__((packed, aligned(4))) UInt32AndUInt32 {
    uint8_t Type;
    uint8_t Len;
    uint32_t Val1;
    uint32_t Val2;
};

// (uint32, uint32, int64) serialized format
// -----------------------------------------------------------
// | Msg Type | Len (B)  | Uint32    | Uint32    | Uint64    |
// | (1 B)    | (1 B)    | (32 bits) | (32 bits) | (64 bits) |
// -----------------------------------------------------------
struct __attribute__((packed, aligned(4))) UInt32AndUInt32AndUInt64 {
    uint8_t Type;
    uint8_t Len;
    uint32_t Val1;
    uint32_t Val2;
    uint64_t Val3;
};

#define MAX_STRING_SIZE 26

// (uint32, string) serialized format
// -----------------------------------------------
// | Msg Type | Len (B)  | Uint32    | String    |
// | (1 B)    | (1 B)    | (32 bits) | (variable)|
// -----------------------------------------------
struct __attribute__((packed, aligned(4))) UInt32AndString {
    uint8_t Type;
    uint8_t Len;
    uint32_t Val;
    char Str[MAX_STRING_SIZE];
};

// (uint32, uint32, string) serialized format
// ----------------------------------------------------------
// | Msg Type | Len (B)  | Uint32    | Uint32    |  String  |
// | (1 B)    | (1 B)    | (32 bits) | (32 bits) |(variable)|
// ----------------------------------------------------------
struct __attribute__((packed, aligned(4))) UInt32AndUInt32AndString {
    uint8_t Type;
    uint8_t Len;
    uint32_t Val;
    uint32_t Val2;
    char Str[MAX_STRING_SIZE];
};

int writeCwndMsg(char *buf, uint32_t sid, uint32_t cwnd);
int readCwndMsg(char *buf, struct UInt32AndUInt32 *msg);

int writeCreateMsg(
    char *buf, 
    uint32_t sid, 
    uint32_t startSeq, 
    char* str
);
int readCreateMsg(char *buf, struct UInt32AndUInt32AndString *msg);

int writeDropMsg(char *buf, uint32_t sid, char* str);
int readDropMsg(char *buf, struct UInt32AndString *msg);

int writeAckMsg(
    char *buf, 
    uint32_t sid, 
    uint32_t ackNo, 
    uint64_t rtt
);
int readAckMsg(char *buf, struct UInt32AndUInt32AndUInt64 *msg);

#endif
