#include <linux/module.h>
#include <net/tcp.h>

#include "tcp_ccp.h"

#define CCP_FRAC_DENOM 10
#define CCP_EWMA_RECENCY 6

void ccp_set_pacing_rate(struct sock *sk) {
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp *ca = inet_csk_ca(sk);
    u64 segs_in_flight; /* desired cwnd as rate * rtt */
    sk->sk_pacing_rate = ca->rate;

    if (likely(ca->mmt.rtt > 0)) {
        segs_in_flight = (u64)ca->rate * ca->mmt.rtt;
        do_div(segs_in_flight, MTU);
        do_div(segs_in_flight, S_TO_US);
        pr_info("ccp: Setting new rate %d Mbit/s (%d Bps) (cwnd %llu)\n", ca->rate / 125000, ca->rate, segs_in_flight + 3);
         //Add few more segments to segs_to_flight to prevent rate underflow due to 
         //temporary RTT fluctuations.
        tp->snd_cwnd = segs_in_flight + 3;
    }
}

static int rate_sample_valid(const struct rate_sample *rs)
{
  int ret = 0;
  if (rs->delivered <= 0)
    ret |= 1;
  if (rs->snd_int_us <= 0)
    ret |= 1 << 1;
  if (rs->rcv_int_us <= 0)
    ret |= 1 << 2;
  if (rs->interval_us <= 0)
    ret |= 1 << 3;
  if (rs->rtt_us <= 0)
    ret |= 1 << 4;
  return ret;
}

void load_primitives( struct sock *sk, const struct rate_sample *rs) {
    // load the primitive registers of the rate sample - convert all to u64
    // raw values, not averaged
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp *ca = inet_csk_ca(sk);
    u64 ack = (u64)(tp->snd_una);
    u64 rtt = (u64)(rs->rtt_us);
    u64 loss = (u64)(rs->losses);
    u64 rin = 0; // send bandwidth in bytes per second
    u64 rout = 0; // recv bandwidth in bytes per second
    int measured_valid_rate = rate_sample_valid(rs);
    pr_info("LOSS is %llu\n", loss);
    if ( measured_valid_rate == 0 ) {
       rin = rout  = (u64)rs->delivered * MTU * S_TO_US;
       do_div(rin, rs->snd_int_us);
       do_div(rout, rs->rcv_int_us);
    } else {
        return;
    }
    ca->mmt.ack = ack;
    ca->mmt.rtt = rtt;
    ca->mmt.loss = loss;
    ca->mmt.rin = rin;
    ca->mmt.rout = rout;
    return;
}

// read values given a register
u64 read_reg(struct Register reg, struct ccp *ca, struct ccp_instruction_list* instr) {
    switch (reg.type) {
        case STATE_REG:
            return instr->state_registers[reg.index];
        case TMP_REG:
            return instr->tmp_registers[reg.index];
        case PRIMITIVE_REG:
            switch (reg.index) {
                case ACK:
                    return ca->mmt.ack;
                case RTT:
                    return ca->mmt.rtt;
                case LOSS:
                    return ca->mmt.loss;
                case RIN:
                    return ca->mmt.rin;
                case ROUT:
                    return ca->mmt.rout;
                default:
                    return 0;
            }
            break;
        case CONST_REG:
            return reg.value;
        default:
            return 0;
    }
    return 0;
}

