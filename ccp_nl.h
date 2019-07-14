/* 
 * CCP Datapath Netlink Socket Interface
 *
 * Wrapper around kernel-side netlink sockets for communication with userspace CCP.
 */
#ifndef CCP_NL_H
#define CCP_NL_H

#include "libccp/ccp.h"

typedef int (*ccp_nl_recv_handler)(struct ccp_datapath *datapath, char *msg, int msg_size);

/* Create a netlink kernel socket
 * A global (struct sock*), ccp_nl_sk, will get set so we can use the socket
 * There is *only one* netlink socket active *per datapath*
 */
int ccp_nl_sk(ccp_nl_recv_handler msg);

/* Wrap netlink_kernel_release of (struct sock *ccp_nl_sk).
 */
void free_ccp_nl_sk(void);

/* Send serialized message to userspace CCP
 */
int nl_sendmsg(
    struct ccp_connection *conn,
    char *msg, 
    int msg_size
);

#endif
