# ccp-kernel [![Build Status](https://travis-ci.org/ccp-project/ccp-kernel.svg?branch=master)](https://travis-ci.org/ccp-project/ccp-kernel)

Kernel module implementing a kernel datapath for the CCP

## (1) Compile a custom kernel
ccp-kernel runs on a patched Linux 4.10 kernel. Get the patched kernel sources from

https://github.com/ngsrinivas/linux-fork

and follow the instructions from

https://kernelnewbies.org/KernelBuild

You don't need to download the latest stable build as shown there; the
linux-fork repository has 
the source code you need. Compile the kernel from the nimbus branch,
and follow the tutorial until the point you're able to boot into the
freshly compiled kernel.

If you run Ubuntu 17.04, which comes with a default 4.10 kernel,
you'll have minimal issues with your kernel configuration. We've also
tested the custom kernel and modules on Ubuntu 16.04 LTS without too
much trouble.

## (2) Set up the ccp-kernel datapath

Once you boot into the custom kernel, you should be able to compile
and load the ccp-kernel modules from the master branch (use `modprobe`
or `insmod` to load the compiled .ko module). Once loaded, you should
see ccp as one of the available TCP congestion control algorithms:

```
sudo sysctl net.ipv4.tcp_available_congestion_control
```

You should add the CCP to the list of allowed congestion control
algorithms, and make FQ your default qdisc (FQ is used for packet
pacing):

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