// write values given a register and a value
void write_reg(struct Register reg, u64 value, struct ccp_instruction_list *instr) {
    switch (reg.type) {
        case STATE_REG:
            pr_info("valu: %llu, index: %d\n", value, reg.index);
            instr->state_registers[reg.index] = value;
            break;
        case TMP_REG:
            instr->tmp_registers[reg.index] = value;
            break;
        default:
            pr_info("Trying to write into register with type %d\n", reg.type);
            break;
    }

}
void update_state_registers(struct ccp *ca) {
    // updates dates all the state registers
    // first grab the relevant instruction set
    struct ccp_instruction_list *instr;
    // for now - just RTT - at state index 0
    int i;
    u64 arg1;
    u64 arg2;
    u64 arg0; // for ewma and if and not if
    int num_instructions;
    struct Instruction64 current_instruction;
    pr_info("about to try to dereference the instr_list for ccp index %d\n", ca->ccp_index);
    instr = ccp_instruction_list_lookup(ca->ccp_index);
    pr_info("deferenced the instr_list for ccp index %d\n", ca->ccp_index);

    num_instructions = instr->num_instructions;
    pr_info("Num instr is %d\n", num_instructions);
    for ( i = 0; i < num_instructions; i++ ) {
        current_instruction = instr->fold_instructions[i];
        pr_info("Trying to read registers");
        arg1 = read_reg(current_instruction.r1, ca, instr);
        arg2 = read_reg(current_instruction.r2, ca, instr);
        pr_info("Op: %d, arg1: %llu, arg2: %llu\n", current_instruction.op, arg1, arg2);
        switch (current_instruction.op) {
            case ADD64:
                pr_info("Reg 1 type: %d, Reg 1 index: %d\n", current_instruction.r1.type, current_instruction.r1.index);
                pr_info("Arg1: %llu, Arg2: %llu\n", arg1, arg2);
                write_reg(current_instruction.rStore, myadd64(arg1, arg2), instr);
                break;
            case DIV64:
                write_reg(current_instruction.rStore, mydiv64(arg1, arg2), instr);
                break;
            case EQUIV64:
                write_reg(current_instruction.rStore, myequiv64(arg1, arg2), instr);
                break;
            case EWMA64:
                arg0 = read_reg(current_instruction.rStore, ca, instr);
                write_reg(current_instruction.rStore, myewma64(arg1, arg0, arg2), instr);
                break;
            case GT64:
                write_reg(current_instruction.rStore, mygt64(arg1, arg2), instr);
                break;
            case LT64:
                write_reg(current_instruction.rStore, mylt64(arg1, arg2), instr);
                break;
            case MAX64:
                write_reg(current_instruction.rStore, mymax64(arg1, arg2), instr);
                break;
            case MIN64:
                write_reg(current_instruction.rStore, mymin64(arg1, arg2), instr);
                break;
            case MUL64:
                write_reg(current_instruction.rStore, mymul64(arg1, arg2), instr);
                break;
            case SUB64:
                write_reg(current_instruction.rStore, mysub64(arg1, arg2), instr);
                break;
            case IFCNT64: // if arg1, adds 1 to register in rStore
                if (arg1 == 1) {
                    write_reg(current_instruction.rStore, myadd64(1, arg2), instr);                 
                }
                break;
            case IFNOTCNT64:
                if (arg1 == 0) {
                    write_reg(current_instruction.rStore, myadd64(1, arg2), instr);
                }
                break;
            case BIND64: // take arg1, and put it in rStore
                pr_info("Arg 1 we're gonna write in is %llu\n", arg1);
                write_reg(current_instruction.rStore, arg1, instr);
            default:
                break;
            
        }

    }
}

void tcp_ccp_cong_control(struct sock *sk, const struct rate_sample *rs) {
    // aggregate measurement
    // state = fold(state, rs)
    // TODO custom fold functions (for now, default only all fields)

    struct ccp *ca = inet_csk_ca(sk);
    struct ccp_instruction_list* instr = ccp_instruction_list_lookup(ca->ccp_index);

    // load primitive registers
    load_primitives(sk, rs);
    // update the signal state
    update_state_registers(ca);


    pr_info("Prim ack: %llu, our ack: %llu\n", ca->mmt.ack, instr->state_registers[ACK]);
    pr_info("Prim rtt: %llu, our rtt: %llu\n", ca->mmt.rtt, instr->state_registers[RTT]);
    pr_info("Prim rin: %llu, our rin: %llu\n", ca->mmt.rin, instr->state_registers[RIN]);
    pr_info("Prim rout: %llu, our rout: %llu\n", ca->mmt.rout, instr->state_registers[ROUT]);
    pr_info("Prim loss: %llu, our loss : %llu\n", ca->mmt.loss, instr->state_registers[LOSS]); 
    // rate control state machine
    sendStateMachine(sk);
}
EXPORT_SYMBOL_GPL(tcp_ccp_cong_control);

/* Slow start threshold is half the congestion window (min 2) */
u32 tcp_ccp_ssthresh(struct sock *sk) {
    const struct tcp_sock *tp = tcp_sk(sk);

    return max(tp->snd_cwnd >> 1U, 2U);
}
EXPORT_SYMBOL_GPL(tcp_ccp_ssthresh);

u32 tcp_ccp_undo_cwnd(struct sock *sk) {
    const struct tcp_sock *tp = tcp_sk(sk);

    return max(tp->snd_cwnd, tp->snd_ssthresh << 1);
}
EXPORT_SYMBOL_GPL(tcp_ccp_undo_cwnd);

void tcp_ccp_pkts_acked(struct sock *sk, const struct ack_sample *sample) {
    struct ccp *cpl;
    s32 sampleRTT;

    cpl = inet_csk_ca(sk);
    sampleRTT = sample->rtt_us;
    //printk(KERN_INFO "pkt sample rtt %d us\n", sampleRTT);
}
EXPORT_SYMBOL_GPL(tcp_ccp_pkts_acked);

/*
 * Detect drops.
 *
 * TCP_CA_Loss -> a timeout happened
 * TCP_CA_Recovery -> an isolated loss (3x dupack) happened.
 * TCP_CA_CWR -> got an ECN
 */
