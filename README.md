# ccp-kernel
Kernel module implementing a kernel datapath for the CCP

## (1) Set up the ccp-kernel datapath

We support Linux kernel versions 4.13 and 4.14. For later kernel versions,
you may be interested in [our patch](https://github.com/ngsrinivas/linux-fork/),
which [is being upstreamed](https://patchwork.ozlabs.org/patch/941532/).

Once you have compiled (`make`) the kernel module, you can load it:

```
sudo ./ccp_kernel_load ipc=0
```

to use the default netlink socket IPC backend. To use our character device:

```
sudo ./ccp_kernel_load ipc=1
```

Once you have done this, you should
see CCP as one of the available TCP congestion control algorithms:

```
sudo sysctl net.ipv4.tcp_available_congestion_control
```

There should also be some initialization log messages in the syslog:

```
dmesg
```

## (2) Set kernel congfigurations

You should add the CCP to the list of allowed congestion control
algorithms, and make FQ your default qdisc (FQ is used for packet
pacing). In later kernels, CCP also supports in-stack pacing.

```
sudo sysctl -w net.ipv4.tcp_allowed_congestion_control="cubic reno ccp"
sudo sysctl -w net.core.default_qdisc=fq
```

You've now started the CCP data path in the Linux kernel. 
To run new CCP transport algorithms, you must set up and run the
CCP user-space (https://github.mit.edu/nebula/ccp) separately.

## (3) Test CCP

After setting up both the user-space (ccp) and kernel (ccp-kernel) components, 
you can use applications using the standard socket API (e.g., `iperf`) to run transfers over
any CCP transport algorithm.