void tcp_ccp_set_state(struct sock *sk, u8 new_state) {
    enum drop_type dtype;
    struct ccp *cpl = inet_csk_ca(sk);
    switch (new_state) {
        case TCP_CA_Recovery:
            printk(KERN_INFO "entered TCP_CA_Recovery (dupack drop)\n");
            dtype = DROP_DUPACK;
            break;
        case TCP_CA_Loss:
            printk(KERN_INFO "entered TCP_CA_Loss (timeout drop)\n");
            dtype = DROP_TIMEOUT;
            break;
        case TCP_CA_CWR:
            printk(KERN_INFO "entered TCP_CA_CWR (ecn drop)\n");
            dtype = DROP_ECN;
            break;
        default:
            printk(KERN_INFO "entered TCP normal state\n");
            cpl->last_drop_state = NO_DROP;
            return;
    }

    if (cpl->last_drop_state == dtype) {
        return;
    }

    cpl->last_drop_state = dtype;
    nl_send_drop_notif(cpl->ccp_index, dtype);
}
EXPORT_SYMBOL_GPL(tcp_ccp_set_state);

void tcp_ccp_init(struct sock *sk) {
    int ok;
    struct tcp_sock *tp;
    struct ccp *cpl;
    struct ccp_instruction_list *instr_list;
    struct ccp_primitives init_mmt = {
        .ack = 0,
        .rtt = 0,
        .loss = 0,
        .rin = 0,
        .rout = 0
    };
    pr_info("In init ccp flow function pls get to this print function omg\n");
    // store initialized netlink sock ptr in connection state
    tp = tcp_sk(sk);
    cpl = inet_csk_ca(sk);
    cpl->ccp_index = ccp_connection_start(sk);
    pr_info("ccp: starting connection %d", cpl->ccp_index);
    
    cpl->next_event_time = tcp_time_stamp;
    cpl->currPatternEvent = 0;
    cpl->numPatternEvents = 0;
    cpl->last_drop_state = NO_DROP;

    memcpy(&(cpl->mmt), &init_mmt, sizeof(struct ccp_primitives));
    // send to CCP:
    // index of pointer back to this sock for IPC callback
    // first ack to expect
    ok = nl_send_conn_create(cpl->ccp_index, tp->snd_una);
    if (ok < 0) {
        pr_info("failed to send create message: %d", ok);
    }
    pr_info("Size of ccp struct is %lu\n", sizeof(struct ccp));
    instr_list = ccp_instruction_list_lookup(cpl->ccp_index);
    pr_info("Num instructions in thing is %d\n", instr_list->num_instructions);


}
EXPORT_SYMBOL_GPL(tcp_ccp_init);

void tcp_ccp_release(struct sock *sk) {
    struct ccp *cpl = inet_csk_ca(sk);
    pr_info("ccp: freeing connection %d", cpl->ccp_index);
    ccp_connection_free(cpl->ccp_index);
}
EXPORT_SYMBOL_GPL(tcp_ccp_release);

struct tcp_congestion_ops tcp_ccp_congestion_ops = {
    .flags = TCP_CONG_NON_RESTRICTED,
    .name = "ccp",
    .owner = THIS_MODULE,
    .init = tcp_ccp_init,
    .release = tcp_ccp_release,
    .ssthresh = tcp_ccp_ssthresh,
    //.cong_avoid = tcp_ccp_cong_avoid,
    .cong_control = tcp_ccp_cong_control,
    .undo_cwnd = tcp_ccp_undo_cwnd,
    .set_state = tcp_ccp_set_state,
    .pkts_acked = tcp_ccp_pkts_acked,
};

static int __init tcp_ccp_register(void) {
    int ok;
    struct sock *nl_sk;
    struct netlink_kernel_cfg cfg = {
        .input = nl_recv,
    };

    printk(KERN_INFO "Init ccp\n");
    ok = ccp_init_connection_map();
    if (ok < 0) {
        return -1;
    }
    ok = ccp_init_fold_map();
    if (ok < 0) {
        return -1;
    }

    nl_sk = netlink_kernel_create(&init_net, NETLINK_USERSOCK, &cfg);
    if (!nl_sk) {
        printk(KERN_ALERT "Error creating netlink socket.\n");
        return -1;
    }
   
    set_ccp_nl_sk(nl_sk);
    printk(KERN_INFO "init NL\n");
    
    return tcp_register_congestion_control(&tcp_ccp_congestion_ops);
}

static void __exit tcp_ccp_unregister(void) {
    printk(KERN_INFO "Exit ccp\n");
    ccp_free_connection_map();
    free_ccp_nl_sk();
    tcp_unregister_congestion_control(&tcp_ccp_congestion_ops);
}

module_init(tcp_ccp_register);
module_exit(tcp_ccp_unregister);

MODULE_AUTHOR("Akshay Narayan <akshayn@mit.edu>");
MODULE_DESCRIPTION("Kernel datapath for a congestion control plane");
MODULE_LICENSE("GPL");
